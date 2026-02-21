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
 * Compute dot product of interleaved I/Q circular buffer with real coefficients.
 *
 * The circular buffer wraps at most once for len <= window_alloc. Split into
 * one or two contiguous segments so each loop body is a simple stride-2 access
 * pattern that GCC can auto-vectorize with -O3 -ffast-math.
 */
static void dotprod_interleaved(const float *win_iq, int start, int mask,
                                const float *coeff, int len,
                                float *out_i, float *out_q)
{
    float sum_i = 0.0f;
    float sum_q = 0.0f;
    int alloc = mask + 1;
    int seg1 = alloc - start;  /* elements from start to end of buffer */

    if (seg1 >= len) {
        /* No wrap - single contiguous segment */
        const float *w = win_iq + start * 2;
        for (int i = 0; i < len; i++) {
            float c = coeff[i];
            sum_i += w[i * 2 + 0] * c;
            sum_q += w[i * 2 + 1] * c;
        }
    } else {
        /* Wrap - two contiguous segments */
        const float *w1 = win_iq + start * 2;
        for (int i = 0; i < seg1; i++) {
            float c = coeff[i];
            sum_i += w1[i * 2 + 0] * c;
            sum_q += w1[i * 2 + 1] * c;
        }
        const float *w2 = win_iq;  /* wraps to beginning */
        for (int i = 0; i < len - seg1; i++) {
            float c = coeff[seg1 + i];
            sum_i += w2[i * 2 + 0] * c;
            sum_q += w2[i * 2 + 1] * c;
        }
    }
    *out_i = sum_i;
    *out_q = sum_q;
}

