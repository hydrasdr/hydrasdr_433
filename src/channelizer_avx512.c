/** @file
    AVX-512 variant of channelizer_process.

    Compiled with -mavx512f -mavx512vl -mfma -ffast-math (GCC/Clang)
    or /arch:AVX512 /fp:fast (MSVC). The dot product loop is
    auto-vectorized to 512-bit FMA instructions (16 floats/iteration,
    48 taps = exactly 3 iterations, no epilogue).

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

#define CHANNELIZER_PROCESS_FN channelizer_process_avx512
#include "channelizer_process.inc"
