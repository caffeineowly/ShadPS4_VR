// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/libraries/camera/camera.h"

/// A connected virtual headset brings a virtual PlayStation Camera along, because titles will not
/// start head tracking without one. It never touches a host webcam: tracking comes from the host
/// headset, so all the guest ever sees are blank frames arriving at the sensor's frame rate.
/// (From bigmak94/AstroQuest.)
namespace Libraries::Camera::Virtual {

/// A headset is connected: the camera is the virtual one.
bool IsActive();
/// A camera is attached: the virtual one, or a host webcam chosen in the settings.
bool IsCameraConnected();
void Start();
void Stop();
s32 GetFrameData(OrbisCameraFrameData* frame_data);
void SetExposureGain(OrbisCameraChannel channel, const OrbisCameraExposureGain* exposure_gain);

} // namespace Libraries::Camera::Virtual
