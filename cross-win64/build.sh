#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 Shadps4_vr contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Cross-compiles Shadps4_vr for Windows x64 on Linux and packs it into a zip.
#
#   cross-win64/build.sh [--clean] [--jobs N]
#
# Needs: clang (clang-cl), lld (lld-link), llvm (llvm-lib, llvm-rc, llvm-mt), cmake, ninja,
# git, curl, zip. Downloads on first run, into WORK (default /tmp/shadps4_vr-win64, which is
# RAM-backed on Fedora, so nothing large lands on disk):
#   - xwin, then the MSVC CRT + Windows SDK through it (~630 MB; you accept Microsoft's license
#     terms by running this: https://go.microsoft.com/fwlink/?LinkId=2086102)
#   - compiler-rt builtins sources matching your clang, built to clang_rt.builtins-x86_64.lib
# Output: dist/Shadps4_vr-win64.zip in the repository.
set -euo pipefail

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
WORK=${WORK:-/tmp/shadps4_vr-win64}
JOBS=${JOBS:-$(( $(nproc) > 8 ? 8 : $(nproc) ))}
XWIN_VERSION=0.10.0
clean=0
while [ $# -gt 0 ]; do
    case "$1" in
    --clean) clean=1 ;;
    --jobs) JOBS=$2; shift ;;
    *) echo "unknown argument $1" >&2; exit 2 ;;
    esac
    shift
done

for tool in clang++ clang-cl lld-link llvm-lib llvm-rc llvm-mt cmake ninja git curl zip; do
    command -v "$tool" >/dev/null || { echo "missing tool: $tool" >&2; exit 1; }
done
mkdir -p "$WORK"

# 1. MSVC CRT + Windows SDK
if [ ! -d "$WORK/winsdk/crt/include" ]; then
    if [ ! -x "$WORK/xwin" ]; then
        curl -fsSL "https://github.com/Jake-Shadle/xwin/releases/download/$XWIN_VERSION/xwin-$XWIN_VERSION-x86_64-unknown-linux-musl.tar.gz" |
            tar xz -C "$WORK"
        mv "$WORK/xwin-$XWIN_VERSION-x86_64-unknown-linux-musl/xwin" "$WORK/xwin"
        rm -rf "$WORK/xwin-$XWIN_VERSION-x86_64-unknown-linux-musl"
    fi
    "$WORK/xwin" --accept-license --arch x86_64 --cache-dir "$WORK/xwin-cache" splat --output "$WORK/winsdk"
    rm -rf "$WORK/xwin-cache"
fi

"$repo/cross-win64/fix-sdk-case.sh" "$repo" "$WORK/winsdk"

# 2. clang_rt.builtins-x86_64.lib
if [ ! -f "$WORK/crt/clang_rt.builtins-x86_64.lib" ]; then
    mkdir -p "$WORK/crt"
    "$repo/cross-win64/build-builtins.sh" "$WORK/crt" "$WORK/winsdk"
fi

# 3. Submodules
git -C "$repo" submodule update --init --recursive --depth 1

# 3b. Tools that run during the build are made for this machine
if [ ! -x "$WORK/host-tools/binary_to_compressed_c" ]; then
    mkdir -p "$WORK/host-tools"
    clang++ -O2 -o "$WORK/host-tools/binary_to_compressed_c" \
        "$repo/externals/imgui/misc/fonts/binary_to_compressed_c.cpp"
fi

if [ ! -x "$WORK/host-tools/glslang/bin/glslang" ]; then
    cmake -S "$repo/externals/glslang" -B "$WORK/host-tools/glslang-build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
        -DENABLE_OPT=OFF -DGLSLANG_TESTS=OFF -DENABLE_GLSLANG_BINARIES=ON \
        -DCMAKE_INSTALL_PREFIX="$WORK/host-tools/glslang"
    ninja -C "$WORK/host-tools/glslang-build" -j "$JOBS" install
    rm -rf "$WORK/host-tools/glslang-build"
fi

if [ ! -x "$WORK/host-tools/protoc" ]; then
    cmake -S "$repo/cross-win64/host-protoc" -B "$WORK/host-tools/protobuf-build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
    ninja -C "$WORK/host-tools/protobuf-build" -j "$JOBS" protoc
    cp -L "$WORK/host-tools/protobuf-build/protobuf/protoc" "$WORK/host-tools/protoc"
    rm -rf "$WORK/host-tools/protobuf-build"
fi

# 4. Configure + build
build=$WORK/build
[ $clean = 1 ] && rm -rf "$build"
export XWIN_DIR=$WORK/winsdk CLANG_RT_DIR=$WORK/crt
cmake -S "$repo" -B "$build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$repo/cross-win64/toolchain-win64.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_OPENXR=ON \
    -DENABLE_UPDATER=OFF \
    -DENABLE_DISCORD_RPC=OFF \
    -DHAVE_ENDIAN_H=OFF \
    -DIMGUI_FONT_EMBED_EXECUTABLE="$WORK/host-tools/binary_to_compressed_c" \
    -DHOST_GLSLANG_COMPILER="$WORK/host-tools/glslang/bin/glslang" \
    -DHOST_PROTOC="$WORK/host-tools/protoc"
ninja -C "$build" -j "$JOBS" shadps4

# 5. Package
"$repo/cross-win64/package.sh" "$build"
