// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>

#include <fmt/format.h>

#include "common/logging/log.h"
#include "core/vr/guest_frames.h"
#include "core/libraries/error_codes.h"
#include "core/libraries/hmd/hmd.h"
#include "core/libraries/hmd/hmd_error.h"
#include "core/libraries/libs.h"
#include "core/libraries/videoout/video_out.h"
#include "core/memory.h"
#include "core/vr/vr_runtime.h"

namespace Libraries::Hmd {

namespace {

struct UserEvent {
    Libraries::Kernel::OrbisKernelEqueue eq{};
    s32 id{};
    bool is_set{};

    void Trigger() const {
        if (!is_set) {
            return;
        }
        if (auto* equeue = Libraries::Kernel::GetEqueue(eq); equeue != nullptr) {
            equeue->TriggerEvent(id, Libraries::Kernel::OrbisKernelEvent::Filter::User, nullptr);
        }
    }
};

// On real hardware a system thread warps the latest submitted frame to the current head pose
// and scans it out on every panel refresh. Here the host compositor does the warping, so this
// only has to keep the guest-visible timing alive and pass submitted frames along.
struct Reprojection {
    std::mutex mutex;
    bool initialized{};
    s32 video_out_handle{-1};
    s32 display_index[2]{-1, -1};
    u32 submitted_frames{};
    UserEvent start_event;
    UserEvent end_event;
};

Reprojection g_reprojection;

Core::Vr::Fov FovFromUv(const OrbisHmdReprojectionEyeUv& left_eye) {
    // uv = tan * scale + offset, so the image edges (uv 0 and 1) give the half angles back.
    // On the left eye the left edge is the temple side.
    return {
        .tan_out = left_eye.offset_x / left_eye.scale_x,
        .tan_in = (1.0f - left_eye.offset_x) / left_eye.scale_x,
        .tan_top = left_eye.offset_y / left_eye.scale_y,
        .tan_bottom = (1.0f - left_eye.offset_y) / left_eye.scale_y,
    };
}


/// Queues one stereo frame for the headset: what sceHmdReprojectionStart and its variants end in.
s32 SubmitFrame(Libraries::VideoOut::HmdFrame& frame) {
    s32 video_out_handle;
    u32 frame_number;
    {
        std::scoped_lock lock{g_reprojection.mutex};
        if (!g_reprojection.initialized || g_reprojection.video_out_handle < 0) {
            return ORBIS_HMD_ERROR_NOT_INITIALIZED;
        }
        video_out_handle = g_reprojection.video_out_handle;
        frame_number = g_reprojection.submitted_frames++;
        // The reprojection alternates between the two display buffers it was given.
        frame.display_index = g_reprojection.display_index[frame_number & 1];
    }

    if (frame_number < 4) {
        const auto& left = frame.eye_textures[0];
        LOG_INFO(Lib_Hmd,
                 "frame {}: flip_arg = {}, eye {}x{} at {:#x} / {:#x}, tan out/in/top/bottom = "
                 "{:.4f}/{:.4f}/{:.4f}/{:.4f}",
                 frame_number, frame.flip_arg, left.width + 1, left.height + 1, left.Address(),
                 frame.eye_textures[1].Address(), frame.fov.tan_out, frame.fov.tan_in,
                 frame.fov.tan_top, frame.fov.tan_bottom);
    }

    Core::Vr::GuestFrames::Note();

    // How fast the title delivers frames is the number that matters for comfort; report it now
    // and then.
    {
        using Clock = std::chrono::steady_clock;
        static constexpr auto ReportInterval = std::chrono::seconds{10};
        static Clock::time_point report_time = Clock::now();
        static u32 report_frame = 0;
        const auto now = Clock::now();
        if (now - report_time >= ReportInterval) {
            const float seconds = std::chrono::duration<float>(now - report_time).count();
            LOG_INFO(Lib_Hmd, "{} headset frames so far, {:.1f} per second", frame_number,
                     static_cast<float>(frame_number - report_frame) / seconds);
            report_time = now;
            report_frame = frame_number;
        }
    }

    const s32 result = Libraries::VideoOut::SubmitHmdFrame(video_out_handle, frame);
    if (result != ORBIS_OK) {
        LOG_ERROR(Lib_Hmd, "Could not queue headset frame: {:#x}", static_cast<u32>(result));
    }
    return ORBIS_OK;
}

// ---- sceHmdReprojectionStartMultilayer -------------------------------------------------------
// Its parameters are not documented anywhere public, and the titles AstroQuest was made with
// never call it (Unity's PSVR support does). Until their layout is known, it is found in the
// data: the eye images are the first two valid 2D texture descriptors (by pointer or inline) of
// the same size in what the first argument points at, the head pose a position and unit
// quaternion. What was found is kept and only checked again for later frames. The first calls
// are dumped to the log so the layout can be written down properly.

/// Whether `size` bytes at guest address `addr` can be read without faulting.
bool Readable(VAddr addr, u64 size) {
    // Guest pointers: 8-byte aligned, within the 40 bits the console's GPU can address, above
    // the null page area. (Filters plain numbers out before the memory manager is asked.)
    if (addr < 0x400000 || addr >= 0x10000000000ULL || (addr & 7) != 0) {
        return false;
    }
    void* start{};
    void* end{};
    u32 prot{};
    auto* memory = Core::Memory::Instance();
    if (memory->QueryProtection(addr, &start, &end, &prot) != ORBIS_OK) {
        return false;
    }
    return (prot & static_cast<u32>(Core::MemoryProt::CpuRead)) != 0 &&
           addr + size <= reinterpret_cast<VAddr>(end);
}

bool PlausibleEyeImage(const AmdGpu::Image& image) {
    const auto type = static_cast<AmdGpu::ImageType>(image.type);
    return image.Valid() &&
           (type == AmdGpu::ImageType::Color2D || type == AmdGpu::ImageType::Color2DArray ||
            type == AmdGpu::ImageType::Color2DMsaa) &&
           image.width + 1 >= 256 && image.height + 1 >= 256 && image.Address() != 0 &&
           image.Address() < 0x10000000000ULL;
}

bool PlausibleUv(const OrbisHmdReprojectionEyeUv& uv) {
    const auto in = [](float v, float lo, float hi) { return std::isfinite(v) && v > lo && v < hi; };
    return in(uv.scale_x, 0.1f, 4.0f) && in(uv.scale_y, 0.1f, 4.0f) &&
           in(uv.offset_x, 0.05f, 0.95f) && in(uv.offset_y, 0.05f, 0.95f);
}

bool PlausiblePose(const float* p) {
    for (int i = 0; i < 7; ++i) {
        if (!std::isfinite(p[i])) {
            return false;
        }
    }
    const float norm = p[3] * p[3] + p[4] * p[4] + p[5] * p[5] + p[6] * p[6];
    return std::abs(norm - 1.0f) < 0.01f && std::abs(p[0]) < 10.0f && std::abs(p[1]) < 10.0f &&
           std::abs(p[2]) < 10.0f;
}

void DumpGuest(std::string_view what, VAddr addr, u32 size) {
    if (!Readable(addr, size)) {
        LOG_INFO(Lib_Hmd, "Multilayer dump: {} = {:#x} (not readable memory)", what, addr);
        return;
    }
    const u8* bytes = reinterpret_cast<const u8*>(addr);
    for (u32 row = 0; row < size; row += 32) {
        std::string line;
        for (u32 i = row; i < row + 32 && i < size; i += 8) {
            u64 value;
            std::memcpy(&value, bytes + i, 8);
            line += fmt::format(" {:016x}", value);
        }
        LOG_INFO(Lib_Hmd, "Multilayer dump: {} {:#x} +{:03x}:{}", what, addr, row, line);
    }
}

constexpr u32 ScanBytes = 0x400;

struct MultilayerLayout {
    bool found = false;
    bool inline_images = false; ///< Descriptors in the parameter block itself, not pointed to.
    std::array<u32, 2> image_at{}; ///< Offsets in the parameter block.
    s32 uv_at = -1;                ///< Offset of the left eye's UV, -1: not found.
    s32 pose_arg = -1;             ///< Argument (1..3) pointing at the pose, -1: in the block.
    s32 pose_at = -1;              ///< Offset of the pose there, -1: not found.
};
MultilayerLayout g_multilayer;

std::optional<AmdGpu::Image> ImageAt(VAddr block, u32 offset, bool inline_image) {
    VAddr at = block + offset;
    if (!inline_image) {
        if (!Readable(at, 8)) {
            return std::nullopt;
        }
        std::memcpy(&at, reinterpret_cast<const void*>(at), 8);
    }
    if (!Readable(at, sizeof(AmdGpu::Image))) {
        return std::nullopt;
    }
    AmdGpu::Image image;
    std::memcpy(&image, reinterpret_cast<const void*>(at), sizeof(image));
    if (!PlausibleEyeImage(image)) {
        return std::nullopt;
    }
    return image;
}

bool FindMultilayerLayout(const std::array<u64, 6>& args) {
    const VAddr block = args[0];
    if (!Readable(block, 8)) {
        return false;
    }
    // The block may be shorter than what is scanned: stop where its mapping ends.
    u32 limit = 8;
    while (limit < ScanBytes && Readable(block, limit + 8)) {
        limit += 8;
    }
    MultilayerLayout layout;
    for (const bool inline_images : {false, true}) {
        const u32 step = 8;
        const u32 last = inline_images ? limit - sizeof(AmdGpu::Image) : limit - 8;
        for (u32 first = 0; first <= last && !layout.found; first += step) {
            const auto left = ImageAt(block, first, inline_images);
            if (!left) {
                continue;
            }
            for (u32 second = first + step; second <= last && second <= first + 0x100;
                 second += step) {
                const auto right = ImageAt(block, second, inline_images);
                if (right && right->width == left->width && right->height == left->height &&
                    right->Address() != left->Address()) {
                    layout = {.found = true,
                              .inline_images = inline_images,
                              .image_at = {first, second}};
                    break;
                }
            }
        }
        if (layout.found) {
            break;
        }
    }
    if (!layout.found) {
        return false;
    }
    // UV: as in sceHmdReprojectionStart, somewhere after the images (it follows a sampler
    // pointer there); two eyes' worth, the left one first.
    const u8* bytes = reinterpret_cast<const u8*>(block);
    for (u32 at = layout.image_at[1] + 8; at + 2 * sizeof(OrbisHmdReprojectionEyeUv) <= limit;
         at += 4) {
        OrbisHmdReprojectionEyeUv uv[2];
        std::memcpy(uv, bytes + at, sizeof(uv));
        if (PlausibleUv(uv[0]) && PlausibleUv(uv[1])) {
            layout.uv_at = static_cast<s32>(at);
            break;
        }
    }
    // Pose: what the second argument points at first (as in sceHmdReprojectionStart), then the
    // other arguments, then the block itself.
    const auto find_pose = [](VAddr where, u32 size) -> s32 {
        for (u32 at = 0; at + 7 * sizeof(float) <= size; at += 4) {
            if (!Readable(where + (at & ~7u), 8 * sizeof(float))) {
                return -1;
            }
            float pose[7];
            std::memcpy(pose, reinterpret_cast<const void*>(where + at), sizeof(pose));
            if (PlausiblePose(pose)) {
                return static_cast<s32>(at);
            }
        }
        return -1;
    };
    for (s32 arg = 1; arg <= 3 && layout.pose_at < 0; ++arg) {
        if (Readable(args[arg], 64)) {
            layout.pose_at = find_pose(args[arg], 0x80);
            layout.pose_arg = layout.pose_at >= 0 ? arg : -1;
        }
    }
    if (layout.pose_at < 0) {
        layout.pose_at = find_pose(block, limit);
        layout.pose_arg = -1;
    }
    LOG_INFO(Lib_Hmd,
             "Multilayer layout: eye images {} at +{:#x} / +{:#x}, UV {}, pose {} at +{:#x}",
             layout.inline_images ? "inline" : "by pointer", layout.image_at[0],
             layout.image_at[1],
             layout.uv_at >= 0 ? fmt::format("at +{:#x}", layout.uv_at) : "not found",
             layout.pose_arg >= 0 ? fmt::format("in argument {}", layout.pose_arg) : "in block",
             layout.pose_at);
    g_multilayer = layout;
    return true;
}

} // namespace

void OnVblank() {
    UserEvent start_event;
    UserEvent end_event;
    {
        std::scoped_lock lock{g_reprojection.mutex};
        if (!g_reprojection.initialized) {
            return;
        }
        start_event = g_reprojection.start_event;
        end_event = g_reprojection.end_event;
    }
    start_event.Trigger();
    end_event.Trigger();
}

s32 PS4_SYSV_ABI sceHmdReprojectionStartMultilayer(u64 arg0, u64 arg1, u64 arg2, u64 arg3,
                                                   u64 arg4, u64 arg5) {
    const std::array<u64, 6> args{arg0, arg1, arg2, arg3, arg4, arg5};
    static u32 calls = 0;
    const u32 call = calls++;
    // The first calls, and one now and then, go to the log in full.
    if (call < 3 || (call % 1200) == 0 || (!g_multilayer.found && call < 10)) {
        LOG_INFO(Lib_Hmd, "Multilayer call {}: args {:#x} {:#x} {:#x} {:#x} {:#x} {:#x}", call,
                 arg0, arg1, arg2, arg3, arg4, arg5);
        DumpGuest("arg0", arg0, ScanBytes);
        DumpGuest("arg1", arg1, 0x80);
    }

    // Looked for on the first calls, then once a second: a search asks the memory manager about
    // every number in the block, and it logs each one that is no mapped address.
    const bool search = call < 10 || (call % 90) == 0;
    if (!g_multilayer.found && (!search || !FindMultilayerLayout(args))) {
        if (call < 10 || (call % 600) == 0) {
            LOG_WARNING(Lib_Hmd, "Multilayer call {}: no eye images found in its parameters",
                        call);
        }
        return ORBIS_OK;
    }
    const auto& layout = g_multilayer;
    const auto left = ImageAt(arg0, layout.image_at[0], layout.inline_images);
    const auto right = ImageAt(arg0, layout.image_at[1], layout.inline_images);
    if (!left || !right) {
        // The layout no longer fits: look again next time.
        LOG_WARNING(Lib_Hmd, "Multilayer call {}: the eye images moved, looking again", call);
        g_multilayer.found = false;
        return ORBIS_OK;
    }

    Libraries::VideoOut::HmdFrame frame{};
    frame.eye_textures = {*left, *right};
    frame.fov = Core::Vr::Runtime::Instance().GetConfig().fov;
    if (layout.uv_at >= 0) {
        OrbisHmdReprojectionEyeUv uv;
        std::memcpy(&uv, reinterpret_cast<const u8*>(arg0) + layout.uv_at, sizeof(uv));
        if (PlausibleUv(uv)) {
            frame.fov = FovFromUv(uv);
        }
    }
    if (layout.pose_at >= 0) {
        const VAddr where = layout.pose_arg >= 0 ? args[layout.pose_arg] : arg0;
        float pose[7];
        if (Readable(where + (layout.pose_at & ~7), sizeof(pose) + 8)) {
            std::memcpy(pose, reinterpret_cast<const u8*>(where) + layout.pose_at, sizeof(pose));
            if (PlausiblePose(pose)) {
                frame.render_pose = {.position{pose[0], pose[1], pose[2]},
                                     .orientation{pose[3], pose[4], pose[5], pose[6]}};
            }
        }
    }
    // As with sceHmdReprojectionStart: the flip argument follows the pose argument.
    frame.flip_arg = static_cast<s64>(layout.pose_arg == 1 ? arg2 : arg1);
    return SubmitFrame(frame);
}

s32 PS4_SYSV_ABI sceHmdReprojectionAddDisplayBuffer() {
    LOG_ERROR(Lib_Hmd, "(STUBBED) called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceHmdReprojectionClearUserEventEnd() {
    LOG_INFO(Lib_Hmd, "called");
    std::scoped_lock lock{g_reprojection.mutex};
    g_reprojection.end_event = {};
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceHmdReprojectionClearUserEventStart() {
    LOG_INFO(Lib_Hmd, "called");
    std::scoped_lock lock{g_reprojection.mutex};
    g_reprojection.start_event = {};
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceHmdReprojectionDebugGetLastInfo() {
    LOG_ERROR(Lib_Hmd, "(STUBBED) called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceHmdReprojectionDebugGetLastInfoMultilayer() {
    LOG_ERROR(Lib_Hmd, "(STUBBED) called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceHmdReprojectionFinalize() {
    LOG_INFO(Lib_Hmd, "called");
    std::scoped_lock lock{g_reprojection.mutex};
    g_reprojection.initialized = false;
    g_reprojection.video_out_handle = -1;
    g_reprojection.start_event = {};
    g_reprojection.end_event = {};
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceHmdReprojectionFinalizeCapture() {
    LOG_ERROR(Lib_Hmd, "(STUBBED) called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceHmdReprojectionInitialize(const OrbisHmdReprojectionResourceInfo* resource,
                                              s32 type, void* option) {
    if (resource == nullptr) {
        return ORBIS_HMD_ERROR_PARAMETER_NULL;
    }
    LOG_INFO(Lib_Hmd,
             "called, type = {}, thread_priority = {}, cpu_affinity_mask = {:#x}, pipe_id = {}, "
             "queue_id = {}",
             type, resource->thread_priority, resource->cpu_affinity_mask, resource->pipe_id,
             resource->queue_id);
    std::scoped_lock lock{g_reprojection.mutex};
    g_reprojection.initialized = true;
    g_reprojection.submitted_frames = 0;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceHmdReprojectionInitializeCapture() {
    LOG_ERROR(Lib_Hmd, "(STUBBED) called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceHmdReprojectionQueryGarlicBuffAlign() {
    return 0x100;
}

s32 PS4_SYSV_ABI sceHmdReprojectionQueryGarlicBuffSize() {
    return 0x100000;
}

s32 PS4_SYSV_ABI sceHmdReprojectionQueryOnionBuffAlign() {
    return 0x100;
}

s32 PS4_SYSV_ABI sceHmdReprojectionQueryOnionBuffSize() {
    return 0x810;
}

s32 PS4_SYSV_ABI sceHmdReprojectionSetCallback() {
    LOG_ERROR(Lib_Hmd, "(STUBBED) called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceHmdReprojectionSetDisplayBuffers(s32 video_out_handle, s32 index0, s32 index1,
                                                     void* option) {
    LOG_INFO(Lib_Hmd, "called, video_out_handle = {}, index0 = {}, index1 = {}", video_out_handle,
             index0, index1);
    std::scoped_lock lock{g_reprojection.mutex};
    g_reprojection.video_out_handle = video_out_handle;
    g_reprojection.display_index[0] = index0;
    g_reprojection.display_index[1] = index1;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceHmdReprojectionSetOutputMinColor(float red, float green, float blue) {
    LOG_DEBUG(Lib_Hmd, "called, red = {}, green = {}, blue = {}", red, green, blue);
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceHmdReprojectionSetUserEventEnd(Libraries::Kernel::OrbisKernelEqueue eq,
                                                   s32 id) {
    LOG_INFO(Lib_Hmd, "called, eq = {}, id = {}", eq, id);
    std::scoped_lock lock{g_reprojection.mutex};
    g_reprojection.end_event = {.eq = eq, .id = id, .is_set = true};
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceHmdReprojectionSetUserEventStart(Libraries::Kernel::OrbisKernelEqueue eq,
                                                     s32 id) {
    LOG_INFO(Lib_Hmd, "called, eq = {}, id = {}", eq, id);
    std::scoped_lock lock{g_reprojection.mutex};
    g_reprojection.start_event = {.eq = eq, .id = id, .is_set = true};
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceHmdReprojectionStart(const OrbisHmdReprojectionParam* param,
                                         const OrbisHmdReprojectionTrackerState* tracker_state,
                                         s64 flip_arg, s32 option) {
    if (param == nullptr || tracker_state == nullptr || param->texture[0] == nullptr ||
        param->texture[1] == nullptr) {
        return ORBIS_HMD_ERROR_PARAMETER_NULL;
    }

    Libraries::VideoOut::HmdFrame frame{};
    std::memcpy(&frame.eye_textures[0], param->texture[0], sizeof(AmdGpu::Image));
    std::memcpy(&frame.eye_textures[1], param->texture[1], sizeof(AmdGpu::Image));
    frame.fov = FovFromUv(param->uv[0]);
    frame.render_pose = {
        .position{tracker_state->position[0], tracker_state->position[1],
                  tracker_state->position[2]},
        .orientation{tracker_state->orientation[0], tracker_state->orientation[1],
                     tracker_state->orientation[2], tracker_state->orientation[3]},
    };
    frame.flip_arg = flip_arg;

    if (g_reprojection.submitted_frames < 4) {
        LOG_INFO(Lib_Hmd,
                 "eye UV: left scale {:.6f}/{:.6f}, offset {:.6f}/{:.6f}; "
                 "right scale {:.6f}/{:.6f}, offset {:.6f}/{:.6f}; unknown_40 = {:#x}, "
                 "flags = {:#x}, option = {}",
                 param->uv[0].scale_x, param->uv[0].scale_y, param->uv[0].offset_x,
                 param->uv[0].offset_y, param->uv[1].scale_x, param->uv[1].scale_y,
                 param->uv[1].offset_x, param->uv[1].offset_y, param->unknown_40, param->flags,
                 option);
    }
    return SubmitFrame(frame);
}

s32 PS4_SYSV_ABI sceHmdReprojectionStart2dVr() {
    LOG_ERROR(Lib_Hmd, "(STUBBED) called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceHmdReprojectionStartCapture() {
    LOG_ERROR(Lib_Hmd, "(STUBBED) called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceHmdReprojectionStartLiveCapture() {
    LOG_ERROR(Lib_Hmd, "(STUBBED) called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceHmdReprojectionStartMultilayer2() {
    LOG_ERROR(Lib_Hmd, "(STUBBED) called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceHmdReprojectionStartWideNear() {
    LOG_ERROR(Lib_Hmd, "(STUBBED) called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceHmdReprojectionStartWideNearWithOverlay() {
    LOG_ERROR(Lib_Hmd, "(STUBBED) called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceHmdReprojectionStartWithOverlay() {
    LOG_ERROR(Lib_Hmd, "(STUBBED) called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceHmdReprojectionStop() {
    LOG_INFO(Lib_Hmd, "called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceHmdReprojectionStopCapture() {
    LOG_ERROR(Lib_Hmd, "(STUBBED) called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceHmdReprojectionStopLiveCapture() {
    LOG_ERROR(Lib_Hmd, "(STUBBED) called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceHmdReprojectionUnsetCallback() {
    LOG_ERROR(Lib_Hmd, "(STUBBED) called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceHmdReprojectionUnsetDisplayBuffers() {
    LOG_INFO(Lib_Hmd, "called");
    std::scoped_lock lock{g_reprojection.mutex};
    g_reprojection.video_out_handle = -1;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI Func_A31A0320D80EAD99() {
    LOG_ERROR(Lib_Hmd, "(STUBBED) called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI Func_B9A6FA0735EC7E49() {
    LOG_ERROR(Lib_Hmd, "(STUBBED) called");
    return ORBIS_OK;
}

void RegisterReprojection(Core::Loader::SymbolsResolver* sym) {
    LIB_FUNCTION("8gH1aLgty5I", "libsceHmdReprojectionMultilayer", 1, "libSceHmd",
                 sceHmdReprojectionStartMultilayer);
    LIB_FUNCTION("NTIbBpSH9ik", "libSceHmd", 1, "libSceHmd", sceHmdReprojectionAddDisplayBuffer);
    LIB_FUNCTION("94+Ggm38KCg", "libSceHmd", 1, "libSceHmd", sceHmdReprojectionClearUserEventEnd);
    LIB_FUNCTION("mdyFbaJj66M", "libSceHmd", 1, "libSceHmd", sceHmdReprojectionClearUserEventStart);
    LIB_FUNCTION("MdV0akauNow", "libSceHmd", 1, "libSceHmd", sceHmdReprojectionDebugGetLastInfo);
    LIB_FUNCTION("ymiwVjPB5+k", "libSceHmd", 1, "libSceHmd",
                 sceHmdReprojectionDebugGetLastInfoMultilayer);
    LIB_FUNCTION("ZrV5YIqD09I", "libSceHmd", 1, "libSceHmd", sceHmdReprojectionFinalize);
    LIB_FUNCTION("utHD2Ab-Ixo", "libSceHmd", 1, "libSceHmd", sceHmdReprojectionFinalizeCapture);
    LIB_FUNCTION("OuygGEWkins", "libSceHmd", 1, "libSceHmd", sceHmdReprojectionInitialize);
    LIB_FUNCTION("BTrQnC6fcAk", "libSceHmd", 1, "libSceHmd", sceHmdReprojectionInitializeCapture);
    LIB_FUNCTION("TkcANcGM0s8", "libSceHmd", 1, "libSceHmd",
                 sceHmdReprojectionQueryGarlicBuffAlign);
    LIB_FUNCTION("z0KtN1vqF2E", "libSceHmd", 1, "libSceHmd", sceHmdReprojectionQueryGarlicBuffSize);
    LIB_FUNCTION("IWybWbR-xvA", "libSceHmd", 1, "libSceHmd", sceHmdReprojectionQueryOnionBuffAlign);
    LIB_FUNCTION("kLUAkN6a1e8", "libSceHmd", 1, "libSceHmd", sceHmdReprojectionQueryOnionBuffSize);
    LIB_FUNCTION("6CRWGc-evO4", "libSceHmd", 1, "libSceHmd", sceHmdReprojectionSetCallback);
    LIB_FUNCTION("E+dPfjeQLHI", "libSceHmd", 1, "libSceHmd", sceHmdReprojectionSetDisplayBuffers);
    LIB_FUNCTION("LjdLRysHU6Y", "libSceHmd", 1, "libSceHmd", sceHmdReprojectionSetOutputMinColor);
    LIB_FUNCTION("knyIhlkpLgE", "libSceHmd", 1, "libSceHmd", sceHmdReprojectionSetUserEventEnd);
    LIB_FUNCTION("7as0CjXW1B8", "libSceHmd", 1, "libSceHmd", sceHmdReprojectionSetUserEventStart);
    LIB_FUNCTION("dntZTJ7meIU", "libSceHmd", 1, "libSceHmd", sceHmdReprojectionStart);
    LIB_FUNCTION("q3e8+nEguyE", "libSceHmd", 1, "libSceHmd", sceHmdReprojectionStart2dVr);
    LIB_FUNCTION("RrvyU1pjb9A", "libSceHmd", 1, "libSceHmd", sceHmdReprojectionStartCapture);
    LIB_FUNCTION("XZ5QUzb4ae0", "libSceHmd", 1, "libSceHmd", sceHmdReprojectionStartLiveCapture);
    LIB_FUNCTION("8gH1aLgty5I", "libSceHmd", 1, "libSceHmd", sceHmdReprojectionStartMultilayer);
    LIB_FUNCTION("gqAG7JYeE7A", "libSceHmd", 1, "libSceHmd", sceHmdReprojectionStartMultilayer2);
    LIB_FUNCTION("3JyuejcNhC0", "libSceHmd", 1, "libSceHmd", sceHmdReprojectionStartWideNear);
    LIB_FUNCTION("mKa8scOc4-k", "libSceHmd", 1, "libSceHmd",
                 sceHmdReprojectionStartWideNearWithOverlay);
    LIB_FUNCTION("kcldQ7zLYQQ", "libSceHmd", 1, "libSceHmd", sceHmdReprojectionStartWithOverlay);
    LIB_FUNCTION("vzMEkwBQciM", "libSceHmd", 1, "libSceHmd", sceHmdReprojectionStop);
    LIB_FUNCTION("F7Sndm5teWw", "libSceHmd", 1, "libSceHmd", sceHmdReprojectionStopCapture);
    LIB_FUNCTION("PAa6cUL5bR4", "libSceHmd", 1, "libSceHmd", sceHmdReprojectionStopLiveCapture);
    LIB_FUNCTION("0wnZViigP9o", "libSceHmd", 1, "libSceHmd", sceHmdReprojectionUnsetCallback);
    LIB_FUNCTION("iGNNpDDjcwo", "libSceHmd", 1, "libSceHmd", sceHmdReprojectionUnsetDisplayBuffers);
    LIB_FUNCTION("oxoDINgOrZk", "libSceHmd", 1, "libSceHmd", Func_A31A0320D80EAD99);
    LIB_FUNCTION("uab6BzXsfkk", "libSceHmd", 1, "libSceHmd", Func_B9A6FA0735EC7E49);
}
} // namespace Libraries::Hmd
