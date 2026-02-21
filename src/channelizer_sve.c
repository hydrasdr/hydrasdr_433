/** @file
    SVE variant of channelizer_process.

    Compiled with -march=armv8-a+sve -ffast-math (GCC/Clang).
    GCC/Clang auto-vectorize the dot product loop to scalable
    fmla instructions. Only called when runtime SVE detection
    confirms hardware support.

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

#define CHANNELIZER_PROCESS_FN channelizer_process_sve
#include "channelizer_process.inc"
