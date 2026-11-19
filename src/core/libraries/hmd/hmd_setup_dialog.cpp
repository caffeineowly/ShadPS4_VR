// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <atomic>

#include "common/assert.h"
#include "common/logging/log.h"
#include "core/libraries/error_codes.h"
#include "core/libraries/hmd/hmd_setup_dialog.h"
#include "core/libraries/libs.h"
#include "core/vr/vr_runtime.h"

namespace Libraries::HmdSetupDialog {

using Libraries::CommonDialog::Status;

// The dialog that asks the player to connect and put on the headset. The virtual headset is
// always connected, so an opened dialog finishes at once with OK; it goes through the states a
// common dialog goes through, which titles wait for.
static std::atomic<Status> g_status{Status::NONE};

s32 PS4_SYSV_ABI sceHmdSetupDialogInitialize() {
    LOG_INFO(Lib_HmdSetupDialog, "called");
    if (g_status.load() != Status::NONE) {
        return static_cast<s32>(Libraries::CommonDialog::Error::ALREADY_INITIALIZED);
    }
    g_status = Status::INITIALIZED;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceHmdSetupDialogClose() {
    LOG_INFO(Lib_HmdSetupDialog, "called");
    if (g_status.load() != Status::RUNNING) {
        return static_cast<s32>(Libraries::CommonDialog::Error::NOT_RUNNING);
    }
    g_status = Status::FINISHED;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceHmdSetupDialogOpen(const OrbisHmdSetupDialogParam* param) {
    LOG_INFO(Lib_HmdSetupDialog, "called");
    const Status status = g_status.load();
    if (status == Status::NONE) {
        return static_cast<s32>(Libraries::CommonDialog::Error::NOT_INITIALIZED);
    }
    if (status == Status::RUNNING) {
        return static_cast<s32>(Libraries::CommonDialog::Error::BUSY);
    }
    // Without a headset the dialog would wait for one; the player is taken to cancel it then
    // (sceHmdSetupDialogGetResult). Either way it is over right away.
    g_status = Status::FINISHED;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceHmdSetupDialogGetResult(OrbisHmdSetupDialogResult* result) {
    LOG_DEBUG(Lib_HmdSetupDialog, "called");
    if (result == nullptr) {
        return static_cast<s32>(Libraries::CommonDialog::Error::ARG_NULL);
    }
    if (g_status.load() != Status::FINISHED) {
        return static_cast<s32>(Libraries::CommonDialog::Error::NOT_FINISHED);
    }
    // Result::OK means a headset was connected. Without one, simulate the user pressing circle
    // to cancel the dialog.
    result->result = Core::Vr::Runtime::Instance().IsHeadsetConnected()
                         ? Libraries::CommonDialog::Result::OK
                         : Libraries::CommonDialog::Result::USER_CANCELED;
    return ORBIS_OK;
}

Status PS4_SYSV_ABI sceHmdSetupDialogUpdateStatus() {
    LOG_TRACE(Lib_HmdSetupDialog, "called");
    return g_status.load();
}

Status PS4_SYSV_ABI sceHmdSetupDialogGetStatus() {
    LOG_TRACE(Lib_HmdSetupDialog, "called");
    return g_status.load();
}

s32 PS4_SYSV_ABI sceHmdSetupDialogTerminate() {
    LOG_DEBUG(Lib_HmdSetupDialog, "called");
    if (g_status.load() == Status::NONE) {
        return static_cast<s32>(Libraries::CommonDialog::Error::NOT_INITIALIZED);
    }
    g_status = Status::NONE;
    return ORBIS_OK;
}

void RegisterLib(Core::Loader::SymbolsResolver* sym) {
    LIB_FUNCTION("nmHzU4Gh0xs", "libSceHmdSetupDialog", 1, "libSceHmdSetupDialog",
                 sceHmdSetupDialogClose);
    LIB_FUNCTION("6lVRHMV5LY0", "libSceHmdSetupDialog", 1, "libSceHmdSetupDialog",
                 sceHmdSetupDialogGetResult);
    LIB_FUNCTION("J9eBpW1udl4", "libSceHmdSetupDialog", 1, "libSceHmdSetupDialog",
                 sceHmdSetupDialogGetStatus);
    LIB_FUNCTION("NB1Y2kA2jCY", "libSceHmdSetupDialog", 1, "libSceHmdSetupDialog",
                 sceHmdSetupDialogInitialize);
    LIB_FUNCTION("NNgiV4T+akU", "libSceHmdSetupDialog", 1, "libSceHmdSetupDialog",
                 sceHmdSetupDialogOpen);
    LIB_FUNCTION("+z4OJmFreZc", "libSceHmdSetupDialog", 1, "libSceHmdSetupDialog",
                 sceHmdSetupDialogTerminate);
    LIB_FUNCTION("Ud7j3+RDIBg", "libSceHmdSetupDialog", 1, "libSceHmdSetupDialog",
                 sceHmdSetupDialogUpdateStatus);
};

} // namespace Libraries::HmdSetupDialog