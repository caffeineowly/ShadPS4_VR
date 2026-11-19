#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 Shadps4_vr contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Packs a Windows build of Shadps4_vr into dist/Shadps4_vr-win64-<version>.zip.
#   package.sh <build-dir>
set -euo pipefail
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build=$1
version=$(git -C "$repo" describe --always --dirty 2>/dev/null || echo unknown)
name=Shadps4_vr-win64-$version
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
out=$stage/$name
mkdir -p "$out/licenses"

cp "$build/shadps4.exe" "$out/"
# Any DLLs the build produced next to the executable (normally none: everything is static).
find "$build" -maxdepth 1 -name '*.dll' -exec cp {} "$out/" \;

cp "$repo/cross-win64/README-VR.txt" "$out/README-VR.txt"
cp "$repo/cross-win64/vr.json.example" "$out/vr.json.example"
cp "$repo/cross-win64/Start-VR.bat" "$out/Start-VR.bat"
cp "$repo/PROJECT.md" "$out/PROJECT.md" 2>/dev/null || true
cp "$repo/LICENSE" "$out/licenses/shadPS4-GPL-2.0-or-later.txt"
cp -r "$repo/LICENSES" "$out/licenses/shadPS4-LICENSES"
cp "$repo/externals/openxr-sdk/LICENSE" "$out/licenses/OpenXR-SDK-Apache-2.0.txt"
# Licenses of the statically linked third-party libraries.
for dir in "$repo"/externals/*/; do
    lib=$(basename "$dir")
    for f in LICENSE LICENSE.txt LICENSE.md COPYING COPYING.txt LICENCE NOTICE; do
        [ -f "$dir/$f" ] && cp "$dir/$f" "$out/licenses/$lib-$f"
    done
done

mkdir -p "$repo/dist"
rm -f "$repo/dist/$name.zip"
(cd "$stage" && zip -qr9 "$repo/dist/$name.zip" "$name")
ls -la "$repo/dist/$name.zip"
