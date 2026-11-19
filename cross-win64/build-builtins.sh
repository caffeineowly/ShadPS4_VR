#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 Shadps4_vr contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Builds clang_rt.builtins-x86_64.lib (compiler-rt builtins for Windows x64) with clang-cl.
# Distros ship clang-cl but not this Windows runtime library, which shadPS4 links.
#   build-builtins.sh <work-dir> <winsdk-dir>
set -eu
work=$1; sdk=$2
ver=$(clang --version | sed -n 's/.*clang version \([0-9.]*\).*/\1/p')
src=$work/llvm/compiler-rt/lib/builtins
if [ ! -d "$src" ]; then
    git clone -q --depth 1 --filter=blob:none --sparse --branch "llvmorg-$ver" \
        https://github.com/llvm/llvm-project "$work/llvm"
    git -C "$work/llvm" sparse-checkout set compiler-rt/lib/builtins
fi
out=$work/builtins-obj; mkdir -p "$out"; rm -f "$out"/*.obj
inc="/imsvc $sdk/crt/include /imsvc $sdk/sdk/include/ucrt /imsvc $sdk/sdk/include/um /imsvc $sdk/sdk/include/shared"
n=0; skipped=0
for f in "$src"/*.c "$src"/x86_64/*.c; do
    b=$(basename "$f" .c)
    # x86_64/ versions replace the generic ones of the same name
    [ "$(dirname "$f")" = "$src" ] && [ -f "$src/x86_64/$b.c" ] && continue
    if clang-cl --target=x86_64-pc-windows-msvc /nologo /c /O2 /MT $inc -fms-compatibility \
        -Wno-everything "$f" "/Fo$out/$b.obj" >/dev/null 2>&1; then n=$((n+1)); else skipped=$((skipped+1)); fi
done
llvm-lib /nologo "/out:$work/clang_rt.builtins-x86_64.lib" "$out"/*.obj
echo "built $work/clang_rt.builtins-x86_64.lib ($n objects, $skipped not for this target)"
