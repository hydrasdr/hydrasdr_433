/** @file
    CF32 Polyphase Resampler for HydraSDR.

    Copyright (C) 2025-2026 Benjamin Vernoux <bvernoux@hydrasdr.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_CF32_RESAMPLER_H_
#define INCLUDE_CF32_RESAMPLER_H_

#include <stdint.h>
#include <stdlib.h>

/* Resampler filter design parameters */
#define RESAMPLER_TAPS_PER_BRANCH  32     /* Taps per polyphase branch */
#define RESAMPLER_STOPBAND_DB      60.0f  /* Kaiser stopband attenuation (dB) */

/* Float32 polyphase resampler for IQ data */
typedef struct {
    int up_factor;          /* Interpolation factor L */
    int down_factor;        /* Decimation factor M */
    int num_taps;           /* Total filter taps */
    int taps_per_branch;    /* Taps per polyphase branch */
    int phase_idx;          /* Current phase index */

    float **branches;       /* Polyphase filter branches [L][taps_per_branch] */
    float *branch_data;     /* Contiguous storage for branches */

    /* Circular buffers for I and Q channels */
    float *hist_i;
    float *hist_q;
    int hist_size;          /* Power of 2 size */
    int hist_mask;          /* hist_size - 1 for fast modulo */
    int write_pos;          /* Current write position */

    float *output_buf;      /* Resampled output buffer (CF32 native) */
    size_t output_buf_size; /* Output buffer size in complex samples */

    int initialized;
} cf32_resampler_t;

/** Compute greatest common divisor. */
int cf32_resampler_gcd(int a, int b);

/** Design lowpass filter coefficients. */
void cf32_resampler_design_filter(float *coeffs, int num_taps, int factor);

/** Initialize resampler. Returns 0 on success. */
int cf32_resampler_init(cf32_resampler_t *res, uint32_t input_rate, uint32_t output_rate, size_t max_input_samples);

/** Process IQ samples. Returns number of output samples. */
int cf32_resampler_process(cf32_resampler_t *res, const float *input, int num_iq_samples, float **output, int max_output);

/** Free resampler resources. */
void cf32_resampler_free(cf32_resampler_t *res);

#endif /* INCLUDE_CF32_RESAMPLER_H_ */
