# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# SPDX-License-Identifier: Apache-2.0

# The build host's own gcc or clang, compiling against an x86-64 recipe
# sysroot. The image toolchain ships no sanitizer runtimes; the host's do,
# and with the same glibc as the sysroot they run against its libraries.
#
#   MCS_SYSROOT   <recipe workdir>/recipe-sysroot of the corei7-64 build
#   MCS_HOST_CC   gcc or clang (default gcc)

# try_compile() re-reads this file in a project of its own.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES MCS_SYSROOT MCS_HOST_CC)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

if (NOT MCS_HOST_CC)
    set(MCS_HOST_CC gcc)
endif()
if (MCS_HOST_CC STREQUAL "clang")
    set(CMAKE_C_COMPILER clang)
    set(CMAKE_CXX_COMPILER clang++)
    # The sysroot's gcc (crtbeginS.o, libgcc, libstdc++'s headers) is in a
    # Yocto triple directory clang does not look for on its own.
    file(GLOB MCS_GCC_INSTALL_DIR LIST_DIRECTORIES true "${MCS_SYSROOT}/usr/lib/x86_64-*-linux/*")
    set(CMAKE_C_FLAGS_INIT "--gcc-install-dir=${MCS_GCC_INSTALL_DIR}")
    set(CMAKE_CXX_FLAGS_INIT "--gcc-install-dir=${MCS_GCC_INSTALL_DIR}")
    set(CMAKE_EXE_LINKER_FLAGS_INIT "--gcc-install-dir=${MCS_GCC_INSTALL_DIR}")
else()
    set(CMAKE_C_COMPILER gcc)
    set(CMAKE_CXX_COMPILER g++)
endif()

set(CMAKE_SYSROOT ${MCS_SYSROOT})
set(CMAKE_FIND_ROOT_PATH ${MCS_SYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# pkg-config is pointed at the sysroot by the caller's environment
# (PKG_CONFIG_SYSROOT_DIR, PKG_CONFIG_LIBDIR): see run-sanitizers.sh.
