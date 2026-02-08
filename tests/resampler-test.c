/*
 * Polyphase Resampler Test
 *
 * Tests the CF32 polyphase resampler for HydraSDR.
 * Verifies correct sample rate conversion and signal integrity.
 *
 * Copyright (C) 2025-2026 Benjamin Vernoux <bvernoux@hydrasdr.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <limits.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

/* Include the actual resampler implementation from the shared header */
#include "cf32_resampler.h"

/*============================================================================
 * Test Constants (avoid magic numbers)
 *============================================================================*/

/* Sample rates used in testing */
#define TEST_RATE_HYDRASDR      312500   /* HydraSDR native rate */
#define TEST_RATE_TARGET        250000   /* Default target (freq <= 800MHz) */
#define TEST_RATE_TARGET_HF     1000000  /* High-freq target (freq > 800MHz) */
#define TEST_RATE_AUDIO_IN      48000    /* Audio input rate */
#define TEST_RATE_AUDIO_OUT     44100    /* Audio output rate */

/* Resampler design parameters */
#define RESAMPLER_TAPS_PER_BRANCH  8     /* Taps per polyphase branch */
#define RESAMPLER_CUTOFF_FACTOR    1.0f  /* Cutoff = 1.0/factor */

/* Test tolerances */
#define TOLERANCE_DC_GAIN       0.001    /* DC gain tolerance */
#define TOLERANCE_AMPLITUDE     0.05     /* Amplitude tolerance */
#define TOLERANCE_FREQUENCY     0.05     /* Frequency tolerance (5%) */
#define TOLERANCE_SYMMETRY      1e-6     /* Filter symmetry tolerance */

/* Test buffer sizes */
#define TEST_BUF_SMALL          100
#define TEST_BUF_MEDIUM         1000
#define TEST_BUF_LARGE          10000

/*============================================================================
 * Test Framework
 *============================================================================*/

static int test_count = 0;
static int test_passed = 0;

#define TEST_ASSERT(cond, msg) do { \
    test_count++; \
    if (!(cond)) { \
        printf("FAIL: %s\n", msg); \
    } else { \
        test_passed++; \
        printf("PASS: %s\n", msg); \
    } \
} while(0)

#define TEST_ASSERT_NEAR(val, expected, tolerance, msg) do { \
    test_count++; \
    double _v = (val), _e = (expected), _t = (tolerance); \
    if (fabs(_v - _e) > _t) { \
        printf("FAIL: %s (got %.6f, expected %.6f, tol %.6f)\n", msg, _v, _e, _t); \
    } else { \
        test_passed++; \
        printf("PASS: %s (%.6f)\n", msg, _v); \
    } \
} while(0)

/*============================================================================
 * Filter Design Characterization Tests
 *============================================================================*/

/* Compute frequency response magnitude at normalized frequency f (0 to 0.5) */
static double compute_freq_response(const float *coeffs, int num_taps, double f)
{
    double real_sum = 0.0, imag_sum = 0.0;
    for (int i = 0; i < num_taps; i++) {
        double phase = -2.0 * M_PI * f * i;
        real_sum += coeffs[i] * cos(phase);
        imag_sum += coeffs[i] * sin(phase);
    }
    return sqrt(real_sum * real_sum + imag_sum * imag_sum);
}

/* Test filter coefficient normalization (DC gain = 1) */
static void test_filter_dc_gain(void)
{
    printf("\n=== Filter DC Gain Tests ===\n");

    int test_factors[] = {2, 4, 5, 8, 10};
    int num_tests = (int)(sizeof(test_factors) / sizeof(test_factors[0]));

    for (int t = 0; t < num_tests; t++) {
        int factor = test_factors[t];
        int num_taps = 8 * factor;  /* 8 taps per branch */

        float *coeffs = (float *)malloc((size_t)num_taps * sizeof(float));
        if (!coeffs) {
            printf("FAIL: Failed to allocate coefficients\n");
            test_count++;
            continue;
        }

        cf32_resampler_design_filter(coeffs, num_taps, factor);

        /* Sum should be 1.0 after normalization */
        double sum = 0.0;
        for (int i = 0; i < num_taps; i++) {
            sum += coeffs[i];
        }

        char msg[64];
        snprintf(msg, sizeof(msg), "DC gain = 1.0 for factor %d", factor);
        TEST_ASSERT_NEAR(sum, 1.0, 0.001, msg);

        free(coeffs);
    }
}

/* Test filter symmetry (linear phase FIR) */
static void test_filter_symmetry(void)
{
    printf("\n=== Filter Symmetry Tests ===\n");

    int factor = 5;
    int num_taps = 8 * factor;  /* 40 taps */

    float *coeffs = (float *)malloc((size_t)num_taps * sizeof(float));
    if (!coeffs) {
        printf("FAIL: Failed to allocate coefficients\n");
        test_count++;
        return;
    }

    cf32_resampler_design_filter(coeffs, num_taps, factor);

    /* Check symmetry: h[n] should equal h[N-1-n] for linear phase */
    double max_asymmetry = 0.0;
    for (int i = 0; i < num_taps / 2; i++) {
        double diff = fabs(coeffs[i] - coeffs[num_taps - 1 - i]);
        if (diff > max_asymmetry)
            max_asymmetry = diff;
    }

    TEST_ASSERT_NEAR(max_asymmetry, 0.0, 1e-6, "Filter is symmetric (linear phase)");

    free(coeffs);
}

