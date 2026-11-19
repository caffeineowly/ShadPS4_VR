// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <chrono>

#include "common/types.h"

namespace Vulkan {

/// The interface of AstroQuest's per-frame statistics, which the ported VR frame path reports
/// to. The statistics themselves (spread through AstroQuest's scheduler) are not ported: every
/// report is dropped here.
struct FrameStats {
    enum class Stage : u32 {
        Submitting,
        CatchUp,
        Translate,
        Queued,
        GpuWait,
        Latency,
        Count,
    };
    static void Time(Stage, std::chrono::nanoseconds) {}
    static void EndFrame() {}
    static void NoteHmdFrame() {}
    static void NoteGpuThreadFrame(std::chrono::nanoseconds) {}
    static void GpuLook(bool) {}
    static std::chrono::steady_clock::time_point TakeGuestSubmit() {
        return {};
    }
    static std::chrono::nanoseconds ThreadTime() {
        return {};
    }
    /// A/B switch for single optimizations in AstroQuest's tests: nothing is switched off.
    static bool AbOff(const char* = "") {
        return false;
    }
};

} // namespace Vulkan
