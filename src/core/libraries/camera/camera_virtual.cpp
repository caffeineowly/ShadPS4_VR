// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <thread>
#include <utility>
#include <vector>

#include "core/emulator_settings.h"
#include "core/libraries/camera/camera_error.h"
#include "core/libraries/camera/camera_virtual.h"
#include "core/libraries/error_codes.h"
#include "core/libraries/kernel/time.h"
#include "core/vr/guest_frames.h"
#include "core/vr/vr_runtime.h"

namespace Libraries::Camera::Virtual {

static constexpr u32 c_width = 1280, c_height = 800;
static constexpr u64 VirtualFramePeriodUs = 1000000 / 60;
static std::vector<u8> g_virtual_frame;
static bool g_virtual_started = false;
static u64 g_virtual_frame_count = 0;
static u64 g_virtual_next_frame_time = 0;
static u64 g_virtual_frame_time = 0;
/// How many frames the guest had presented when the sensor delivered its latest frame.
static u64 g_virtual_guest_frames = 0;
// Titles read the sensor settings back out of each frame's metadata to tell whether the frame
// was captured with the exposure tracking needs, so the virtual sensor has to remember them.
static OrbisCameraExposureGain g_virtual_exposure_gain[ORBIS_CAMERA_MAX_DEVICE_NUM]{};

bool IsActive() {
    return Core::Vr::Runtime::Instance().IsHeadsetConnected();
}

bool IsCameraConnected() {
    return IsActive() || EmulatorSettings.GetCameraId() != -1;
}

void Start() {
    // Large enough for a full resolution frame in any of the 16 bit formats.
    g_virtual_frame.assign(c_width * c_height * 2, 0);
    g_virtual_started = true;
    g_virtual_next_frame_time = 0;
    g_virtual_guest_frames = Core::Vr::GuestFrames::Count();
}

void Stop() {
    g_virtual_started = false;
}

void SetExposureGain(OrbisCameraChannel channel, const OrbisCameraExposureGain* exposure_gain) {
    if (exposure_gain == nullptr) {
        return;
    }
    // The channel is a mask of the two sensors.
    for (s32 device = 0; device < ORBIS_CAMERA_MAX_DEVICE_NUM; ++device) {
        if (std::to_underlying(channel) & (1 << device)) {
            g_virtual_exposure_gain[device] = *exposure_gain;
        }
    }
}

s32 GetFrameData(OrbisCameraFrameData* frame_data) {
    if (!g_virtual_started) {
        return ORBIS_CAMERA_ERROR_NOT_START;
    }

    // A read either asks for the frame after the one it got last, and then blocks until the
    // sensor delivers it, or takes the latest frame as it is.
    static constexpr u32 ReadModeWaitNextFrame = 1;
    if ((frame_data->readMode & ReadModeWaitNextFrame) != 0 || g_virtual_frame_count == 0) {
        const u64 now = Libraries::Kernel::sceKernelGetProcessTime();
        if (g_virtual_next_frame_time > now) {
            std::this_thread::sleep_for(
                std::chrono::microseconds(g_virtual_next_frame_time - now));
        }
        // Titles keep what they read in a ring of three entries: one thread reads the camera
        // into the next entry while the main thread uses the newest complete one, trusting that
        // it never falls two camera frames behind. A title that is emulated slower than a
        // console runs it does, and finds the entry it is looking at wiped for reuse. So this
        // sensor never runs ahead of the title: a frame for every frame the title presents. The
        // time limit keeps the camera alive while the title presents nothing at all.
        static constexpr auto GuestFrameTimeout = std::chrono::milliseconds{200};
        g_virtual_guest_frames =
            Core::Vr::GuestFrames::WaitFor(g_virtual_guest_frames + 1, GuestFrameTimeout);

        g_virtual_frame_time = Libraries::Kernel::sceKernelGetProcessTime();
        g_virtual_next_frame_time =
            std::max(g_virtual_next_frame_time, g_virtual_frame_time) + VirtualFramePeriodUs;
        ++g_virtual_frame_count;
    }
    const u64 timestamp = g_virtual_frame_time;
    const u64 frame_number = g_virtual_frame_count - 1;

    OrbisCameraFrameData frame{};
    const OrbisCameraConfigExtention* configs[ORBIS_CAMERA_MAX_DEVICE_NUM] = {&output_config0,
                                                                              &output_config1};
    for (s32 device = 0; device < ORBIS_CAMERA_MAX_DEVICE_NUM; ++device) {
        for (s32 level = 0; level < ORBIS_CAMERA_MAX_FORMAT_LEVEL_NUM; ++level) {
            const u32 width = c_width >> level;
            const u32 height = c_height >> level;
            frame.framePosition[device][level] = {0, 0, width, height};
            frame.pFramePointerList[device][level] = g_virtual_frame.data();
            frame.pFramePointerListGarlic[device][level] = g_virtual_frame.data();
            frame.frameSize[device][level] = width * height * 2;
        }
        const auto& format = configs[device]->format;
        frame.meta.format[device][0] = std::to_underlying(format.formatLevel0);
        frame.meta.format[device][1] = std::to_underlying(format.formatLevel1);
        frame.meta.format[device][2] = std::to_underlying(format.formatLevel2);
        frame.meta.format[device][3] = std::to_underlying(format.formatLevel3);
        frame.status[device] = 1;
        frame.meta.frame[device] = frame_number;
        frame.meta.timestamp[device] = timestamp;
        frame.meta.deviceTimestamp[device] = static_cast<u32>(timestamp);
        frame.meta.exposureGain[device] = g_virtual_exposure_gain[device];
    }
    // The camera sits level.
    frame.meta.acceleration_y = 1.0f;
    frame.meta.vcounter = frame_number;

    // Older SDKs use a shorter structure, never write past what the caller declared.
    constexpr size_t header_size = offsetof(OrbisCameraFrameData, framePosition);
    const size_t size = std::min<size_t>(frame_data->sizeThis, sizeof(OrbisCameraFrameData));
    if (size > header_size) {
        std::memcpy(reinterpret_cast<u8*>(frame_data) + header_size,
                    reinterpret_cast<const u8*>(&frame) + header_size, size - header_size);
    }
    return ORBIS_OK;
}

} // namespace Libraries::Camera::Virtual