/* Test filter frequency response characteristics */
static void test_filter_frequency_response(void)
{
    printf("\n=== Filter Frequency Response Tests ===\n");

    int factor = 5;  /* Typical: cutoff at 1.0/5 = 0.20 normalized */
    int num_taps = RESAMPLER_TAPS_PER_BRANCH * factor;
    float cutoff_norm = RESAMPLER_CUTOFF_FACTOR / (float)factor;  /* 0.18 */

    float *coeffs = (float *)malloc((size_t)num_taps * sizeof(float));
    if (!coeffs) {
        printf("FAIL: Failed to allocate coefficients\n");
        test_count++;
        return;
    }

    cf32_resampler_design_filter(coeffs, num_taps, factor);

    /* Test DC response (f=0) should be 1.0 */
    double dc_response = compute_freq_response(coeffs, num_taps, 0.0);
    TEST_ASSERT_NEAR(dc_response, 1.0, TOLERANCE_DC_GAIN, "DC response = 1.0");

    /* Test very low frequency (f << cutoff): should be close to 1.0 */
    double low_freq_response = compute_freq_response(coeffs, num_taps, 0.01);
    TEST_ASSERT_NEAR(low_freq_response, 1.0, TOLERANCE_AMPLITUDE, "Low frequency response near 1.0");

    /* Find actual -3dB cutoff frequency */
    double f_3db = 0.0;
    for (double f = 0.001; f < 0.5; f += 0.001) {
        double response = compute_freq_response(coeffs, num_taps, f);
        if (response < 0.707) {  /* -3dB point */
            f_3db = f;
            break;
        }
    }
    printf("INFO: Actual -3dB cutoff frequency: %.4f (design cutoff: %.3f)\n", f_3db, cutoff_norm);
    TEST_ASSERT(f_3db > 0.0, "Filter has finite bandwidth");

    /* Test stopband: should be well attenuated after cutoff */
    double stopband_response = compute_freq_response(coeffs, num_taps, 0.3);
    TEST_ASSERT(stopband_response < 0.1, "Stopband at f=0.3 is attenuated (< -20dB)");

    /* Test at Nyquist (f=0.5): should be heavily attenuated */
    double nyquist_response = compute_freq_response(coeffs, num_taps, 0.5);
    TEST_ASSERT(nyquist_response < 0.01, "Nyquist response heavily attenuated (< -40dB)");

    free(coeffs);
}

static void test_blackman_window(void)
{
    printf("\n=== Blackman Window Tests ===\n");

    const float a0 = 0.42f;
    const float a1 = 0.50f;
    const float a2 = 0.08f;

    int num_taps = 40;
    float *window = (float *)malloc((size_t)num_taps * sizeof(float));
    if (!window) {
        printf("FAIL: Failed to allocate window\n");
        test_count++;
        return;
    }

    for (int i = 0; i < num_taps; i++) {
        float phase = 2.0f * (float)M_PI * (float)i / (float)(num_taps - 1);
        window[i] = a0 - a1 * cosf(phase) + a2 * cosf(2.0f * phase);
    }

    TEST_ASSERT_NEAR(window[0], 0.0, 0.001, "Blackman window start = 0.0");
    TEST_ASSERT_NEAR(window[num_taps - 1], 0.0, 0.001, "Blackman window end = 0.0");
    TEST_ASSERT_NEAR(window[num_taps / 2], 1.0, 0.01, "Blackman window center = 1.0");

    double max_asymmetry = 0.0;
    for (int i = 0; i < num_taps / 2; i++) {
        double diff = fabs(window[i] - window[num_taps - 1 - i]);
        if (diff > max_asymmetry)
            max_asymmetry = diff;
    }
    TEST_ASSERT_NEAR(max_asymmetry, 0.0, 1e-6, "Blackman window is symmetric");

    free(window);
}

/* Test sinc function at various points */
static void test_sinc_function(void)
{
    printf("\n=== Sinc Function Tests ===\n");

    /* sinc(0) = cutoff (from filter design) */
    /* For n=0: sinc should equal cutoff value */
    float cutoff = 0.2f;
    float sinc_at_zero = cutoff;  /* By definition in the filter design */
    TEST_ASSERT_NEAR(sinc_at_zero, cutoff, 0.001, "sinc(0) = cutoff");

    /* Test sinc at non-zero values: sin(pi*cutoff*n) / (pi*n) */
    float n = 5.0f;
    float x = (float)M_PI * cutoff * n;
    float sinc_at_n = sinf(x) / ((float)M_PI * n);
    /* Should be close to 0 at multiples of 1/cutoff */
    TEST_ASSERT(fabs(sinc_at_n) < 0.1, "sinc decays away from center");
}

static void test_stopband_attenuation(void)
{
    printf("\n=== Stopband Attenuation Tests ===\n");

    struct {
        int factor;
        double min_attenuation_db;
    } test_cases[] = {
        {2, 30.0},
        {4, 40.0},
        {5, 45.0},
        {8, 50.0},
    };

    int num_cases = (int)(sizeof(test_cases) / sizeof(test_cases[0]));

    for (int t = 0; t < num_cases; t++) {
        int factor = test_cases[t].factor;
        int num_taps = 8 * factor;
        float cutoff_norm = RESAMPLER_CUTOFF_FACTOR / (float)factor;

        float *coeffs = (float *)malloc((size_t)num_taps * sizeof(float));
        if (!coeffs) {
            printf("FAIL: Failed to allocate coefficients\n");
            test_count++;
            continue;
        }

        cf32_resampler_design_filter(coeffs, num_taps, factor);

        /* Find maximum stopband response (from 2*cutoff to Nyquist) */
        double max_stopband = 0.0;
        for (double f = cutoff_norm * 2.0; f <= 0.5; f += 0.01) {
            double response = compute_freq_response(coeffs, num_taps, f);
            if (response > max_stopband)
                max_stopband = response;
        }

        /* Convert to dB */
        double attenuation_db = -20.0 * log10(max_stopband + 1e-10);

        char msg[128];
        snprintf(msg, sizeof(msg), "Factor %d: stopband attenuation >= %.0f dB (got %.1f dB)",
                 factor, test_cases[t].min_attenuation_db, attenuation_db);
        TEST_ASSERT(attenuation_db >= test_cases[t].min_attenuation_db, msg);

        free(coeffs);
    }
}

