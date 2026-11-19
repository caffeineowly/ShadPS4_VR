// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <chrono>

#include "common/types.h"

/// How many frames the title has handed to the headset, for what has to keep in step with it
/// (the virtual PlayStation Camera). Reduced from bigmak94/AstroQuest's guest watchdog.
namespace Core::Vr::GuestFrames {

/// The title has handed over a frame.
void Note() noexcept;
/// Frames handed over so far.
u64 Count() noexcept;
/// Waits until more than `count` frames were handed over, or `timeout`. Returns the count.
u64 WaitFor(u64 count, std::chrono::milliseconds timeout);

} // namespace Core::Vr::GuestFrames
