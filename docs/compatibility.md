# Compatibility

PSVR titles tested with Shadps4_vr. Headset used so far: Windows Mixed Reality through SteamVR's
OpenXR runtime, on Windows.

| Title | Serial | Engine | Status | Frame path | Notes |
| --- | --- | --- | --- | --- | --- |
| Fate/Grand Order VR feat. Mash Kyrielight | CUSA09078 | Unity | Playable | `sceHmdReprojectionStartMultilayer` | Stereo, head, controller and hand tracking. Runs at half the headset's refresh (45 fps at 90 Hz); use `fov_percent` 110-120 in `vr.json` to hide the black edge when turning. View-dependent banding in some dark areas (under investigation). |
| CYBER DANGANRONPA VR THE CLASS TRIAL (demo) | CUSA07121 | Unreal Engine 4 | Playable | `sceHmdReprojectionStart` | Whole demo playable. Needs `"redzone_patches": true` in `custom_configs/CUSA07121.json` (otherwise crashes at an effect, eboot+0x19876af). One effect shows graphical glitches. 60 fps. |

## Status meanings

- **Playable**: can be played from start to finish (of what was tested) with minor issues.
- **In-game**: gets into the game, but crashes or has issues that stop play.
- **Menus**: reaches menus only.
- **Nothing**: no picture in the headset.

## Settings per game

Per-game shadPS4 settings go in `%APPDATA%\shadPS4\custom_configs\<SERIAL>.json` (or
`user\custom_configs\` next to the exe in portable mode), for example:

```json
{
  "General": {
    "redzone_patches": true
  }
}
```

VR settings (`vr.json`) are global: see `cross-win64/README-VR.txt`.

## What each engine needed

Useful when trying an untested title:

- **Unity**: frames through `sceHmdReprojectionStartMultilayer`; both eyes side by side in one
  texture, stored bottom-up (negative UV scale).
- **Unreal Engine 4**: frames through `sceHmdReprojectionStart` with both eyes side by side in one
  UNORM texture; `sceVrTrackerInit` and `sceVrTrackerGpuSubmit` with structure sizes of another SDK
  version; the HMD setup dialog's state machine; red-zone protection on Windows (functions using
  the red zone through the frame pointer).
- **ASTRO BOT's engine** (from AstroQuest, not retested here): frames through
  `sceHmdReprojectionStart`, one texture per eye.