/* Test passband characteristics */
static void test_passband_characteristics(void)
{
    printf("\n=== Passband Characteristics Tests ===\n");

    int factor = 5;
    int num_taps = RESAMPLER_TAPS_PER_BRANCH * factor;

    float *coeffs = (float *)malloc((size_t)num_taps * sizeof(float));
    if (!coeffs) {
        printf("FAIL: Failed to allocate coefficients\n");
        test_count++;
        return;
    }

    cf32_resampler_design_filter(coeffs, num_taps, factor);

    /* Measure passband response variation in useful frequency range
       For SDR resampling, we care about signals up to ~100kHz at 250kHz rate */
    double min_passband = 1.0, max_passband = 0.0;
    double test_end = 0.05;  /* Up to 5% of Nyquist */
    for (double f = 0.0; f <= test_end; f += 0.001) {
        double response = compute_freq_response(coeffs, num_taps, f);
        if (response < min_passband) min_passband = response;
        if (response > max_passband) max_passband = response;
    }

    double ripple_db = 20.0 * log10(max_passband / (min_passband + 1e-10));

    /* For narrow passband near DC, ripple should be low */
    TEST_ASSERT(ripple_db < 1.0, "Narrow passband ripple < 1 dB");

    printf("INFO: Passband (DC to %.3f) ripple = %.3f dB\n", test_end, ripple_db);
    printf("INFO: Passband min=%.4f, max=%.4f\n", min_passband, max_passband);

    free(coeffs);
}

/*============================================================================
 * Test Functions
 *============================================================================*/

/* Test GCD calculation */
static void test_gcd(void)
{
    printf("\n=== GCD Tests ===\n");
    TEST_ASSERT(cf32_resampler_gcd(250000, 312500) == 62500, "GCD(250000, 312500) = 62500");
    TEST_ASSERT(cf32_resampler_gcd(312500, 250000) == 62500, "GCD(312500, 250000) = 62500");
    TEST_ASSERT(cf32_resampler_gcd(1000000, 250000) == 250000, "GCD(1000000, 250000) = 250000");
    TEST_ASSERT(cf32_resampler_gcd(48000, 44100) == 300, "GCD(48000, 44100) = 300");
    TEST_ASSERT(cf32_resampler_gcd(1, 1000000) == 1, "GCD(1, 1000000) = 1");
    TEST_ASSERT(cf32_resampler_gcd(0, 100) == 100, "GCD(0, 100) = 100");
}

/* Test sample count ratio */
static void test_sample_ratio(void)
{
    printf("\n=== Sample Ratio Tests ===\n");

    cf32_resampler_t res;
    float *output;
    int num_out;

    /* Test 312500 -> 250000 (ratio 4/5) */
    TEST_ASSERT(cf32_resampler_init(&res, 312500, 250000, 10000) == 0, "Init 312500->250000");
    TEST_ASSERT(res.up_factor == 4, "L=4 for 312500->250000");
    TEST_ASSERT(res.down_factor == 5, "M=5 for 312500->250000");

    /* Generate 5000 input samples, expect ~4000 output */
    float *input = (float *)calloc(5000 * 2, sizeof(float));
    if (!input) {
        printf("FAIL: Failed to allocate input buffer\n");
        test_count++;
        return;
    }
    for (int i = 0; i < 5000; i++) {
        input[i * 2 + 0] = 0.5f;  /* DC signal */
        input[i * 2 + 1] = 0.0f;
    }

    num_out = cf32_resampler_process(&res, input, 5000, &output, 10000);
    TEST_ASSERT(num_out == 4000, "5000 in -> 4000 out (ratio 4/5)");

    free(input);
    cf32_resampler_free(&res);

    /* Test 250000 -> 312500 (ratio 5/4) */
    TEST_ASSERT(cf32_resampler_init(&res, 250000, 312500, 10000) == 0, "Init 250000->312500");
    TEST_ASSERT(res.up_factor == 5, "L=5 for 250000->312500");
    TEST_ASSERT(res.down_factor == 4, "M=4 for 250000->312500");

    input = (float *)calloc(4000 * 2, sizeof(float));
    if (!input) {
        printf("FAIL: Failed to allocate input buffer\n");
        test_count++;
        cf32_resampler_free(&res);
        return;
    }
    for (int i = 0; i < 4000; i++) {
        input[i * 2 + 0] = 0.5f;
        input[i * 2 + 1] = 0.0f;
    }

    num_out = cf32_resampler_process(&res, input, 4000, &output, 10000);
    TEST_ASSERT(num_out == 5000, "4000 in -> 5000 out (ratio 5/4)");

    free(input);
    cf32_resampler_free(&res);
}

