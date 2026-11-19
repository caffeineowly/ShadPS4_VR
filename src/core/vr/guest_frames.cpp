// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <condition_variable>
#include <mutex>

#include "core/vr/guest_frames.h"

namespace Core::Vr::GuestFrames {

namespace {
std::mutex frame_mutex;
std::condition_variable frame_signal;
u64 frame_count = 0;
} // namespace

void Note() noexcept {
    {
        std::scoped_lock lock{frame_mutex};
        ++frame_count;
    }
    frame_signal.notify_all();
}

u64 Count() noexcept {
    std::scoped_lock lock{frame_mutex};
    return frame_count;
}

u64 WaitFor(u64 count, std::chrono::milliseconds timeout) {
    std::unique_lock lock{frame_mutex};
    frame_signal.wait_for(lock, timeout, [count] { return frame_count > count; });
    return frame_count;
}

} // namespace Core::Vr::GuestFrames
