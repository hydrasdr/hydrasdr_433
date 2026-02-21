/** @file
    AVX2+FMA variant of channelizer_process.

    Compiled with -mavx2 -mfma -ffast-math (GCC/Clang) or
    /arch:AVX2 /fp:fast (MSVC). The dot product loop is
    auto-vectorized to 256-bit FMA instructions.

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

#define CHANNELIZER_PROCESS_FN channelizer_process_avx2
#include "channelizer_process.inc"