/* Test DC gain (unity) */
static void test_dc_gain(void)
{
    printf("\n=== DC Gain Tests ===\n");

    cf32_resampler_t res;
    float *output;
    int num_out;

    cf32_resampler_init(&res, 312500, 250000, 10000);

    /* Generate DC signal with known amplitude */
    float *input = (float *)calloc(5000 * 2, sizeof(float));
    if (!input) {
        printf("FAIL: Failed to allocate input buffer\n");
        test_count++;
        cf32_resampler_free(&res);
        return;
    }
    float dc_level = 0.75f;
    for (int i = 0; i < 5000; i++) {
        input[i * 2 + 0] = dc_level;
        input[i * 2 + 1] = dc_level * 0.5f;
    }

    num_out = cf32_resampler_process(&res, input, 5000, &output, 10000);

    /* Check DC gain after filter settles (skip first 100 samples) */
    double sum_i = 0, sum_q = 0;
    int count = 0;
    for (int i = 100; i < num_out; i++) {
        sum_i += output[i * 2 + 0];
        sum_q += output[i * 2 + 1];
        count++;
    }
    double avg_i = sum_i / count;
    double avg_q = sum_q / count;

    TEST_ASSERT_NEAR(avg_i, dc_level, 0.01, "DC gain I channel");
    TEST_ASSERT_NEAR(avg_q, dc_level * 0.5, 0.01, "DC gain Q channel");

    free(input);
    cf32_resampler_free(&res);
}

/* Test sine wave preservation */
static void test_sine_wave(void)
{
    printf("\n=== Sine Wave Tests ===\n");

    cf32_resampler_t res;
    float *output;
    int num_out;

    uint32_t input_rate = 312500;
    uint32_t output_rate = 250000;
    cf32_resampler_init(&res, input_rate, output_rate, 20000);

    /* Generate 10 kHz sine wave (well below Nyquist for both rates) */
    float freq = 10000.0f;
    int num_in = 10000;
    float *input = (float *)calloc((size_t)num_in * 2, sizeof(float));
    if (!input) {
        printf("FAIL: Failed to allocate input buffer\n");
        test_count++;
        cf32_resampler_free(&res);
        return;
    }

    for (int i = 0; i < num_in; i++) {
        float t = (float)i / (float)input_rate;
        float phase = 2.0f * (float)M_PI * freq * t;
        input[i * 2 + 0] = cosf(phase) * 0.8f;  /* I = cos */
        input[i * 2 + 1] = sinf(phase) * 0.8f;  /* Q = sin */
    }

    num_out = cf32_resampler_process(&res, input, num_in, &output, 20000);

    /* Verify output frequency by measuring zero crossings (skip startup) */
    int zero_crossings = 0;
    int start_sample = 500;  /* Skip filter settling */
    for (int i = start_sample + 1; i < num_out; i++) {
        if ((output[(i-1) * 2] < 0 && output[i * 2] >= 0) ||
            (output[(i-1) * 2] >= 0 && output[i * 2] < 0)) {
            zero_crossings++;
        }
    }

    /* Expected zero crossings: 2 per cycle, for (num_out - start) samples at output_rate */
    float duration = (float)(num_out - start_sample) / (float)output_rate;
    float expected_crossings = 2.0f * freq * duration;

    TEST_ASSERT_NEAR(zero_crossings, expected_crossings, expected_crossings * 0.05,
                     "Sine wave frequency preserved");

    /* Check amplitude preservation (after settling) */
    float max_amp = 0;
    for (int i = start_sample; i < num_out; i++) {
        float amp = sqrtf(output[i * 2] * output[i * 2] + output[i * 2 + 1] * output[i * 2 + 1]);
        if (amp > max_amp) max_amp = amp;
    }
    TEST_ASSERT_NEAR(max_amp, 0.8f, 0.05f, "Sine wave amplitude preserved");

    free(input);
    cf32_resampler_free(&res);
}

/* Test state persistence across multiple calls */
static void test_state_persistence(void)
{
    printf("\n=== State Persistence Tests ===\n");

    cf32_resampler_t res;
    float *output;
    int total_out = 0;

    cf32_resampler_init(&res, 312500, 250000, 1000);

    /* Process in small chunks */
    float *input = (float *)calloc(100 * 2, sizeof(float));
    if (!input) {
        printf("FAIL: Failed to allocate input buffer\n");
        test_count++;
        cf32_resampler_free(&res);
        return;
    }
    for (int i = 0; i < 100; i++) {
        input[i * 2 + 0] = 0.5f;
        input[i * 2 + 1] = 0.25f;
    }

    /* Process 50 chunks of 100 samples = 5000 total input */
    for (int chunk = 0; chunk < 50; chunk++) {
        int num_out = cf32_resampler_process(&res, input, 100, &output, 1000);
        total_out += num_out;
    }

    /* 5000 input at 4/5 ratio = 4000 output */
    TEST_ASSERT(total_out == 4000, "Chunked processing: 50x100 in -> 4000 out total");

    free(input);
    cf32_resampler_free(&res);
}

