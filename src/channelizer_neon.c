/** @file
    NEON variant of channelizer_process.

    On AArch64, NEON is mandatory; GCC/Clang auto-vectorize to
    fmla.4s with -ffast-math. Compiled with: -ffast-math only
    (NEON is implicit on AArch64).

    On ARMv7, compiled with -mfpu=neon -ffast-math when NEON
    is available.

    Copyright (C) 2026 Benjamin Vernoux <bvernoux@hydrasdr.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "channelizer.h"
#include "compat_opt.h"
#include "fft_kernels.h"
#include <string.h>

#define CHANNELIZER_PROCESS_FN channelizer_process_neon
#include "channelizer_process.inc"
