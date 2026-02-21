/** @file
    SSE2 (x86-64 baseline) variant of channelizer_process.

    Compiled with -ffast-math only (no explicit ISA flags).
    Auto-vectorized by the compiler for whatever baseline the
    target supports. This is the portable fallback.

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

#define CHANNELIZER_PROCESS_FN channelizer_process_sse2
#include "channelizer_process.inc"