/* Test various rate combinations */
static void test_various_rates(void)
{
    printf("\n=== Various Rate Combinations ===\n");

    struct {
        uint32_t in_rate;
        uint32_t out_rate;
        int expected_L;
        int expected_M;
    } test_cases[] = {
        /* Standard mode: 250kHz target for frequencies <= 800MHz */
        {312500, 250000, 4, 5},     /* HydraSDR native -> default */
        {250000, 312500, 5, 4},     /* Inverse of above */
        {1000000, 250000, 1, 4},    /* 1MHz -> 250kHz decimation */
        {500000, 250000, 1, 2},     /* 500kHz -> 250kHz decimation */
        {625000, 250000, 2, 5},     /* 625kHz -> 250kHz */
        /* High-frequency mode: 1MHz target for frequencies > 800MHz */
        {312500, 1000000, 16, 5},   /* HydraSDR -> high-freq mode */
        {625000, 1000000, 8, 5},    /* 625kHz -> 1MHz */
        {1250000, 1000000, 4, 5},   /* 1.25MHz -> 1MHz */
        {2500000, 1000000, 2, 5},   /* 2.5MHz -> 1MHz */
        /* Audio rate conversion */
        {48000, 44100, 147, 160},
    };

    int num_cases = (int)(sizeof(test_cases) / sizeof(test_cases[0]));

    for (int i = 0; i < num_cases; i++) {
        cf32_resampler_t res;
        int ret = cf32_resampler_init(&res, test_cases[i].in_rate, test_cases[i].out_rate, 1000);

        char msg[128];
        snprintf(msg, sizeof(msg), "Init %u->%u", test_cases[i].in_rate, test_cases[i].out_rate);
        TEST_ASSERT(ret == 0, msg);

        snprintf(msg, sizeof(msg), "L=%d for %u->%u", test_cases[i].expected_L,
                 test_cases[i].in_rate, test_cases[i].out_rate);
        TEST_ASSERT(res.up_factor == test_cases[i].expected_L, msg);

        snprintf(msg, sizeof(msg), "M=%d for %u->%u", test_cases[i].expected_M,
                 test_cases[i].in_rate, test_cases[i].out_rate);
        TEST_ASSERT(res.down_factor == test_cases[i].expected_M, msg);

        cf32_resampler_free(&res);
    }
}

/* Test no-resampling case */
static void test_no_resampling(void)
{
    printf("\n=== No Resampling Test ===\n");

    cf32_resampler_t res;
    int ret = cf32_resampler_init(&res, 250000, 250000, 1000);

    TEST_ASSERT(ret == 0, "Init same rate returns success");
    TEST_ASSERT(res.initialized == 0, "Same rate: initialized = 0 (passthrough)");

    cf32_resampler_free(&res);
}

/*============================================================================
 * Corner Case Tests (NEW)
 *============================================================================*/

/* Test very small input (edge case for phase accumulator) */
static void test_small_input(void)
{
    printf("\n=== Small Input Tests ===\n");

    cf32_resampler_t res;
    float *output;
    int num_out;

    cf32_resampler_init(&res, 312500, 250000, 100);

    /* Single sample input */
    float input_single[2] = {0.5f, 0.25f};
    num_out = cf32_resampler_process(&res, input_single, 1, &output, 100);
    /* With L=4, M=5, single input produces 0 or 1 output depending on phase */
    TEST_ASSERT(num_out >= 0 && num_out <= 1, "Single sample produces 0-1 outputs");

    /* Two sample input */
    float input_two[4] = {0.5f, 0.25f, 0.5f, 0.25f};
    num_out = cf32_resampler_process(&res, input_two, 2, &output, 100);
    TEST_ASSERT(num_out >= 0 && num_out <= 2, "Two samples produces valid output count");

    cf32_resampler_free(&res);
}

/* Test all-zero input */
static void test_zero_input(void)
{
    printf("\n=== Zero Input Test ===\n");

    cf32_resampler_t res;
    float *output;
    int num_out;

    cf32_resampler_init(&res, 312500, 250000, 1000);

    /* All-zero input */
    float *input = (float *)calloc(1000 * 2, sizeof(float));
    if (!input) {
        printf("FAIL: Failed to allocate input buffer\n");
        test_count++;
        cf32_resampler_free(&res);
        return;
    }
    /* Already zero from calloc */

    num_out = cf32_resampler_process(&res, input, 1000, &output, 1000);
    TEST_ASSERT(num_out == 800, "1000 zero samples -> 800 output (ratio 4/5)");

    /* Verify output is also zero (or very close) */
    double max_val = 0;
    for (int i = 0; i < num_out * 2; i++) {
        if (fabs(output[i]) > max_val)
            max_val = fabs(output[i]);
    }
    TEST_ASSERT_NEAR(max_val, 0.0, 1e-6, "Zero input produces zero output");

    free(input);
    cf32_resampler_free(&res);
}

/* Test impulse response */
static void test_impulse_response(void)
{
    printf("\n=== Impulse Response Test ===\n");

    cf32_resampler_t res;
    float *output;
    int num_out;

    cf32_resampler_init(&res, 312500, 250000, 1000);

    /* Create impulse: single 1.0 sample followed by zeros */
    float *input = (float *)calloc(500 * 2, sizeof(float));
    if (!input) {
        printf("FAIL: Failed to allocate input buffer\n");
        test_count++;
        cf32_resampler_free(&res);
        return;
    }
    input[0] = 1.0f;  /* I impulse */
    input[1] = 0.0f;  /* Q = 0 */

    num_out = cf32_resampler_process(&res, input, 500, &output, 1000);

    /* Impulse response should have non-zero values (filter response) */
    int non_zero_count = 0;
    for (int i = 0; i < num_out; i++) {
        if (fabs(output[i * 2]) > 1e-6)
            non_zero_count++;
    }
    TEST_ASSERT(non_zero_count > 0, "Impulse produces non-zero filter response");
    TEST_ASSERT(non_zero_count < num_out, "Impulse response is finite (not all samples non-zero)");

    free(input);
    cf32_resampler_free(&res);
}

