@echo off
rem SPDX-FileCopyrightText: Copyright 2026 Shadps4_vr contributors
rem SPDX-License-Identifier: GPL-2.0-or-later
rem Starts a PSVR game in the PC's OpenXR headset.
rem Usage: drag a game's eboot.bin onto this file, or: Start-VR.bat "C:\Games\CUSA00000\eboot.bin"
rem The active OpenXR runtime is used (SteamVR, Meta/Oculus, Virtual Desktop, Pimax, WMR...).
rem Start the headset's software first.
setlocal
cd /d "%~dp0"
if "%~1"=="" (
    echo Drag a game's eboot.bin onto Start-VR.bat, or pass its path.
    pause
    exit /b 1
)
rem Plug the virtual PSVR in even for titles that only support (not require) it.
set SHADPS4_VR=1
shadps4.exe "%~1"
if errorlevel 1 pause
