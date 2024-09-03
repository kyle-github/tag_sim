#   Copyright (C) 2024 by Kyle Hayes
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


#
# address, memory, thread and undefined sanitizers
#
if (ENABLE_ASAN)
  add_compiler_flag("-fsanitize=address")
  add_linker_flag("-fsanitize=address")
endif()

if(ENABLE_MSAN)
  add_compiler_flag("-fsanitize=memory")
  add_linker_flag("-fsanitize=memory")
endif()

if(ENABLE_TSAN)
  add_compiler_flag("-fPIE -fsanitize=thread")
  add_linker_flag("-fPIE -fsanitize=thread")
endif()

if(ENABLE_UBSAN)
  add_compiler_flag("-fsanitize=undefined")
  add_linker_flag("-fsanitize=undefined")
endif()


set(PROACTOR_IMPL_SRC "src/util/proactor_net_kevent.c")

set(COMPILER_FLAGS "--std=c11"
                   "-fms-extensions"
                   "-Wno-microsoft-anon-tag"
                   "-DIS_APPLE"
                   "-DIS_BSD"
                   "-DIS_UNIX"
)