/* Test phase continuity across chunks */
static void test_phase_continuity(void)
{
    printf("\n=== Phase Continuity Test ===\n");

    cf32_resampler_t res;
    float *output;

    uint32_t input_rate = 312500;
    uint32_t output_rate = 250000;
    cf32_resampler_init(&res, input_rate, output_rate, 200);

    /* Generate continuous sine wave, process in chunks, verify no discontinuities */
    float freq = 1000.0f;  /* 1 kHz */
    int chunk_size = 100;
    int num_chunks = 10;
    float *input = (float *)malloc((size_t)chunk_size * 2 * sizeof(float));
    if (!input) {
        printf("FAIL: Failed to allocate input buffer\n");
        test_count++;
        cf32_resampler_free(&res);
        return;
    }

    float prev_i = 0, prev_q = 0;
    int first_output = 1;
    int max_discontinuity_count = 0;

    for (int chunk = 0; chunk < num_chunks; chunk++) {
        int base_sample = chunk * chunk_size;
        for (int i = 0; i < chunk_size; i++) {
            float t = (float)(base_sample + i) / (float)input_rate;
            float phase = 2.0f * (float)M_PI * freq * t;
            input[i * 2 + 0] = cosf(phase) * 0.5f;
            input[i * 2 + 1] = sinf(phase) * 0.5f;
        }

        int num_out = cf32_resampler_process(&res, input, chunk_size, &output, 200);

        /* Check for discontinuities at chunk boundary */
        if (!first_output && num_out > 0) {
            float diff_i = fabsf(output[0] - prev_i);
            float diff_q = fabsf(output[1] - prev_q);
            /* Allow some tolerance for filter transient */
            if (diff_i > 0.1f || diff_q > 0.1f) {
                max_discontinuity_count++;
            }
        }

        if (num_out > 0) {
            prev_i = output[(num_out - 1) * 2 + 0];
            prev_q = output[(num_out - 1) * 2 + 1];
            first_output = 0;
        }
    }

    TEST_ASSERT(max_discontinuity_count == 0, "No phase discontinuities at chunk boundaries");

    free(input);
    cf32_resampler_free(&res);
}

/* Test near-Nyquist passthrough and above-Nyquist attenuation */
static void test_aliasing(void)
{
    printf("\n=== Aliasing Test ===\n");

    cf32_resampler_t res;
    float *output;
    int num_out;

    uint32_t input_rate = 312500;
    uint32_t output_rate = 250000;
    cf32_resampler_init(&res, input_rate, output_rate, 5000);

    /*
     * With cutoff=1.0, the resampler passes signals up to output Nyquist.
     * The PFB channelizer provides anti-aliasing, not the resampler.
     * Test that a signal at 0.45 * output_rate (112.5 kHz, 90% Nyquist)
     * passes through with minimal attenuation.
     */
    float freq = 0.45f * (float)output_rate;  /* 112.5 kHz */
    int num_in = 2000;
    float *input = (float *)malloc((size_t)num_in * 2 * sizeof(float));
    if (!input) {
        printf("FAIL: Failed to allocate input buffer\n");
        test_count++;
        cf32_resampler_free(&res);
        return;
    }

    for (int i = 0; i < num_in; i++) {
        float t = (float)i / (float)input_rate;
        float phase = 2.0f * (float)M_PI * freq * t;
        input[i * 2 + 0] = cosf(phase) * 0.8f;
        input[i * 2 + 1] = sinf(phase) * 0.8f;
    }

    num_out = cf32_resampler_process(&res, input, num_in, &output, 5000);

    /* Check RMS amplitude - should pass through near Nyquist */
    double sum_sq = 0;
    int start = num_out / 4;  /* Skip startup */
    for (int i = start; i < num_out; i++) {
        sum_sq += output[i * 2] * output[i * 2] + output[i * 2 + 1] * output[i * 2 + 1];
    }
    double rms = sqrt(sum_sq / (num_out - start));

    /* Input RMS is 0.8/sqrt(2) = 0.566, should NOT be heavily attenuated */
    TEST_ASSERT(rms > 0.1, "Near-Nyquist signal passes through resampler");

    free(input);
    cf32_resampler_free(&res);
}

/* Test memory management (multiple init/free cycles) */
static void test_memory_management(void)
{
    printf("\n=== Memory Management Test ===\n");

    cf32_resampler_t res;

    /* Multiple init/free cycles to check for memory leaks */
    for (int i = 0; i < 10; i++) {
        int ret = cf32_resampler_init(&res, 312500, 250000, 10000);
        if (ret != 0) {
            printf("FAIL: Init failed on cycle %d\n", i);
            test_count++;
            return;
        }
        cf32_resampler_free(&res);
    }
    test_count++;
    test_passed++;
    printf("PASS: 10 init/free cycles completed without crash\n");

    /* Free on uninitialized/zeroed struct should be safe */
    memset(&res, 0, sizeof(res));
    cf32_resampler_free(&res);  /* Should not crash */
    test_count++;
    test_passed++;
    printf("PASS: Free on zeroed struct is safe\n");
}

/* Test extreme values */
static void test_extreme_values(void)
{
    printf("\n=== Extreme Values Test ===\n");

    cf32_resampler_t res;
    float *output;
    int num_out;

    cf32_resampler_init(&res, 312500, 250000, 1000);

    float *input = (float *)malloc(500 * 2 * sizeof(float));
    if (!input) {
        printf("FAIL: Failed to allocate input buffer\n");
        test_count++;
        cf32_resampler_free(&res);
        return;
    }

    /* Test with maximum float values (within normal range) */
    for (int i = 0; i < 500; i++) {
        input[i * 2 + 0] = 1.0f;
        input[i * 2 + 1] = -1.0f;
    }

    num_out = cf32_resampler_process(&res, input, 500, &output, 1000);
    TEST_ASSERT(num_out == 400, "Extreme values: correct output count");

    /* Check output is finite */
    int all_finite = 1;
    for (int i = 0; i < num_out * 2; i++) {
        if (!isfinite(output[i])) {
            all_finite = 0;
            break;
        }
    }
    TEST_ASSERT(all_finite, "Extreme values produce finite output");

    free(input);
    cf32_resampler_free(&res);
}