int channelizer_init(channelizer_t *ch, int num_channels,
                     float center_freq, float bandwidth,
                     uint32_t input_rate, size_t max_input)
{
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
    ch->filter_coeffs = (float *)calloc((size_t)(M * p), sizeof(float));
    if (!ch->filter_coeffs) {
        free(proto);
        return -1;
    }
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

    /* Allocate interleaved I/Q window buffers for all branches (contiguous).
     * Use power-of-2 allocation for efficient modulo via bitmask.
     * Each branch has window_alloc * 2 floats (interleaved I/Q pairs). */
    int window_alloc = 1;
    while (window_alloc < p)
        window_alloc <<= 1;
    ch->window_alloc = window_alloc;
    ch->window_mask = window_alloc - 1;
    ch->channel_mask = M - 1;

    ch->window_iq = (float *)calloc((size_t)M * (size_t)window_alloc * 2, sizeof(float));
    if (!ch->window_iq) {
        channelizer_free(ch);
        return -1;
    }
    ch->window_ptrs = (float **)malloc((size_t)M * sizeof(float *));
    if (!ch->window_ptrs) {
        channelizer_free(ch);
        return -1;
    }
    for (int i = 0; i < M; i++)
        ch->window_ptrs[i] = ch->window_iq + (size_t)i * (size_t)window_alloc * 2;
    ch->window_len = p;

    /* Per-branch write positions for circular buffers (initialized to 0) */
    ch->window_write_pos = (int *)calloc((size_t)M, sizeof(int));
    if (!ch->window_write_pos) {
        channelizer_free(ch);
        return -1;
    }

    /* Initialize filter index for commutator pattern */
    ch->filter_index = M - 1;

    /* Allocate FFT work buffers (interleaved CF32 format) */
    ch->fft_in = (float *)calloc((size_t)M * 2, sizeof(float));
    if (!ch->fft_in) {
        channelizer_free(ch);
        return -1;
    }
    ch->fft_out = (float *)calloc((size_t)M * 2, sizeof(float));
    if (!ch->fft_out) {
        channelizer_free(ch);
        return -1;
    }

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
 * Computes polyphase filter outputs and FFT.
 */
static void analyzer_run(channelizer_t *ch, float *out)
{
    int M = ch->num_channels;
    int M_mask = ch->channel_mask;
    int p = ch->window_len;

    /* Execute filter outputs with index wrapping.
     * Store in reverse order (as per liquid-dsp algorithm).
     *
     * For each branch, the dot product reads `p` elements from the circular
     * buffer starting at the oldest relevant sample. The write_pos points
     * to the next-to-be-overwritten slot (= oldest element). We offset by
     * (window_alloc - p) to skip any padding slots and read exactly the
     * `p` most recent samples in oldest-to-newest order. */
    for (int i = 0; i < M; i++) {
        int index = (i + ch->filter_index + 1) & M_mask;
        int out_idx = M - i - 1;
        float sum_i, sum_q;

        /* Start of the p oldest-to-newest samples in the circular buffer */
        int start = (ch->window_write_pos[index] + ch->window_alloc - p) & ch->window_mask;

        /* Compute dot product: window[index] . branch[i] (interleaved, linearized) */
        dotprod_interleaved(ch->window_ptrs[index],
                            start, ch->window_mask,
                            ch->branches[i], p,
                            &sum_i, &sum_q);

        /* Store in interleaved format */
        ch->fft_in[out_idx * 2 + 0] = sum_i;
        ch->fft_in[out_idx * 2 + 1] = sum_q;
    }

    /* Execute M-point FFT via hydrasdr-lfft */
    hlfft_forward(ch->fft_plan,
                  (const hlfft_complex_t *)ch->fft_in,
                  (hlfft_complex_t *)out);
}

int channelizer_process(channelizer_t *ch, const float *input, int n_samples,
                        float **channel_out, int *out_samples)
{
    if (!ch->initialized || !input || n_samples < 0)
        return -1;
    if (n_samples == 0) {
        *out_samples = 0;
        return 0;
    }

    int M = ch->num_channels;
    int M_mask = ch->channel_mask;
    int D = ch->decimation_factor;
    int w_mask = ch->window_mask;
    int output_idx = 0;

    /* Process D input samples at a time to produce 1 output sample per channel.
     * D = M/2 for 2x oversampled PFB (push half a block per FFT). */
    for (int s = 0; s + D <= n_samples; s += D) {
        /* Push D samples into analyzer (inlined for locality) */
        for (int i = 0; i < D; i++) {
            int idx = ch->filter_index;
            int pos = ch->window_write_pos[idx];
            float *win = ch->window_ptrs[idx];

            /* Write interleaved I/Q at current position */
            win[pos * 2 + 0] = input[(s + i) * 2 + 0];
            win[pos * 2 + 1] = input[(s + i) * 2 + 1];

            /* Advance write position with bitmask wrap */
            ch->window_write_pos[idx] = (pos + 1) & w_mask;

            /* Decrement filter index (reverse commutator, bitmask) */
            ch->filter_index = (idx + M - 1) & M_mask;
        }

        /* Run analyzer to get M channel outputs (interleaved format) */
        analyzer_run(ch, ch->fft_out);

        /* Store outputs with OS-PFB phase correction.
         *
         * The 2x oversampled PFB introduces a systematic phase rotation
         * of exp(-j*pi*k*n) for channel k at output index n, because the
         * commutator only advances D=M/2 steps per output (not a full
         * cycle). Correct by multiplying by exp(j*pi*k*n) = (-1)^(k*n).
         *
         * Even channels (k=0,2,4,...): correction = 1 (no change)
         * Odd channels  (k=1,3,5,...): negate on odd output index
         */
        if ((size_t)output_idx < ch->output_buf_size) {
            for (int c = 0; c < M; c++) {
                float I = ch->fft_out[c * 2 + 0];
                float Q = ch->fft_out[c * 2 + 1];
                if ((c & 1) && (output_idx & 1)) {
                    I = -I;
                    Q = -Q;
                }
                ch->channel_outputs[c][output_idx * 2 + 0] = I;
                ch->channel_outputs[c][output_idx * 2 + 1] = Q;
            }
            output_idx++;
        }
    }

    /* Return output buffer pointers */
    for (int c = 0; c < M; c++) {
        channel_out[c] = ch->channel_outputs[c];
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

void channelizer_free(channelizer_t *ch)
{
    if (!ch)
        return;

    free(ch->filter_coeffs);
    free(ch->branches);
    free(ch->window_iq);
    free(ch->window_ptrs);
    free(ch->window_write_pos);
    free(ch->fft_in);
    free(ch->fft_out);
    if (ch->fft_plan) {
        hlfft_plan_destroy(ch->fft_plan);
    }
    free(ch->output_storage);
    free(ch->channel_outputs);
    free(ch->channel_freqs);

    memset(ch, 0, sizeof(*ch));
}
