/** @file
    Channelizer hot-path: dot product + process function.

    Included by ISA-specific translation units (channelizer_sse2.c,
    channelizer_avx2.c, channelizer_avx512.c, channelizer_neon.c,
    channelizer_sve.c). Each TU is compiled with different ISA flags,
    producing auto-vectorized variants.

    The includer must define CHANNELIZER_PROCESS_FN before including
    this file, and must have already included:
      - channelizer.h
      - compat_opt.h
      - fft_kernels.h
      - hydrasdr_lfft.h
      - <string.h>

    Copyright (C) 2025-2026 Benjamin Vernoux <bvernoux@hydrasdr.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef CHANNELIZER_PROCESS_FN
#error "Define CHANNELIZER_PROCESS_FN before including channelizer_process.inc"
#endif

/* Compile-time constant for dot product loop bound.
 * 48 taps/branch = exactly 3 AVX-512 iterations, no epilogue. */
#define TAPS_PER_BRANCH (2 * 24)

/**
 * Compute dot product of contiguous SoA buffer with real coefficients.
 *
 * Linear buffers guarantee stride-1 access with no wrap logic.
 * Single loop = full SIMD utilization (16 floats/AVX-512).
 * With -ffast-math, the compiler auto-vectorizes and hides FMA latency
 * via multiple vector accumulators.
 */
static OPT_HOT void dotprod_linear(const float *OPT_RESTRICT wr,
                                   const float *OPT_RESTRICT wi,
                                   const float *OPT_RESTRICT coeff,
                                   int len,
                                   float *OPT_RESTRICT out_re,
                                   float *OPT_RESTRICT out_im)
{
    float sum_re = 0.0f, sum_im = 0.0f;
    OPT_PRAGMA_VECTORIZE
    for (int i = 0; i < len; i++) {
        sum_re += wr[i] * coeff[i];
        sum_im += wi[i] * coeff[i];
    }
    *out_re = sum_re;
    *out_im = sum_im;
}

/**
 * Run the filterbank analyzer: compute dot products and FFT.
 *
 * Based on liquid-dsp's FIRPFBCH_(analyzer_run).
 * Computes polyphase filter outputs and FFT using SoA format throughout.
 */
int CHANNELIZER_PROCESS_FN(channelizer_t *ch, const float *input, int n_samples,
                           float **channel_out, int *out_samples)
{
    if (!ch->initialized || !input || n_samples < 0)
        return -1;
    if (n_samples == 0) {
        *out_samples = 0;
        return 0;
    }

    /* Hoist ALL hot struct fields to locals.
     * Stores through ch-> force the compiler to re-load all other ch->
     * fields (can't prove no alias). Locals break this dependency chain
     * and let the optimizer keep values in registers. */
    const int M = ch->num_channels;
    const int M_mask = ch->channel_mask;
    const int D = ch->decimation_factor;
    const int w_alloc = ch->window_alloc;
    const int w_len = ch->window_len;
    int filter_index = ch->filter_index;
    int *OPT_RESTRICT write_pos = ch->window_write_pos;
    float **OPT_RESTRICT win_re = ch->window_re_ptrs;
    float **OPT_RESTRICT win_im = ch->window_im_ptrs;
    float **OPT_RESTRICT branches = ch->branches;
    float *fft_in_re = (float *)OPT_ASSUME_ALIGNED(ch->fft_in_re, 64);
    float *fft_in_im = (float *)OPT_ASSUME_ALIGNED(ch->fft_in_im, 64);
    float *fft_out_re = (float *)OPT_ASSUME_ALIGNED(ch->fft_out_re, 64);
    float *fft_out_im = (float *)OPT_ASSUME_ALIGNED(ch->fft_out_im, 64);
    hlfft_plan_t *fft_plan = ch->fft_plan;
    float **OPT_RESTRICT chan_out = ch->channel_outputs;
    const size_t out_buf_size = ch->output_buf_size;
    int output_idx = 0;

    /* Process D input samples at a time to produce 1 output sample per channel.
     * D = M/2 for 2x oversampled PFB (push half a block per FFT). */
    for (int s = 0; s + D <= n_samples; s += D) {

        /* --- Commutator: push D samples into window buffers --- */
        for (int i = 0; i < D; i++) {
            int idx = filter_index;
            int pos = write_pos[idx];

            win_re[idx][pos] = input[(s + i) * 2 + 0];
            win_im[idx][pos] = input[(s + i) * 2 + 1];

            pos++;
            if (OPT_UNLIKELY(pos >= w_alloc)) {
                float *wr = win_re[idx];
                float *wi = win_im[idx];
                memcpy(wr, wr + w_len, (size_t)w_len * sizeof(float));
                memcpy(wi, wi + w_len, (size_t)w_len * sizeof(float));
                pos = w_len;
            }
            write_pos[idx] = pos;

            filter_index = (idx + M - 1) & M_mask;
        }

        /* --- Dot products: M branches into FFT input --- */
        for (int i = 0; i < M; i++) {
            int index = (i + filter_index + 1) & M_mask;
            int out_idx = M - i - 1;
            int start = write_pos[index] - TAPS_PER_BRANCH;

            dotprod_linear(win_re[index] + start,
                           win_im[index] + start,
                           branches[i], TAPS_PER_BRANCH,
                           &fft_in_re[out_idx],
                           &fft_in_im[out_idx]);
        }

        /* --- M-point FFT (SoA, zero-copy) ---
         * Specialized kernels for N=2,4,8,16 eliminate all call
         * overhead (null checks, vtable, twiddle compute, staging).
         * M is loop-invariant so the compiler eliminates dead cases. */
        switch (M) {
        case 2:
            fft2_forward_soa(fft_in_re, fft_in_im,
                             fft_out_re, fft_out_im);
            break;
        case 4:
            fft4_forward_soa(fft_in_re, fft_in_im,
                             fft_out_re, fft_out_im);
            break;
        case 8:
            fft8_forward_soa(fft_in_re, fft_in_im,
                             fft_out_re, fft_out_im);
            break;
        case 16:
            fft16_forward_soa(fft_in_re, fft_in_im,
                              fft_out_re, fft_out_im);
            break;
        default:
            hlfft_forward_soa(fft_plan,
                              fft_in_re, fft_in_im,
                              fft_out_re, fft_out_im);
            break;
        }

        /* --- Phase correction + output store ---
         *
         * OS-PFB phase correction: (-1)^(k*n) for channel k, output n.
         * Even channels: correction = 1 (no change)
         * Odd channels: negate on odd output index
         *
         * Read from SoA fft_out, write AoS interleaved (downstream expects it).
         */
        if ((size_t)output_idx < out_buf_size) {
            for (int c = 0; c < M; c++) {
                float I = fft_out_re[c];
                float Q = fft_out_im[c];
                float sign = 1.0f - 2.0f * (float)((c & output_idx) & 1);
                chan_out[c][output_idx * 2 + 0] = I * sign;
                chan_out[c][output_idx * 2 + 1] = Q * sign;
            }
            output_idx++;
        }
    }

    /* Write back mutable state */
    ch->filter_index = filter_index;

    /* Return output buffer pointers */
    for (int c = 0; c < M; c++) {
        channel_out[c] = chan_out[c];
    }
    *out_samples = output_idx;

    return 0;
}

#undef TAPS_PER_BRANCH
