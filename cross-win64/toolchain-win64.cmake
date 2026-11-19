# SPDX-FileCopyrightText: Copyright 2026 Shadps4_vr contributors
# SPDX-License-Identifier: GPL-2.0-or-later

# Cross-compile shadPS4 for Windows x64 from Linux with clang-cl + lld-link and the MSVC CRT /
# Windows SDK fetched by xwin. Set XWIN_DIR (the xwin "splat" output) and CLANG_RT_DIR (the folder
# with clang_rt.builtins-x86_64.lib) in the environment or with -D.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_VERSION 10.0)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

if (NOT XWIN_DIR)
    set(XWIN_DIR "$ENV{XWIN_DIR}")
endif()
if (NOT CLANG_RT_DIR)
    set(CLANG_RT_DIR "$ENV{CLANG_RT_DIR}")
endif()
if (NOT XWIN_DIR OR NOT EXISTS "${XWIN_DIR}/crt/include")
    message(FATAL_ERROR "XWIN_DIR must point to an xwin splat folder (crt/ and sdk/)")
endif()
# Pass on to try_compile projects.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES XWIN_DIR CLANG_RT_DIR)

set(CMAKE_C_COMPILER clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)
set(CMAKE_LINKER lld-link)
set(CMAKE_AR llvm-lib)
set(CMAKE_RC_COMPILER llvm-rc)
set(CMAKE_MT llvm-mt)
set(CMAKE_ASM_MASM_COMPILER llvm-ml)
set(CMAKE_C_COMPILER_TARGET x86_64-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET x86_64-pc-windows-msvc)

set(_inc "/imsvc ${XWIN_DIR}/crt/include /imsvc ${XWIN_DIR}/sdk/include/ucrt /imsvc ${XWIN_DIR}/sdk/include/um /imsvc ${XWIN_DIR}/sdk/include/shared /imsvc ${XWIN_DIR}/sdk/include/winrt")
set(CMAKE_C_FLAGS_INIT "--target=x86_64-pc-windows-msvc ${_inc}")
set(CMAKE_CXX_FLAGS_INIT "--target=x86_64-pc-windows-msvc ${_inc}")
set(CMAKE_RC_FLAGS_INIT "-I ${XWIN_DIR}/sdk/include/um -I ${XWIN_DIR}/sdk/include/shared")

set(_lib "/libpath:${XWIN_DIR}/crt/lib/x86_64 /libpath:${XWIN_DIR}/sdk/lib/um/x86_64 /libpath:${XWIN_DIR}/sdk/lib/ucrt/x86_64")
if (CLANG_RT_DIR)
    string(APPEND _lib " /libpath:${CLANG_RT_DIR}")
endif()
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_lib}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_lib}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_lib}")

# xwin fetches only release CRT libraries.
set(CMAKE_TRY_COMPILE_CONFIGURATION Release)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
