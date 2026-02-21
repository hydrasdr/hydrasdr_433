/** @file
    Polyphase Filter Bank Channelizer for HydraSDR wideband scanning.

    Implementation based on liquid-dsp's firpfbch algorithm by Joseph Gaeddert.
    Implements an M-channel 2x oversampled analysis PFB (D = M/2).

    Algorithm:
      1. Input samples are pushed into polyphase filter branches via commutator
      2. Each branch computes a dot product (FIR filter output)
      3. M-point FFT separates the frequency channels

    Original liquid-dsp copyright:
    Copyright (c) 2007 - 2024 Joseph Gaeddert (MIT License)

    Adaptation for HydraSDR:
    Copyright (C) 2025-2026 Benjamin Vernoux <bvernoux@hydrasdr.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "channelizer.h"
#include "build_info.h"
#include "compat_opt.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/*
 * Portable atomic int for thread-safe one-time init.
 * MSVC in C mode lacks <stdatomic.h> before /std:c11;
 * GCC/Clang have __atomic builtins in C99.
 */
#if defined(_MSC_VER)
#include <windows.h>
typedef volatile LONG atomic_init_t;
#define ATOMIC_INIT_VAL 0
static inline int atomic_init_load(atomic_init_t *p) { return (int)*p; }
static inline void atomic_init_store(atomic_init_t *p, int v) { _InterlockedExchange(p, (LONG)v); }
static inline int atomic_init_cas(atomic_init_t *p, int expected, int desired)
{
    return _InterlockedCompareExchange(p, (LONG)desired, (LONG)expected) == (LONG)expected;
}
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_ATOMICS__)
#include <stdatomic.h>
typedef atomic_int atomic_init_t;
#define ATOMIC_INIT_VAL 0
static inline int atomic_init_load(atomic_init_t *p) { return atomic_load(p); }
static inline void atomic_init_store(atomic_init_t *p, int v) { atomic_store(p, v); }
static inline int atomic_init_cas(atomic_init_t *p, int expected, int desired)
{
    return atomic_compare_exchange_strong(p, &expected, desired);
}
#else
/* GCC/Clang __atomic builtins (works in C99 mode) */
typedef volatile int atomic_init_t;
#define ATOMIC_INIT_VAL 0
static inline int atomic_init_load(atomic_init_t *p) { return __atomic_load_n(p, __ATOMIC_SEQ_CST); }
static inline void atomic_init_store(atomic_init_t *p, int v) { __atomic_store_n(p, v, __ATOMIC_SEQ_CST); }
static inline int atomic_init_cas(atomic_init_t *p, int expected, int desired)
{
    return __atomic_compare_exchange_n(p, &expected, desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Filter design parameters
 *
 * For good adjacent channel rejection in PFB channelizer:
 * - More taps = sharper transition = better isolation
 * - Tighter cutoff = wider transition band = better stopband
 * - Higher stopband attenuation = less leakage
 *
 * With m=24 (48 taps/branch) and 80 dB target:
 * - Adjacent channel rejection: >41 dB (8-ch), >44 dB (4-ch)
 * - Non-adjacent rejection: >49 dB
 */
#define FILTER_SEMI_LEN     24       /* m: filter semi-length in symbols (48 taps/branch) */
#define TAPS_PER_BRANCH     (2 * FILTER_SEMI_LEN)  /* Compile-time constant for dot product */
#define FILTER_STOPBAND_DB  80.0f    /* Target stopband attenuation in dB */
/* CHANNELIZER_CUTOFF_RATIO defined in channelizer.h (public API) */
#define EPSILON             1e-10f

/**
 * Check if n is a power of 2
 */
static int is_power_of_2(int n)
{
    return n > 0 && (n & (n - 1)) == 0;
}

/**
 * Compute Kaiser window coefficient.
 * I0(x) is modified Bessel function of first kind, order 0.
 */
static float bessel_i0(float x)
{
    /* Series approximation: I0(x) = sum_{k=0}^inf ((x/2)^k / k!)^2 */
    float sum = 1.0f;
    float term = 1.0f;
    float x2 = x * x * 0.25f;

    for (int k = 1; k < 32; k++) {
        term *= x2 / (float)(k * k);
        sum += term;
        if (term < sum * 1e-10f)
            break;
    }
    return sum;
}

/**
 * Design lowpass Kaiser window FIR filter.
 *
 * Based on liquid-dsp's liquid_firdes_kaiser().
 *
 * @param h         Output coefficients [size: h_len]
 * @param h_len     Filter length (should be 2*M*m + 1)
 * @param fc        Normalized cutoff frequency (0 < fc < 0.5)
 * @param As        Stopband attenuation in dB
 */
static void design_kaiser_filter(float *h, int h_len, float fc, float As)
{
    float beta;
    float t, t1, t2;
    int n;
    float center = (float)(h_len - 1) / 2.0f;

    /* Compute Kaiser beta from stopband attenuation */
    if (As > 50.0f) {
        beta = 0.1102f * (As - 8.7f);
    } else if (As > 21.0f) {
        beta = 0.5842f * powf(As - 21.0f, 0.4f)
               + 0.07886f * (As - 21.0f);
    } else {
        beta = 0.0f;
    }

    float I0_beta = bessel_i0(beta);
    float sum = 0.0f;

    for (n = 0; n < h_len; n++) {
        t = (float)n - center;

        /* Sinc function: sinc(2*fc*t) */
        if (fabsf(t) < EPSILON) {
            t1 = 2.0f * fc;
        } else {
            t1 = sinf(2.0f * (float)M_PI * fc * t) / ((float)M_PI * t);
        }

        /* Kaiser window */
        t2 = (float)n / (float)(h_len - 1);
        t2 = 2.0f * t2 - 1.0f;  /* Map to [-1, 1] */
        t2 = sqrtf(1.0f - t2 * t2);
        t2 = bessel_i0(beta * t2) / I0_beta;

        h[n] = t1 * t2;
        sum += h[n];
    }

    /* Normalize for unity DC gain */
    if (fabsf(sum) > EPSILON) {
        for (n = 0; n < h_len; n++)
            h[n] /= sum;
    }
}

/* Thread-safe hydrasdr-lfft library initialization state.
 * 0 = not started, 1 = in progress, 2 = done, -1 = failed */
static atomic_init_t hlfft_init_state = ATOMIC_INIT_VAL;

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

int channelizer_init(channelizer_t *ch, int num_channels,
                     float center_freq, float bandwidth,
                     uint32_t input_rate, size_t max_input)
{
    static int build_info_printed;
    if (!build_info_printed) {
        build_info_printed = 1;
        fprintf(stderr, "[Channelizer] %s\n", channelizer_build_info());
    }

    memset(ch, 0, sizeof(*ch));

    /* Validate parameters */
    if (num_channels < 2 || num_channels > CHANNELIZER_MAX_CHANNELS)
        return -1;
    if (!is_power_of_2(num_channels))
        return -1;
    if (input_rate == 0)
        return -1;

    int M = num_channels;
    int m = FILTER_SEMI_LEN;  /* Filter semi-length in symbols */
    int p = 2 * m;            /* Taps per polyphase branch */
    int h_len = 2 * M * m + 1; /* Total prototype filter length */

    ch->num_channels = M;
    ch->center_freq = center_freq;
    ch->bandwidth = bandwidth;
    /* Channel spacing is determined by input sample rate, not bandwidth.
     * The FFT naturally produces bins at multiples of fs/M. */
    ch->channel_spacing = (float)input_rate / (float)M;
    ch->input_rate = input_rate;
    ch->decimation_factor = M / 2;
    ch->channel_rate = input_rate / (uint32_t)ch->decimation_factor;
    ch->taps_per_branch = p;
    ch->total_taps = h_len;

    /* Design prototype lowpass filter */
    float *proto = (float *)malloc((size_t)h_len * sizeof(float));
    if (!proto)
        return -1;

    /* Cutoff below channel Nyquist to allow transition band for steep rolloff.
     * With CHANNELIZER_CUTOFF_RATIO=0.9, usable bandwidth is ~90% of channel spacing.
     * The remaining 10% is transition band for stopband rejection. */
    float fc = CHANNELIZER_CUTOFF_RATIO / (float)M;
    design_kaiser_filter(proto, h_len, fc, FILTER_STOPBAND_DB);

    /* Allocate polyphase filter branches.
     *
     * Sub-sample prototype filter into M branches.
     * Branch i gets coefficients: h[i], h[i+M], h[i+2M], ...
     * Coefficients are stored in REVERSE order for dot product.
     */
    size_t coeff_size = (size_t)(M * p) * sizeof(float);
    ch->filter_coeffs = (float *)opt_aligned_alloc(coeff_size, 64);
    if (!ch->filter_coeffs) {
        free(proto);
        return -1;
    }
    memset(ch->filter_coeffs, 0, coeff_size);
    ch->branches = (float **)malloc((size_t)M * sizeof(float *));
    if (!ch->branches) {
        free(proto);
        channelizer_free(ch);
        return -1;
    }

    for (int i = 0; i < M; i++) {
        ch->branches[i] = ch->filter_coeffs + i * p;
        for (int n = 0; n < p; n++) {
            int proto_idx = i + n * M;
            /* Store in reverse order for efficient dot product */
            if (proto_idx < h_len)
                ch->branches[i][p - n - 1] = proto[proto_idx];
        }
    }
    free(proto);

    /* Allocate SoA window buffers for all branches (contiguous).
     * Linear buffer: 2*p per branch. The dot product always reads a
     * contiguous p-element segment. When write_pos reaches 2*p, the
     * last p elements are shifted to the front (amortized O(1)). */
    int window_alloc = 2 * p;
    ch->window_alloc = window_alloc;
    ch->channel_mask = M - 1;

    size_t win_buf_size = (size_t)M * (size_t)window_alloc * sizeof(float);
    ch->window_re = (float *)opt_aligned_alloc(win_buf_size, 64);
    ch->window_im = (float *)opt_aligned_alloc(win_buf_size, 64);
    if (!ch->window_re || !ch->window_im) {
        channelizer_free(ch);
        return -1;
    }
    memset(ch->window_re, 0, win_buf_size);
    memset(ch->window_im, 0, win_buf_size);
    ch->window_re_ptrs = (float **)malloc((size_t)M * sizeof(float *));
    ch->window_im_ptrs = (float **)malloc((size_t)M * sizeof(float *));
    if (!ch->window_re_ptrs || !ch->window_im_ptrs) {
        channelizer_free(ch);
        return -1;
    }
    for (int i = 0; i < M; i++) {
        ch->window_re_ptrs[i] = ch->window_re + (size_t)i * (size_t)window_alloc;
        ch->window_im_ptrs[i] = ch->window_im + (size_t)i * (size_t)window_alloc;
    }
    ch->window_len = p;

    /* Per-branch write positions (start at p so read at pos-p=0 is valid) */
    ch->window_write_pos = (int *)malloc((size_t)M * sizeof(int));
    if (!ch->window_write_pos) {
        channelizer_free(ch);
        return -1;
    }
    for (int i = 0; i < M; i++)
        ch->window_write_pos[i] = p;

    /* Initialize filter index for commutator pattern */
    ch->filter_index = M - 1;

    /* Allocate FFT work buffers (SoA split format, 64-byte aligned) */
    size_t fft_buf_size = (size_t)M * sizeof(float);
    ch->fft_in_re = (float *)opt_aligned_alloc(fft_buf_size, 64);
    ch->fft_in_im = (float *)opt_aligned_alloc(fft_buf_size, 64);
    ch->fft_out_re = (float *)opt_aligned_alloc(fft_buf_size, 64);
    ch->fft_out_im = (float *)opt_aligned_alloc(fft_buf_size, 64);
    if (!ch->fft_in_re || !ch->fft_in_im ||
        !ch->fft_out_re || !ch->fft_out_im) {
        channelizer_free(ch);
        return -1;
    }
    memset(ch->fft_in_re, 0, fft_buf_size);
    memset(ch->fft_in_im, 0, fft_buf_size);
    memset(ch->fft_out_re, 0, fft_buf_size);
    memset(ch->fft_out_im, 0, fft_buf_size);

    /* Initialize hydrasdr-lfft library (thread-safe, once globally) */
    {
        if (atomic_init_cas(&hlfft_init_state, 0, 1)) {
            /* Won the init race - perform initialization */
            if (hlfft_init() != HLFFT_OK) {
                atomic_init_store(&hlfft_init_state, -1);
                channelizer_free(ch);
                return -1;
            }
            atomic_init_store(&hlfft_init_state, 2);
        } else {
            /* Another caller is initializing or has finished.
             * Spin-wait until state transitions from 1 (in-progress).
             * GCC analyzer false-positive: it cannot see that another
             * thread will store a different value via atomic CAS above. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wanalyzer-infinite-loop"
#endif
            while (atomic_init_load(&hlfft_init_state) == 1) {
                /* Spin-wait for init to complete */
            }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
            if (atomic_init_load(&hlfft_init_state) != 2) {
                channelizer_free(ch);
                return -1;
            }
        }
    }

    /* Create FFT plan for M-point forward FFT */
    ch->fft_plan = hlfft_plan_create((size_t)M, NULL);
    if (!ch->fft_plan) {
        channelizer_free(ch);
        return -1;
    }

    /* Allocate output buffers (minimum 2 to avoid degenerate edge cases) */
    ch->output_buf_size = (max_input / (size_t)ch->decimation_factor) + 1;
    if (ch->output_buf_size < 2)
        ch->output_buf_size = 2;
    size_t total_output = ch->output_buf_size * (size_t)M * 2;
    ch->output_storage = (float *)malloc(total_output * sizeof(float));
    if (!ch->output_storage) {
        channelizer_free(ch);
        return -1;
    }
    ch->channel_outputs = (float **)malloc((size_t)M * sizeof(float *));
    if (!ch->channel_outputs) {
        channelizer_free(ch);
        return -1;
    }
    for (int i = 0; i < M; i++) {
        ch->channel_outputs[i] = ch->output_storage + i * ch->output_buf_size * 2;
    }

    /* Setup channel frequencies.
     *
     * Channel ordering matches liquid-dsp's natural FFT bin order:
     *
     *   Channel 0:       DC (center_freq + 0 Hz)
     *   Channel 1:       center_freq + fs/M  (positive offset)
     *   Channel 2:       center_freq + 2*fs/M
     *   ...
     *   Channel M/2:     center_freq ± fs/2 (Nyquist, edge of band)
     *   Channel M/2+1:   center_freq - (M/2-1)*fs/M (negative offset)
     *   ...
     *   Channel M-1:     center_freq - fs/M
     *
     * Example for M=4, center=868.5 MHz, BW=2 MHz, spacing=500 kHz:
     *   Channel 0: 868.5 MHz (DC)
     *   Channel 1: 869.0 MHz (+500 kHz)
     *   Channel 2: 869.5 MHz (Nyquist, +1 MHz)
     *   Channel 3: 868.0 MHz (-500 kHz)
     *
     * Example for M=8, center=868.5 MHz, BW=2 MHz, spacing=250 kHz:
     *   Channel 0: 868.5 MHz (DC)
     *   Channel 1: 868.75 MHz (+250 kHz)
     *   Channel 2: 869.0 MHz (+500 kHz)
     *   Channel 3: 869.25 MHz (+750 kHz)
     *   Channel 4: 869.5 MHz (Nyquist, +1 MHz)
     *   Channel 5: 867.75 MHz (-750 kHz)
     *   Channel 6: 868.0 MHz (-500 kHz)
     *   Channel 7: 868.25 MHz (-250 kHz)
     *
     * Usable bandwidth per channel is approximately 0.8 * channel_spacing
     * due to filter transition band at channel edges.
     */
    ch->channel_freqs = (float *)malloc((size_t)M * sizeof(float));
    if (!ch->channel_freqs) {
        channelizer_free(ch);
        return -1;
    }

    for (int c = 0; c < M; c++) {
        /* Compute frequency offset for this FFT bin */
        float bin_freq_offset;
        if (c == 0) {
            bin_freq_offset = 0.0f;  /* DC */
        } else if (c <= M / 2) {
            /* Positive frequency bins (1 to M/2) */
            bin_freq_offset = (float)c * ch->channel_spacing;
        } else {
            /* Negative frequency bins (M/2+1 to M-1) */
            bin_freq_offset = (float)(c - M) * ch->channel_spacing;
        }

        ch->channel_freqs[c] = center_freq + bin_freq_offset;
    }

    ch->initialized = 1;
    return 0;
}

/* analyzer_push inlined into channelizer_process for better locality */

/**
 * Run the filterbank analyzer: compute dot products and FFT.
 *
 * Based on liquid-dsp's FIRPFBCH(_analyzer_run).
 * Computes polyphase filter outputs and FFT using SoA format throughout.
 */
int channelizer_process(channelizer_t *ch, const float *input, int n_samples,
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

        /* --- M-point FFT (SoA, zero-copy) --- */
        hlfft_forward_soa(fft_plan,
                          fft_in_re, fft_in_im,
                          fft_out_re, fft_out_im);

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

float channelizer_get_channel_freq(channelizer_t *ch, int channel)
{
    if (!ch || !ch->initialized || channel < 0 || channel >= ch->num_channels)
        return 0.0f;
    return ch->channel_freqs[channel];
}

const char *channelizer_build_info(void)
{
	return "DSP: " BUILD_INFO_STR;
}

void channelizer_free(channelizer_t *ch)
{
    if (!ch)
        return;

    opt_aligned_free(ch->filter_coeffs);
    free(ch->branches);
    opt_aligned_free(ch->window_re);
    opt_aligned_free(ch->window_im);
    free(ch->window_re_ptrs);
    free(ch->window_im_ptrs);
    free(ch->window_write_pos);
    opt_aligned_free(ch->fft_in_re);
    opt_aligned_free(ch->fft_in_im);
    opt_aligned_free(ch->fft_out_re);
    opt_aligned_free(ch->fft_out_im);
    if (ch->fft_plan) {
        hlfft_plan_destroy(ch->fft_plan);
    }
    free(ch->output_storage);
    free(ch->channel_outputs);
    free(ch->channel_freqs);

    memset(ch, 0, sizeof(*ch));
}
