# Repository analysis: PSVR → OpenXR for shadPS4

Snapshot date: 2026-11-19. All clones are shallow (`--depth 1`) under `upstream/`.

| Repo | Folder | Commit | Date |
| --- | --- | --- | --- |
| bigmak94/AstroQuest | `upstream/AstroQuest` | `a56f5b1405acddb9935bdd3c8430fb16013399e8` | 2026-11-14 |
| shadps4-emu/shadPS4 | `upstream/shadps4` | `741d9b75fd87104ae2f81e4443ffc5433828eebf` | 2026-11-18 |
| KhronosGroup/OpenXR-SDK | `upstream/OpenXR-SDK` | `24a1f2b92d212501a2b1b8d30ffd53abf55fcfa8` | 2026-10-16 |

## Key finding: the PC path is already in AstroQuest

Plans.md assumed AstroQuest's useful part is tied up with its ARM/Quest tooling. That is only
half right. `AstroQuest/shadps4-arm64-main/` is one emulator tree that builds **both**:

- the Quest 3 build (ARM64 + FEXCore + "Bachata" Android runtime), and
- a **Windows x86-64 PC VR build** that talks to any desktop OpenXR runtime (SteamVR, Virtual
  Desktop/VDXR, Pimax). It is reported working on Valve Index, Bigscreen Beyond and Pimax.

The OpenXR side sits behind a CMake option that already allows Linux:

```cmake
cmake_dependent_option(ENABLE_OPENXR "..." ON
    "WIN32 OR CMAKE_SYSTEM_NAME STREQUAL \"Linux\";NOT ENABLE_BACHATA_RUNTIME" OFF)
```

So the job is to **port the VR layer out of the fork onto current upstream shadPS4** and leave
the ARM64/FEX/Bachata/Quest parts behind. We don't need to write it from scratch.

## What upstream shadPS4 has (PSVR-related)

