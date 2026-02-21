/** @file
    CF32 Polyphase Resampler for HydraSDR.

    Copyright (C) 2025-2026 Benjamin Vernoux <bvernoux@hydrasdr.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "cf32_resampler.h"
#include <string.h>
#include <math.h>
#include <limits.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Modified Bessel function of the first kind, order 0.
 * Series approximation: I0(x) = sum_{k=0}^inf ((x/2)^k / k!)^2 */
static float bessel_i0(float x)
{
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

int cf32_resampler_gcd(int a, int b)
{
    while (b != 0) {
        int t = b;
        b = a % b;
        a = t;
    }
    return a;
}

void cf32_resampler_design_filter(float *coeffs, int num_taps, int factor)
{
    float cutoff = 1.0f / (float)factor;
    float center = (float)(num_taps - 1) / 2.0f;
    float sum = 0.0f;
    float As = RESAMPLER_STOPBAND_DB;
    float beta, I0_beta;

    /* Compute Kaiser beta from stopband attenuation */
    if (As > 50.0f) {
        beta = 0.1102f * (As - 8.7f);
    } else if (As > 21.0f) {
        beta = 0.5842f * powf(As - 21.0f, 0.4f)
               + 0.07886f * (As - 21.0f);
    } else {
        beta = 0.0f;
    }

    I0_beta = bessel_i0(beta);

    for (int i = 0; i < num_taps; i++) {
        float n = (float)i - center;
        float sinc, window, t;

        /* Sinc function */
        if (fabsf(n) < 1e-6f)
            sinc = cutoff;
        else {
            float x = (float)M_PI * cutoff * n;
            sinc = sinf(x) / ((float)M_PI * n);
        }

        /* Kaiser window */
        t = (float)i / (float)(num_taps - 1);
        t = 2.0f * t - 1.0f;  /* Map to [-1, 1] */
        t = sqrtf(1.0f - t * t);
        window = bessel_i0(beta * t) / I0_beta;

        coeffs[i] = sinc * window;
        sum += coeffs[i];
    }

    /* Normalize for unity DC gain */
    if (fabsf(sum) > 1e-6f) {
        for (int i = 0; i < num_taps; i++)
            coeffs[i] /= sum;
    }
}

int cf32_resampler_init(cf32_resampler_t *res, uint32_t input_rate, uint32_t output_rate, size_t max_input_samples)
{
    memset(res, 0, sizeof(*res));

    if (input_rate == 0 || output_rate == 0)
        return -1;

    if (input_rate == output_rate) {
        res->initialized = 0;  /* No resampling needed */
        return 0;
    }

    /* Validate sample rates fit in int for GCD computation */
    if (input_rate > (uint32_t)INT_MAX || output_rate > (uint32_t)INT_MAX)
        return -1;

    /* Find L/M ratio using GCD for minimal computational cost */
    int g = cf32_resampler_gcd((int)output_rate, (int)input_rate);
    res->up_factor = (int)output_rate / g;
    res->down_factor = (int)input_rate / g;

    /* Filter design: 32 taps per polyphase branch (Kaiser window) */
    res->taps_per_branch = RESAMPLER_TAPS_PER_BRANCH;

    /* Check for integer overflow before multiplication */
    if (res->up_factor > INT_MAX / res->taps_per_branch)
        return -1;
    res->num_taps = res->taps_per_branch * res->up_factor;
    res->phase_idx = 0;

    /* Design prototype lowpass filter */
    float *proto_coeffs = (float *)malloc((size_t)res->num_taps * sizeof(float));
    if (!proto_coeffs)
        return -1;

    int max_factor = res->up_factor > res->down_factor ? res->up_factor : res->down_factor;
    cf32_resampler_design_filter(proto_coeffs, res->num_taps, max_factor);

    /* Scale by interpolation factor for gain correction */
    for (int i = 0; i < res->num_taps; i++)
        proto_coeffs[i] *= (float)res->up_factor;

    /* Allocate polyphase branches (float32 for full precision) */
    res->branches = (float **)malloc((size_t)res->up_factor * sizeof(float *));
    if (!res->branches) {
        free(proto_coeffs);
        return -1;
    }
    res->branch_data = (float *)calloc((size_t)res->up_factor * (size_t)res->taps_per_branch, sizeof(float));
    if (!res->branch_data) {
        free(proto_coeffs);
        free(res->branches);
        return -1;
    }

    /* Decompose into polyphase branches */
    for (int m = 0; m < res->up_factor; m++) {
        res->branches[m] = res->branch_data + m * res->taps_per_branch;
        for (int k = 0; k < res->taps_per_branch; k++) {
            int idx = m + k * res->up_factor;
            if (idx < res->num_taps)
                res->branches[m][k] = proto_coeffs[idx];
        }
    }
    free(proto_coeffs);

    /* Initialize circular history buffers (power of 2 for fast modulo) */
    res->hist_size = 64;
    while (res->hist_size < res->taps_per_branch * 2)
        res->hist_size *= 2;
    res->hist_mask = res->hist_size - 1;
    res->write_pos = 0;

    res->hist_i = (float *)calloc((size_t)res->hist_size, sizeof(float));
    if (!res->hist_i)
        goto fail_branches;
    res->hist_q = (float *)calloc((size_t)res->hist_size, sizeof(float));
    if (!res->hist_q)
        goto fail_hist;

    /* Allocate output buffer (CF32 native, interleaved I/Q) */
    res->output_buf_size = (max_input_samples * (size_t)res->up_factor / (size_t)res->down_factor) + (size_t)res->up_factor + 1;
    /* Guard against overflow: output_buf_size * 2 * sizeof(float) must fit in size_t */
    if (res->output_buf_size > SIZE_MAX / (2 * sizeof(float)))
        goto fail_hist;
    res->output_buf = (float *)malloc(res->output_buf_size * 2 * sizeof(float));  /* I + Q floats */
    if (!res->output_buf)
        goto fail_hist;

    res->initialized = 1;
    return 0;

fail_hist:
    free(res->hist_i);
    free(res->hist_q);
    res->hist_i = NULL;
    res->hist_q = NULL;
fail_branches:
    free(res->branches);
    free(res->branch_data);
    res->branches = NULL;
    res->branch_data = NULL;
    return -1;
}

int cf32_resampler_process(cf32_resampler_t *res, const float *input, int num_iq_samples, float **output, int max_output)
{
    const int L = res->up_factor;
    const int M = res->down_factor;
    int phase = res->phase_idx;
    int out_idx = 0;

    float *out = res->output_buf;
    if (max_output > (int)res->output_buf_size)
        max_output = (int)res->output_buf_size;

    for (int n = 0; n < num_iq_samples && out_idx < max_output; n++) {
        /* Write input to circular history buffers */
        res->hist_i[res->write_pos & res->hist_mask] = input[n * 2 + 0];
        res->hist_q[res->write_pos & res->hist_mask] = input[n * 2 + 1];
        res->write_pos++;

        /* Generate output samples for this input */
        int base_read_pos = (res->write_pos - 1) & res->hist_mask;
        while (phase < L && out_idx < max_output) {
            float *branch = res->branches[phase];
            float acc_i = 0.0f, acc_q = 0.0f;
            int read_pos = base_read_pos;

            /* FIR filter (float MAC) */
            for (int k = 0; k < res->taps_per_branch; k++) {
                acc_i += res->hist_i[read_pos] * branch[k];
                acc_q += res->hist_q[read_pos] * branch[k];
                read_pos = (read_pos - 1) & res->hist_mask;
            }

            /* Output as native float32 */
            out[out_idx * 2 + 0] = acc_i;
            out[out_idx * 2 + 1] = acc_q;
            out_idx++;

            phase += M;
        }

        phase -= L;
    }

    res->phase_idx = phase;
    *output = res->output_buf;
    return out_idx;
}

void cf32_resampler_free(cf32_resampler_t *res)
{
    if (!res)
        return;
    free(res->branches);
    free(res->branch_data);
    free(res->hist_i);
    free(res->hist_q);
    free(res->output_buf);
    memset(res, 0, sizeof(*res));
}
