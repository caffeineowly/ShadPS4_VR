# Shadps4_vr

Experimental: [shadPS4](https://github.com/shadps4-emu/shadPS4) with the PlayStation VR emulation
from [AstroQuest](https://github.com/bigmak94/AstroQuest) ported onto it, so PSVR titles can be
played in any PC headset through its OpenXR runtime (SteamVR, Meta/Oculus, Virtual Desktop,
Pimax, WMR, ...).

> **Status: builds, untested on a headset.** Local repository only; nothing has been published.
> **Do not publish until every item under "Unknown or to investigate" is resolved.**

## Layout of the repository

| Path | What |
| --- | --- |
| *(everything upstream)* | shadPS4 at `741d9b7` (2026-11-18), history kept; VR work is on branch `vr` |
| `src/core/vr/` | VR runtime (poses, seat, pad placement, frame ids), **OpenXR host** (`openxr_host.cpp`), FOV/pacing helpers. From AstroQuest |
| `src/core/vr/guest_frames.*` | Counter of frames handed to the headset (reduced from AstroQuest's guest watchdog) |
| `src/core/libraries/hmd/`, `vr_tracker/` | `libSceHmd` / `libSceVrTracker`: headset present, poses, reprojection. From AstroQuest (3-way merged) |
| `src/core/libraries/camera/camera_virtual.*` | Virtual PS Camera while a headset is connected (titles need one to track). From AstroQuest, moved into its own file |
| `src/core/libraries/videoout/` | Headset frame submission, social-screen port, present thread paced to the headset. From AstroQuest |
| `src/video_core/renderer_vulkan/vk_vr_exporter.*` | Hands eye images to the OpenXR swapchain. From AstroQuest |
| `src/video_core/renderer_vulkan/vk_presenter.*` | `PrepareHmdFrame` / `DeliverHmdFrame` (eyes side by side, desktop mirror/spectator view) |
| `src/video_core/renderer_vulkan/host_passes/pp_pass.*`, `host_shaders/post_process.frag` | Post-process pass extended with regions, sharpening, linear output (AstroQuest) on top of upstream's sRGB-input support |
| `src/video_core/renderer_vulkan/vk_frame_stats.h` | No-op stand-in for AstroQuest's frame statistics |
| `src/input/` | Headset controllers play as the PS4 pad; PS-button recentre/pad placement; helpers (`pad_source.h`, `pad_gestures.h`, ...) from AstroQuest |
| `externals/openxr-sdk` | Khronos OpenXR-SDK 1.1.65 (submodule), loader linked statically |
| `cross-win64/` | Cross-compiling for Windows x64 on Linux, packaging, launcher and README for the zip |
| `dist/` | Built zips (not committed) |

The analysis this port is based on (what each upstream repo has, the fork's base commit
`7bb1f9d`, the 3-way merge approach) is in `REPO_ANALYSIS.md`.

## What was ported, and what was not

Ported (everything needed for the PC OpenXR path):
OpenXR host and VR runtime; Vulkan instance/device/queue hooks for the runtime; headset frame
path (videoout → presenter → exporter → OpenXR); `sceHmd*` and `sceVrTracker*`; virtual PS
Camera; VR_VIEW output capability and social-screen video port; headset controllers as the pad,
gamepad gyro/position for tracking, PS-button recentre and pad placement; headset audio
devices, AUX port muted in VR; "headset worn" system status; high-resolution present pacing on
Windows.

Not ported (deliberately):

- `known_title*` - ASTRO BOT Rescue Mission specific memory patches (timestep, resolution,
  physics). See U3. Without them that title may run in slow motion below 60 fps.
- Quest-only parts: FEXCore ARM64 CPU backend, Bachata Android runtime, `quest-host/` app.
  (`vr_host_link` is present but compiles to an inactive stub on Windows.)
- AstroQuest's debugging aids: frame statistics, draw tracing, eye scan, render-target dumps,
  Bachata traces; title-specific upscaling (TitleUpscale), foveation; its patch system.
- AstroQuest's flip-token/IRQ ordering change in `sceVideoOutSubmitEopFlip` (its
  `FlipLabelTracker` is included but inactive).
- 3D audio spatializer and audio3d changes (upstream's own audio3d is kept).

## Known licenses

| Source | License | Evidence | Use here |
| --- | --- | --- | --- |
| shadPS4 (shadps4-emu/shadPS4 @ `741d9b7`) | **GPL-2.0-or-later** | `LICENSE`, SPDX headers, `REUSE.toml` | Base of the repo |
| AstroQuest (bigmak94/AstroQuest @ `a56f5b1`) | **GPL-2.0-or-later** | `LICENSE`, `THIRD-PARTY-NOTICES.md`; every ported file has `SPDX-License-Identifier: GPL-2.0-or-later` | Ported VR code |
| shadps4-arm64 (zenithblue-oss), inside AstroQuest | GPL-2.0-or-later | AstroQuest notices | Only via the ported files |
| Khronos OpenXR-SDK 1.1.65 (`externals/openxr-sdk`) | **Apache-2.0** | `LICENSE`, `LICENSES/` | OpenXR loader, linked statically (note 1) |
| Third-party submodules (SDL3, fmt, Boost, glslang, sirit, VMA, zlib-ng, xxHash, FFmpeg core, Dear ImGui, OpenAL-soft, spdlog, libressl, freetype, fdk-aac, protobuf, abseil, ...) | their own (zlib, MIT, BSL-1.0, BSD, Apache-2.0, LGPL, FDK-AAC license) | each folder under `externals/` | Same set upstream shadPS4 ships; their license files are copied into the zip's `licenses/` |
| New files of this repo (`cross-win64/*`, `guest_frames.*`, `camera_virtual.*`, `vk_frame_stats.h`) | GPL-2.0-or-later | SPDX headers | |

**Note 1 - Apache-2.0 with GPL-2.0-or-later.** Apache-2.0 is not compatible with GPL-2.0-*only*,
but is with GPL-3.0. Since shadPS4 and AstroQuest are GPL-2.0-*or-later*, a binary containing
the OpenXR loader is in effect distributed under GPL-3.0 terms (AstroQuest's PC build does the
same). The zip contains the OpenXR `LICENSE` and says so in `README-VR.txt`.

**Note 2 - Build-time only, not distributed:** the MSVC CRT headers/libs and Windows SDK are
downloaded by [xwin](https://github.com/Jake-Shadle/xwin) (MIT/Apache-2.0) into `/tmp` when
building, under Microsoft's license terms (accepted by the person building). Nothing from them is
committed. The zip's `shadps4.exe` links dynamically against the Visual C++ runtime
(`MSVCP140.dll`, `VCRUNTIME140*.dll`), which is **not** included: users install Microsoft's
redistributable. compiler-rt builtins (Apache-2.0 WITH LLVM-exception) are built from LLVM
sources and linked into the exe.

## Unknown or to investigate

| # | Item | Why it is flagged | Action needed |
| --- | --- | --- | --- |
| U1 | `AstroQuest/.../network/net_obj.{h,cpp}` | No license header; both files are empty | Not copied. Nothing to do. |
| U2 | 83 AstroQuest files in `quest-host/`, `tools/`, `pc-vr/` without SPDX headers | Only the repo-level GPL-2.0 `LICENSE` covers them | None were copied. `Start-VR.bat`/`README-VR.txt` here are new, not taken from `pc-vr/`. |
| U3 | `known_title*` (ASTRO BOT fixes) | GPL-2.0-or-later, but contains short byte patterns and memory offsets taken from the commercial game binary | **Not included.** Decide before ever adding it. |
| U4 | Copyright lines of AstroQuest's new files say "shadPS4 Emulator Project" although written by bigmak94 (and contributors) | Attribution may be inaccurate | Headers kept as they are; credited below. Optionally ask the author. |
| U5 | Code in `openxr_host.cpp`, `vk_presenter.cpp`, `driver.cpp` marked "After evertec82/AstroQuest" / "from elliotttate/AstroQuest" | Contributions from other AstroQuest forks, merged upstream into AstroQuest under its GPL-2.0-or-later license | Credited below; check those forks' licenses if you want certainty. |
| U6 | AstroQuest's `LICENSES/Vortek-LGPL-2.1.txt`, `BachataXServer-LGPL-2.1.txt`, `libdeflate-NOTICE.txt` | Android/Bachata runtime | Not taken. |
| U7 | PkgTool / LibOrbisPkg (LGPL-3.0), used by AstroQuest's launcher | Separate download | Not taken. |

## Credits

- shadPS4 Emulator Project - the emulator.
- bigmak94 (AstroQuest) - PSVR headset, tracking, reprojection, camera, audio and OpenXR host.
- evertec82 and elliotttate (AstroQuest forks) - late-frame wait, window rate limit and present
  pacing parts of the above, as credited in the code.
- Clodo76 - AstroQuest's v1.04 offsets (not ported, credited in AstroQuest).
- zenithblue-oss (shadps4-arm64) - the fork AstroQuest builds on.
- The Khronos Group - OpenXR SDK.

## Building (Windows x64, cross-compiled on Linux)

Tools: `clang` (with `clang-cl`), `lld`, `llvm` (llvm-lib, llvm-rc, llvm-mt), `cmake`, `ninja`,
`git`, `curl`, `zip`. On Fedora: `sudo dnf install clang lld llvm cmake ninja-build zip`.

```sh
cross-win64/build.sh            # add --clean for a fresh build, --jobs N to limit parallelism
# -> dist/Shadps4_vr-win64.zip
```

On first run it downloads into `WORK` (default `/tmp/shadps4_vr-win64`, RAM-backed on Fedora so
nothing large lands on disk): xwin and the MSVC CRT + Windows SDK (~630 MB), compiler-rt builtins
sources (~30 MB, built to `clang_rt.builtins-x86_64.lib`), and builds host tools needed during
the build (glslang, protoc, the ImGui font embedder). The build directory takes ~3 GB.
Peak memory is high with many jobs; 8 jobs fit in 18 GB.

Cross-compile fixes this needed (all in this repo):
`-DHAVE_ENDIAN_H=OFF` for libressl; `IMGUI_FONT_EMBED_EXECUTABLE`, `HOST_GLSLANG_COMPILER`,
`HOST_PROTOC` CMake options so build-time tools run on the build machine;
`cross-win64/fix-sdk-case.sh` for Windows header/library names on a case-sensitive file system.

Transfer: `scp dist/Shadps4_vr-win64.zip user@windows-pc:` and unzip; see `README-VR.txt`
inside for running it.
