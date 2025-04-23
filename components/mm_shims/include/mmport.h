/*
 * Copyright 2023 Morse Micro
 *
 * This file is licensed under terms that can be found in the LICENSE.md file in the root
 * directory of the Morse Micro IoT SDK software package.
 */

#pragma once

#define MMPORT_BREAKPOINT() while (1)
#define MMPORT_GET_LR()     (__builtin_return_address(0))
#define MMPORT_GET_PC(_a)   ((_a) = 0)  // TODO
#define MMPORT_MEM_SYNC()   __sync_synchronize()