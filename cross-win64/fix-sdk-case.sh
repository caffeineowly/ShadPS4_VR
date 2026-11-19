#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 Shadps4_vr contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Windows headers are looked up case-insensitively on Windows; sources ask for <Shlobj.h> where
# the SDK has ShlObj.h. Adds a symlink for every spelling the sources use that the SDK lacks.
#   fix-sdk-case.sh <repo> <winsdk-dir>
set -eu
repo=$1; sdk=$2
dirs=("$sdk/crt/include" "$sdk/sdk/include/ucrt" "$sdk/sdk/include/um" "$sdk/sdk/include/shared" "$sdk/sdk/include/winrt")
added=0
while read -r name; do
    for d in "${dirs[@]}"; do
        [ -e "$d/$name" ] && continue 2
    done
    for d in "${dirs[@]}"; do
        match=$(find "$d" -maxdepth 1 -iname "$name" -print -quit 2>/dev/null || true)
        if [ -n "$match" ]; then
            ln -s "$(basename "$match")" "$d/$name"
            added=$((added+1))
            continue 2
        fi
    done
done < <(grep -rhoE '#\s*include\s*[<"][A-Za-z0-9_]+\.h[>"]' "$repo/src" "$repo/externals" 2>/dev/null |
         sed -E 's/.*[<"]([^>"]+)[>"]/\1/' | sort -u)
echo "SDK case fix: $added header spellings added"

# The same for import libraries: sources and CMake files name them as Shell32.lib, Ws2_32 and the
# like, xwin lays them out in lower case.
libdirs=("$sdk/crt/lib/x86_64" "$sdk/sdk/lib/um/x86_64" "$sdk/sdk/lib/ucrt/x86_64")
added=0
while read -r name; do
    for d in "${libdirs[@]}"; do
        [ -e "$d/$name" ] && continue 2
    done
    for d in "${libdirs[@]}"; do
        match=$(find "$d" -maxdepth 1 -iname "$name" -print -quit 2>/dev/null || true)
        if [ -n "$match" ]; then
            ln -s "$(basename "$match")" "$d/$name"
            added=$((added+1))
            continue 2
        fi
    done
done < <({ grep -rhoE '[A-Za-z0-9_]+\.[Ll][Ii][Bb]\b' "$repo/src" "$repo/externals" "$repo/CMakeLists.txt" \
              --include='*.cpp' --include='*.c' --include='*.h' --include='*.txt' --include='*.cmake' 2>/dev/null
          grep -rhoE 'target_link_libraries\([^)]*\)' "$repo/CMakeLists.txt" "$repo/externals" \
              --include='CMakeLists.txt' --include='*.cmake' 2>/dev/null |
              grep -oE '\b[A-Za-z][A-Za-z0-9_]*\b' | sed 's/$/.lib/'; } | sort -u)
echo "SDK case fix: $added library spellings added"
