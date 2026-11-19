Shadps4_vr - shadPS4 with PlayStation VR emulation through OpenXR (Windows x64)
=================================================================================

EXPERIMENTAL. This is upstream shadPS4 with the PSVR emulation from AstroQuest
(bigmak94/AstroQuest) ported onto it. Built to run PSVR titles in any PC headset
that has an OpenXR runtime: SteamVR (Index, Vive, Bigscreen, ...), Meta Quest
Link / Air Link, Virtual Desktop (VDXR), Pimax, Windows Mixed Reality, etc.

No game files, firmware or keys are included. Use dumps of games you own.

Requirements
------------
- Windows 10/11 64-bit, a GPU with Vulkan 1.3
- Microsoft Visual C++ Redistributable 2015-2022 (x64):
  https://aka.ms/vs/17/release/vc_redist.x64.exe
- An OpenXR runtime set as the active one (e.g. SteamVR: Settings > OpenXR >
  "Set SteamVR as OpenXR Runtime"; Meta: Oculus app > Settings > General >
  OpenXR Runtime; Virtual Desktop: Streamer > Options > OpenXR runtime VDXR).
- A controller: DualSense / DualShock 4 on the PC works best (gyro, touchpad,
  light bar). Without one, the headset's own controllers play.

Running
-------
1. Start your headset's software (SteamVR / Oculus / Virtual Desktop ...).
2. Drag the game's eboot.bin onto Start-VR.bat (or run:
   shadps4.exe "C:\path\to\CUSAxxxxx\eboot.bin").
   Start-VR.bat sets SHADPS4_VR=1, which connects the virtual PSVR even for
   games that only *support* PSVR. Without it, the headset is only connected
   for games that *require* PSVR (auto mode).
3. Recenter: press and release the PS button.
   Hold PS + D-pad/L1/R1 to move where an untracked gamepad is held in the game,
   PS + Triangle to switch to the default spot, PS + Square to "blow" into the mic.

A "user" folder next to shadps4.exe makes the install portable (settings, saves,
logs in it); otherwise they go to %APPDATA%\shadPS4.

Configuration (optional)
------------------------
vr.json in the user folder (see vr.json.example):
  "headset": "auto" | "on" | "off"
  "refresh_rate": 60..120 (what the game is told the PSVR refreshes at)
  "ipd_mm": interpupillary distance
  "pad_anchor": "head" | "seat"   where an untracked gamepad is placed

Environment variables (set before starting shadps4.exe):
  SHADPS4_VR=1/0               force the virtual headset on / off
  SHADPS4_VR_FOV=<50..120>     % of the PSVR field of view the game renders
  SHADPS4_VR_DESKTOP_VIEW=stereo|left|combined   what the desktop window shows
  SHADPS4_VR_WINDOW_FPS=<n>    limit the desktop mirror (default 60, 0 = never)
  SHADPS4_XR_HANDS=0           don't use hand tracking to place the gamepad

Logs: user\log\shad_log.txt (look for lines with "Core.Vr").

Known limits
------------
- Only the PSVR path from AstroQuest is ported. AstroQuest's game-specific
  fixes for ASTRO BOT Rescue Mission (timestep / resolution) are NOT included,
  so that game may run in slow motion when frames take longer than 1/60 s.
- PlayStation Move controllers are not emulated.
- Untested on most headsets/runtimes; please report what works.

Licenses
--------
shadPS4 and the AstroQuest code: GPL-2.0-or-later. Khronos OpenXR loader:
Apache-2.0 (statically linked; the combined program is therefore distributed
under GPL-3.0 terms). Third-party library licenses are in licenses\.
See PROJECT.md for details and credits.
Source: the Shadps4_vr git repository this build was made from.