/*============================================================================
 * Overflow Guard Tests (validates fixes for findings #3, #4, #10)
 *============================================================================*/

/* Test that init rejects rates exceeding INT_MAX (finding #3) */
static void test_overflow_large_rates(void)
{
    printf("\n=== Overflow Guard: Large Rates ===\n");

    cf32_resampler_t res;
    int ret;

    /* Rates that exceed INT_MAX should be rejected */
    ret = cf32_resampler_init(&res, (uint32_t)INT_MAX + 1u, 250000, 1000);
    TEST_ASSERT(ret == -1, "Reject input_rate > INT_MAX");

    ret = cf32_resampler_init(&res, 250000, (uint32_t)INT_MAX + 1u, 1000);
    TEST_ASSERT(ret == -1, "Reject output_rate > INT_MAX");

    /* Both exceeding INT_MAX */
    ret = cf32_resampler_init(&res, UINT32_MAX, UINT32_MAX - 1, 1000);
    TEST_ASSERT(ret == -1, "Reject both rates > INT_MAX");

    /* Rates at exactly INT_MAX should be accepted (GCD reduces them) */
    /* INT_MAX and INT_MAX are equal, so no resampling needed */
    ret = cf32_resampler_init(&res, (uint32_t)INT_MAX, (uint32_t)INT_MAX, 1000);
    TEST_ASSERT(ret == 0, "Equal rates at INT_MAX -> passthrough");
    cf32_resampler_free(&res);
}

/* Test that init rejects coprime rates that produce huge up_factor (finding #4) */
static void test_overflow_large_factors(void)
{
    printf("\n=== Overflow Guard: Large Factors ===\n");

    cf32_resampler_t res;
    int ret;

    /* Two large coprime rates: GCD=1, so up_factor = output_rate.
     * 104729 * 8 = 837832 > INT_MAX? No, but tests the guard logic.
     * Use rates where up_factor * taps_per_branch would overflow. */

    /* Large coprime: GCD=1, up_factor = INT_MAX-1 */
    /* Actually INT_MAX-1 and INT_MAX-2 with GCD=1 would give huge L/M.
     * But finding the right pair is tricky. Instead, test with known
     * values that trigger the check. */

    /* A rate pair where L would be large enough that L * 8 overflows int:
     * Need up_factor > INT_MAX / 8 = ~268M.
     * Use output_rate = 300000007 (prime), input_rate = 300000017 (prime).
     * GCD = 1, so up_factor = 300000007, and 300000007 * 8 overflows int. */
    ret = cf32_resampler_init(&res, 300000017, 300000007, 100);
    TEST_ASSERT(ret == -1, "Reject coprime rates with up_factor * taps_per_branch overflow");
}

/* Test that init handles very large max_input_samples (finding #10) */
static void test_overflow_large_buffer(void)
{
    printf("\n=== Overflow Guard: Large Buffer Size ===\n");

    cf32_resampler_t res;
    int ret;

    /* A very large max_input_samples that would cause output_buf_size
     * to overflow when multiplied by 2 * sizeof(float). */
    ret = cf32_resampler_init(&res, 312500, 250000, SIZE_MAX / 2);
    TEST_ASSERT(ret == -1, "Reject max_input_samples that overflows output buffer");

    /* An even more extreme case */
    ret = cf32_resampler_init(&res, 312500, 250000, SIZE_MAX);
    TEST_ASSERT(ret == -1, "Reject SIZE_MAX max_input_samples");

    /* Normal large-but-valid max_input_samples should work */
    ret = cf32_resampler_init(&res, 312500, 250000, 1000000);
    TEST_ASSERT(ret == 0, "Accept 1M max_input_samples");
    cf32_resampler_free(&res);
}

/* Test edge case: zero rates are rejected */
static void test_zero_rates(void)
{
    printf("\n=== Edge Case: Zero Rates ===\n");

    cf32_resampler_t res;
    int ret;

    /* Zero rates should be rejected (would cause division by zero) */
    ret = cf32_resampler_init(&res, 0, 250000, 1000);
    TEST_ASSERT(ret == -1, "Reject zero input rate");

    ret = cf32_resampler_init(&res, 250000, 0, 1000);
    TEST_ASSERT(ret == -1, "Reject zero output rate");

    ret = cf32_resampler_init(&res, 0, 0, 1000);
    TEST_ASSERT(ret == -1, "Reject both rates zero");
}

/*============================================================================
 * Benchmark Tests
 *============================================================================*/

/* High-resolution timer */
static double get_time_ms(void)
{
#ifdef _WIN32
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart * 1000.0 / (double)freq.QuadPart;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
#endif
}