| Area | Path | State |
| --- | --- | --- |
| `libSceHmd` | `src/core/libraries/hmd/` (hmd, distortion, reprojection, setup dialog, vrservicedialog) | HLE entry points with parameter checks, logs "PSVR headsets are not supported yet"; ~122 stub logs. Reports no headset. |
| `libSceVrTracker` | `src/core/libraries/vr_tracker/` | Stubbed (~32 stub logs), no pose source. |
| `libSceAudio3d` | `src/core/libraries/audio3d/` | Present, OpenAL backend (larger and newer than the fork's). |
| Camera | `src/core/libraries/camera/` | Newer, split into several files (device, calibration, audio...). |
| OpenXR | none | No OpenXR dependency, no VR output path in the Vulkan renderer. |

Missing in upstream: a pose source, any OpenXR session/swapchain, a way to send the game's
stereo frames to a headset, tracked controllers through the PS camera / `sceVrTracker`, and
headset audio routing.

## What AstroQuest has (relevant to PC VR)

Lines are approximate (`wc -l`). All of it is GPL-2.0-or-later with "shadPS4 Emulator Project"
SPDX headers.

### New VR core: `src/core/vr/` (port these)

| File | Lines | Role |
| --- | --- | --- |
| `vr_runtime.{h,cpp}` | 365 + 1071 | Shared pose/maths types (Vec3, Quat, Pose), headset state, frame hand-off. The hub everything else calls. |
| `openxr_host.{h,cpp}` | 102 + 2955 | **The OpenXR backend.** Instance/session, Vulkan extension negotiation, swapchain, head and hand poses, frame submit, headset audio device names. Only built with `ENABLE_OPENXR`. |
| `openxr_view.h` | 75 | Projection/FOV maths (the canted views become parallel projections). |
| `headset_fov_cache.h` | 40 | Saves the FOV per runtime and headset. |
| `late_frame_wait.h`, `mirror_limiter.h`, `spectator_view.h` | ~210 | Frame pacing, desktop mirror and spectator view. |
| `vr_host_link.{h,cpp}`, `vr_protocol.h` | ~520 | Socket link to the **Quest** host app. Quest only, but `vr_runtime` references it. Stub it or keep it compiled but unused. |

### Changed HLE libraries (port the VR parts)

| File | Notes |
| --- | --- |
| `libraries/hmd/hmd.{h,cpp}`, `hmd_reprojection.cpp`, `hmd_setup_dialog.*` | Report a connected headset, feed poses from `vr_runtime`, real reprojection. |
| `libraries/vr_tracker/vr_tracker.{h,cpp}` (784 lines) | Pad/headset tracking from OpenXR poses. |
| `libraries/camera/camera.cpp` | Fakes the PS Camera's view of the light bar from hand or controller tracking. |
| `libraries/audio3d/*` + `audio3d_spatializer.*` | 3D sound engine and spatializer. |
| `libraries/pad/pad.cpp` | Uses VR controllers when no gamepad is connected, plus gamepad placement. |
| `libraries/videoout/driver.{h,cpp}`, `video_out.{h,cpp}` | Frame hand-off to the headset, pacing to the headset refresh. |
| `libraries/audio/sdl_audio_{in,out}.cpp`, `audioout.cpp` | Route audio to the headset's speakers and microphone. |
| `libraries/system/systemservice.cpp`, `gnmdriver.cpp` | Small hooks (VR mode flags, frame submit). |

### Renderer (port the VR parts)

| File | Notes |
| --- | --- |
| `renderer_vulkan/vk_vr_exporter.{h,cpp}` (530 lines) | **New.** Copies the game's eye buffers into the OpenXR swapchain images. |
| `vk_instance.cpp`, `vk_platform.cpp` | Adds the instance/device extensions OpenXR asks for, picks the GPU the headset is on. |
| `vk_presenter.{h,cpp}`, `vk_swapchain.cpp`, `vk_scheduler.h` | Presents to the headset and the desktop mirror. |
| `vk_rasterizer.cpp`, `texture_cache/image.cpp`, `vk_title_upscale.cpp` | Render-resolution scaling. Mostly Astro-Bot-specific. |

### Startup and input glue

`src/emulator.cpp`, `src/main.cpp`, `src/sdl_window.cpp`, `src/input/controller.cpp`,
`src/core/module.cpp`, `src/core/file_sys/fs.cpp`: start `OpenXrHost::Connect()` before Vulkan
init, add CLI/env switches, place the controller.

### Title-specific code (treat separately)

`src/core/known_title.{h,cpp}`, `known_title_builds.h`, `known_title_scenes.h` (~2.6k lines)
patch **ASTRO BOT Rescue Mission** (CUSA12392 / CUSA12431, v1.00 and v1.04) in memory: timestep
fix, resolution, recentre, physics step. They only act when those exact builds are detected.
This is useful but **not** generic PSVR emulation. It could later become a shadPS4 game patch.
It contains short byte patterns and offsets from the game binary (see PROJECT.md).

### Leave behind (out of scope)

- `src/core/fex/`, `src/core/guest_cpu/`, `fiber_fex.cpp`, `stack.S`, ARM64 bits: FEXCore
  x86→ARM translation.
- `src/platform/bachata/`, `runtime/`, `bachata_audio_*`: the Android container runtime.
  **Caveat:** in the fork's CMake `runtime_client.cpp` is always built because video/audio
  code references it. When porting, remove those references or keep a no-op stub.
- `quest-host/` (the Android app), most of `tools/` (Quest test scripts), `pc-vr/launch.ps1`
  (a Windows launcher; could be used as a reference for a Linux launcher script).
- Fork-only extras unrelated to VR: patch_* system, crash_reporter, fios2, json lib, etc.

## How far the fork has drifted

- About **479** files under `src/` differ between the trees. Upstream also has files the fork
  lacks (split camera files, keyboard lib, `file_sys/backends`, `ifile`, `slot_vector`,
  `rif`, ...), so the fork is based on an **older** upstream and has been heavily changed.
- Lines changed in the main hook files (this mixes VR changes with general drift):
  vk_rasterizer 1710, vk_presenter 1425, camera 1126, audio3d 661, gnmdriver 609,
  driver.cpp 628, emulator.cpp 596, vk_instance 423, vr_tracker 280, pad 231, hmd 91.
- **Doing a git merge is not practical.** Port by hand: copy `core/vr/*` and
  `vk_vr_exporter.*` as-is, then re-apply only the VR hooks file by file onto upstream's
  current versions of hmd, vr_tracker, videoout, presenter, instance, emulator, pad, camera
  and audio.

## Proposed merge plan (step 4)

1. `Shadps4_vr/` = a fresh copy of upstream shadPS4 at the commit above, with its own local git
   history (first commit = the unmodified upstream, so every later VR change shows as a diff).
2. Add `externals/openxr-sdk` (OpenXR-SDK, loader only) and the fork's `ENABLE_OPENXR` CMake
   wiring.
3. Bring over `src/core/vr/` and `vk_vr_exporter.*`. Stub `vr_host_link` (no Quest).
4. Re-apply the hooks in order, building after each one:
   Vulkan instance/device extensions → presenter/exporter frame output → videoout pacing →
   hmd (headset present + poses) → vr_tracker → pad/VR controllers → camera → audio routing.
5. Leave out `known_title*` at first. Add it later behind its own CMake option
   (`ENABLE_TITLE_FIXES`) if you want Astro Bot to be playable.
6. Linux x86-64 first (the fork has only been tested with Windows OpenXR runtimes. On Linux the
   runtimes are SteamVR, Monado or WiVRn, all untested with this code). Windows second.

## Risks and open questions

- The OpenXR path has only been tested by its author against Windows runtimes. On Linux,
  Vulkan interop with SteamVR/Monado may need fixes.
- Upstream shadPS4 changes fast, so the port will need rebasing from time to time.
- Without `known_title`, other PSVR games may start but run in slow motion or at the wrong
  resolution. That depends on the game.
- The `vr_host_link` and Bachata references in common code need care so nothing Android-only
  gets pulled in.
