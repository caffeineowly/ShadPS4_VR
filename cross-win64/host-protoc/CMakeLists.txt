# SPDX-FileCopyrightText: Copyright 2026 Shadps4_vr contributors
# SPDX-License-Identifier: GPL-2.0-or-later

# Builds protoc for the build machine from the repository's own abseil-cpp and protobuf, so the
# Windows cross-build can generate its protobuf sources.
cmake_minimum_required(VERSION 3.22)
project(host_protoc C CXX)
set(CMAKE_CXX_STANDARD 17)
set(EXTERNALS "${CMAKE_CURRENT_SOURCE_DIR}/../../externals")
set(ABSL_PROPAGATE_CXX_STD ON)
add_subdirectory("${EXTERNALS}/abseil-cpp" abseil-cpp EXCLUDE_FROM_ALL)
set(protobuf_BUILD_TESTS OFF CACHE INTERNAL "")
set(protobuf_BUILD_PROTOC_BINARIES ON CACHE INTERNAL "")
set(protobuf_INSTALL OFF CACHE INTERNAL "")
set(protobuf_WITH_ZLIB OFF CACHE INTERNAL "")
set(protobuf_LOCAL_DEPENDENCIES_ONLY ON CACHE INTERNAL "")
add_subdirectory("${EXTERNALS}/protobuf" protobuf EXCLUDE_FROM_ALL)
