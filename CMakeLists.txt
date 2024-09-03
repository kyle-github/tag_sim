#   Copyright (C) 2022 by Kyle Hayes
#   Author Kyle Hayes  kyle.hayes@gmail.com
#
# This software is available under the Mozilla Public license
# version 2.0 (MPL 2.0).
#
# MPL 2.0:
#
#   This Source Code Form is subject to the terms of the Mozilla Public
#   License, v. 2.0. If a copy of the MPL was not distributed with this
#   file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

cmake_minimum_required (VERSION 3.12)

project(tag_sim)

include(CheckCCompilerFlag)
include(CheckCSourceCompiles)

set(CMAKE_C_STANDARD 11)
#set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(ENABLE_ASAN "Enable ASAN" OFF)
option(ENABLE_MSAN "Enable MSAN" OFF)
option(ENABLE_TSAN "Enable TSAN" OFF)
option(ENABLE_UBSAN "Enable UBSAN" OFF)

#
# macros for compiler and linker flags
#
macro(add_compiler_flag)
   set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${ARGN}")
   set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} ${ARGN}")
endmacro(add_compiler_flag)

macro(add_linker_flag)
  set(CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} ${ARGN}")
endmacro(add_linker_flag)


if("${CMAKE_TOOLCHAIN_FILE}" STREQUAL "")
    set(TOOLCHAIN_FILE "toolchains/${CMAKE_SYSTEM_NAME}-${CMAKE_C_COMPILER_ID}.cmake")

    message("TOOLCHAIN_FILE = ${TOOLCHAIN_FILE}")

    include("${TOOLCHAIN_FILE}")
endif()

add_executable(tag_sim
    "src/util/debug.c"
    "src/util/debug.h"
    "${PROACTOR_IMPL_SRC}"
    "src/util/proactor_net.h"
    "src/util/shims.h"
    "src/util/status.c"
    "src/util/status.h"
    "src/util/time_utils.c"
    "src/util/time_utils.h"
)

message("compiler flags = \"${COMPILER_FLAGS}\"")
target_compile_options(tag_sim PUBLIC ${COMPILER_FLAGS})