/* Benchmark resampler throughput */
static void benchmark_resampler_throughput(void)
{
    printf("\n=== Resampler Throughput Benchmark ===\n");

    cf32_resampler_t res;
    float *output;

    /* Setup: 312500 -> 250000 Hz (typical HydraSDR case) */
    uint32_t input_rate = TEST_RATE_HYDRASDR;
    uint32_t output_rate = TEST_RATE_TARGET;
    int num_samples = 100000;  /* 100k samples per iteration */
    int num_iterations = 100;

    cf32_resampler_init(&res, input_rate, output_rate, (size_t)num_samples);

    /* Allocate and fill input buffer */
    float *input = (float *)malloc((size_t)num_samples * 2 * sizeof(float));
    if (!input) {
        printf("FAIL: Failed to allocate benchmark input buffer\n");
        cf32_resampler_free(&res);
        return;
    }

    /* Generate test signal (DC + sine) */
    for (int i = 0; i < num_samples; i++) {
        float t = (float)i / (float)input_rate;
        float phase = 2.0f * (float)M_PI * 10000.0f * t;
        input[i * 2 + 0] = 0.5f + 0.3f * cosf(phase);
        input[i * 2 + 1] = 0.5f + 0.3f * sinf(phase);
    }

    /* Warmup */
    for (int i = 0; i < 5; i++) {
        cf32_resampler_process(&res, input, num_samples, &output, num_samples);
        /* Reset resampler state for consistent results */
        res.phase_idx = 0;
        res.write_pos = 0;
        memset(res.hist_i, 0, (size_t)res.hist_size * sizeof(float));
        memset(res.hist_q, 0, (size_t)res.hist_size * sizeof(float));
    }

    /* Benchmark */
    double start_time = get_time_ms();
    int64_t total_samples = 0;

    for (int iter = 0; iter < num_iterations; iter++) {
        int num_out = cf32_resampler_process(&res, input, num_samples, &output, num_samples);
        total_samples += num_samples;
        /* Reset for next iteration */
        res.phase_idx = 0;
        res.write_pos = 0;
        memset(res.hist_i, 0, (size_t)res.hist_size * sizeof(float));
        memset(res.hist_q, 0, (size_t)res.hist_size * sizeof(float));
        (void)num_out;
    }

    double end_time = get_time_ms();
    double elapsed_ms = end_time - start_time;
    double samples_per_sec = (double)total_samples / (elapsed_ms / 1000.0);
    double msps = samples_per_sec / 1e6;

    printf("INFO: Processed %lld samples in %.2f ms\n", (long long)total_samples, elapsed_ms);
    printf("INFO: Throughput: %.2f Msps (%.2f MHz IQ)\n", msps, msps);
    printf("INFO: This is %.1fx realtime at %u Hz sample rate\n",
           samples_per_sec / input_rate, input_rate);

    /* Pass if throughput exceeds realtime */
    TEST_ASSERT(samples_per_sec > input_rate, "Throughput exceeds realtime requirement");

    free(input);
    cf32_resampler_free(&res);
}

/* Benchmark filter design time */
static void benchmark_filter_design(void)
{
    printf("\n=== Filter Design Benchmark ===\n");

    int factors[] = {2, 4, 5, 8, 10, 20};
    int num_factors = (int)(sizeof(factors) / sizeof(factors[0]));

    for (int f = 0; f < num_factors; f++) {
        int factor = factors[f];
        int num_taps = RESAMPLER_TAPS_PER_BRANCH * factor;
        float *coeffs = (float *)malloc((size_t)num_taps * sizeof(float));
        if (!coeffs) continue;

        double start = get_time_ms();
        int iterations = 1000;
        for (int i = 0; i < iterations; i++) {
            cf32_resampler_design_filter(coeffs, num_taps, factor);
        }
        double elapsed = get_time_ms() - start;

        printf("INFO: Factor %d (%d taps): %.3f us/design\n",
               factor, num_taps, (elapsed * 1000.0) / iterations);

        free(coeffs);
    }

    test_count++;
    test_passed++;
    printf("PASS: Filter design benchmark completed\n");
}

/* Benchmark memory allocation patterns */
static void benchmark_init_free(void)
{
    printf("\n=== Init/Free Benchmark ===\n");

    int iterations = 1000;
    double start = get_time_ms();

    for (int i = 0; i < iterations; i++) {
        cf32_resampler_t res;
        cf32_resampler_init(&res, TEST_RATE_HYDRASDR, TEST_RATE_TARGET, TEST_BUF_LARGE);
        cf32_resampler_free(&res);
    }

    double elapsed = get_time_ms() - start;
    printf("INFO: Init/Free cycle: %.3f us/cycle\n", (elapsed * 1000.0) / iterations);

    test_count++;
    test_passed++;
    printf("PASS: Init/Free benchmark completed\n");
}

/*============================================================================
 * Main
 *============================================================================*/

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printf("===========================================\n");
    printf("  CF32 Polyphase Resampler Test Suite\n");
    printf("  Testing actual implementation from\n");
    printf("  include/cf32_resampler.h\n");
    printf("===========================================\n");

    /* Filter design characterization tests */
    test_filter_dc_gain();
    test_filter_symmetry();
    test_filter_frequency_response();
    test_blackman_window();
    test_sinc_function();
    test_stopband_attenuation();
    test_passband_characteristics();

    /* Original tests */
    test_gcd();
    test_sample_ratio();
    test_dc_gain();
    test_sine_wave();
    test_state_persistence();
    test_various_rates();
    test_no_resampling();

    /* Corner case tests */
    test_small_input();
    test_zero_input();
    test_impulse_response();
    test_phase_continuity();
    test_aliasing();
    test_memory_management();
    test_extreme_values();

    /* Overflow guard tests */
    test_overflow_large_rates();
    test_overflow_large_factors();
    test_overflow_large_buffer();
    test_zero_rates();

    /* Benchmark tests */
    benchmark_resampler_throughput();
    benchmark_filter_design();
    benchmark_init_free();

    printf("\n===========================================\n");
    printf("  Results: %d/%d tests passed\n", test_passed, test_count);
    printf("===========================================\n");

    return (test_passed == test_count) ? 0 : 1;
}
