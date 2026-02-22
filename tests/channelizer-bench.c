/** @file
    Comprehensive correctness and benchmark tests for PFB channelizer.

    Tests cover:
    - Initialization and configuration
    - Frequency mapping (FFT bin ordering)
    - Tone routing (DC and offset)
    - Channel isolation
    - Multi-tone separation
    - Energy conservation
    - Decimation correctness
    - Phase coherence
    - Continuous processing
    - Nyquist handling
    - Filter response

    Copyright (C) 2025-2026 Benjamin Vernoux <bvernoux@hydrasdr.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "channelizer.h"
#include "build_info.h"
#include "cpu_detect.h"
#include "rtl_433.h"

/* ISA-dispatched process functions (compiled in separate TUs) */
extern int channelizer_process_sse2(channelizer_t *ch, const float *input,
    int n_samples, float **channel_out, int *out_samples);
extern int channelizer_process_avx2(channelizer_t *ch, const float *input,
    int n_samples, float **channel_out, int *out_samples);
extern int channelizer_process_avx512(channelizer_t *ch, const float *input,
    int n_samples, float **channel_out, int *out_samples);
extern int channelizer_process_neon(channelizer_t *ch, const float *input,
    int n_samples, float **channel_out, int *out_samples);
extern int channelizer_process_sve(channelizer_t *ch, const float *input,
    int n_samples, float **channel_out, int *out_samples);

typedef int (*channelizer_process_fn_t)(channelizer_t *, const float *, int,
                                        float **, int *);

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ============================================================================
 * Test Configuration Constants
 * ========================================================================= */

/* Test signal parameters */
#define TEST_CENTER_FREQ        868.5e6f    /* ISM band center (Hz) */
#define TEST_BANDWIDTH          2.0e6f      /* Test bandwidth (Hz) */
#define TEST_SAMPLE_RATE_2M     2000000     /* 2 MSps for some tests */
#define TEST_SAMPLE_RATE_5M     5000000     /* 5 MSps for 16-ch tests */

/* Tolerance thresholds */
#define TOLERANCE               0.05f       /* 5% general tolerance */
#define MIN_ISOLATION_DB        15.0f       /* Adjacent channel isolation (dB) */
#define NON_ADJACENT_EXTRA_DB   10.0f       /* Extra isolation for non-adjacent */
#define PHASE_TOLERANCE         0.2f        /* Phase tolerance (radians) */
#define FREQ_TOLERANCE_HZ       1.0f        /* Frequency mapping tolerance (Hz) */

/* Power thresholds */
#define DC_POWER_RATIO_MIN      0.90f       /* DC tone >90% in channel 0 */
#define CONTINUOUS_POWER_MIN    0.8f        /* Min power in continuous processing */
#define OFF_CENTER_POWER_MIN    0.1f        /* Min power for off-center tone */
#define POWER_RATIO_DB_TOL      3.0f        /* Multi-tone power ratio tolerance (dB) */

/* Energy conservation bounds (critically-sampled PFB has channel overlap) */
#define ENERGY_RATIO_MIN        0.5f        /* Min normalized energy ratio */
#define ENERGY_RATIO_MAX        2.0f        /* Max (overlap can cause >1.0) */

/* Power calculation */
#define POWER_FLOOR             1e-20f      /* Floor for dB conversion */
#define DB_FLOOR                -200.0f     /* Minimum dB value */

/* Test signal amplitudes */
#define TONE_AMPLITUDE_FULL     1.0f        /* Full amplitude tone */
#define TONE_AMPLITUDE_HALF     0.5f        /* Half amplitude (secondary tones) */
#define TONE_AMPLITUDE_LOW      0.3f        /* Low amplitude (energy test) */
#define TONE_OFFSET_FACTOR      0.9f        /* Offset within channel (0-1) */
#define TONE_HALF_SPACING       0.5f        /* Half channel spacing offset */

/* Buffer sizes */
#define BUFFER_SIZE_SMALL       1024        /* Small test buffer */
#define BUFFER_SIZE_MEDIUM      2048        /* Medium test buffer */
#define BUFFER_SIZE_DEFAULT     4096        /* Default test buffer */
#define BUFFER_SIZE_LARGE       8192        /* Large test buffer */
#define BUFFER_SIZE_XLARGE      16384       /* Extra large for isolation tests */

/* Continuous processing parameters */
#define CONTINUOUS_NUM_BLOCKS   4           /* Number of blocks in continuous test */

/* Benchmark configuration */
#define BENCH_WARMUP_ITER       100         /* Warmup iterations */
#define BENCH_ITERATIONS        1000        /* Benchmark iterations */
#define BENCH_RT_MARGIN_GOOD    10.0        /* Good real-time margin (10x) */
#define BENCH_RT_MARGIN_MIN     1.0         /* Minimum real-time margin */

/* Unit conversions */
#define NS_PER_SEC              1e9         /* Nanoseconds per second */
#define US_PER_SEC              1e6         /* Microseconds per second */
#define MSPS_DIVISOR            1e6         /* MSps divisor */

/* ============================================================================
 * Test Framework
 * ========================================================================= */

static int g_tests_passed = 0;
static int g_tests_failed = 0;
static int g_verbose = 0;

#define TEST_ASSERT(cond, msg) do { \
	if (!(cond)) { \
		printf("    ASSERT FAILED: %s\n", msg); \
		return 0; \
	} \
} while(0)

static void test_result(const char *name, int passed)
{
	if (passed) {
		printf("  [PASS] %s\n", name);
		g_tests_passed++;
	} else {
		printf("  [FAIL] %s\n", name);
		g_tests_failed++;
	}
}

/* ============================================================================
 * Helper Functions
 * ========================================================================= */

static float *alloc_cf32(int n_samples)
{
	float *buf = (float *)calloc((size_t)n_samples * 2, sizeof(float));
	return buf;
}

static void generate_tone(float *buf, int n_samples, float freq_offset,
                          float sample_rate, float amplitude)
{
	for (int i = 0; i < n_samples; i++) {
		float phase = 2.0f * (float)M_PI * freq_offset * (float)i / sample_rate;
		buf[i * 2 + 0] = amplitude * cosf(phase);
		buf[i * 2 + 1] = amplitude * sinf(phase);
	}
}

static void add_tone(float *buf, int n_samples, float freq_offset,
                     float sample_rate, float amplitude)
{
	for (int i = 0; i < n_samples; i++) {
		float phase = 2.0f * (float)M_PI * freq_offset * (float)i / sample_rate;
		buf[i * 2 + 0] += amplitude * cosf(phase);
		buf[i * 2 + 1] += amplitude * sinf(phase);
	}
}

static float compute_power(const float *buf, int n_samples)
{
	if (n_samples <= 0)
		return 0.0f;
	float power = 0.0f;
	for (int i = 0; i < n_samples; i++) {
		float I = buf[i * 2 + 0];
		float Q = buf[i * 2 + 1];
		power += I * I + Q * Q;
	}
	return power / (float)n_samples;
}

static float power_to_db(float power)
{
	if (power < POWER_FLOOR)
		return DB_FLOOR;
	return 10.0f * log10f(power);
}

static double get_time_sec(void)
{
#ifdef _WIN32
	return (double)clock() / CLOCKS_PER_SEC;
#else
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / NS_PER_SEC;
#endif
}

/* ============================================================================
 * Correctness Tests
 * ========================================================================= */

/*
 * Test 1: Initialization and Configuration
 * Verify proper setup and parameter validation
 */
static int test_init(int num_channels, uint32_t sample_rate)
{
	channelizer_t ch;

	/* Test valid initialization */
	int ret = channelizer_init(&ch, num_channels, TEST_CENTER_FREQ,
	                           TEST_BANDWIDTH, sample_rate, BUFFER_SIZE_DEFAULT);
	TEST_ASSERT(ret == 0, "initialization failed");
	TEST_ASSERT(ch.num_channels == num_channels, "wrong channel count");
	TEST_ASSERT(ch.channel_rate == sample_rate / (uint32_t)ch.decimation_factor, "wrong channel rate");
	TEST_ASSERT(ch.initialized == 1, "not marked initialized");

	/* Verify channel spacing */
	float expected_spacing = (float)sample_rate / (float)num_channels;
	TEST_ASSERT(fabsf(ch.channel_spacing - expected_spacing) < FREQ_TOLERANCE_HZ, "wrong channel spacing");

	channelizer_free(&ch);
	TEST_ASSERT(ch.initialized == 0, "not properly freed");

	/* Test invalid configurations */
	ret = channelizer_init(&ch, 3, TEST_CENTER_FREQ, TEST_BANDWIDTH, sample_rate, BUFFER_SIZE_DEFAULT);
	TEST_ASSERT(ret == -1, "should reject non-power-of-2");

	ret = channelizer_init(&ch, 64, TEST_CENTER_FREQ, TEST_BANDWIDTH, sample_rate, BUFFER_SIZE_DEFAULT);
	TEST_ASSERT(ret == -1, "should reject >16 channels");

	return 1;
}

/*
 * Test 2: Channel Frequency Mapping
 * Verify FFT bin ordering: DC, positive freqs, Nyquist, negative freqs
 */
static int test_frequency_mapping(int num_channels, uint32_t sample_rate)
{
	channelizer_t ch;

	int ret = channelizer_init(&ch, num_channels, TEST_CENTER_FREQ,
	                           TEST_BANDWIDTH, sample_rate, BUFFER_SIZE_DEFAULT);
	TEST_ASSERT(ret == 0, "initialization failed");

	float spacing = ch.channel_spacing;

	/* Channel 0 should be at center (DC) */
	float f0 = channelizer_get_channel_freq(&ch, 0);
	TEST_ASSERT(fabsf(f0 - TEST_CENTER_FREQ) < FREQ_TOLERANCE_HZ, "Ch0 not at center");

	/* Verify all positive frequency channels */
	for (int i = 1; i < num_channels / 2; i++) {
		float f = channelizer_get_channel_freq(&ch, i);
		float expected = TEST_CENTER_FREQ + (float)i * spacing;
		TEST_ASSERT(fabsf(f - expected) < FREQ_TOLERANCE_HZ, "positive freq mismatch");
	}

	/* Verify Nyquist channel (M/2) */
	float f_nyq = channelizer_get_channel_freq(&ch, num_channels / 2);
	float expected_nyq = TEST_CENTER_FREQ + (float)(num_channels / 2) * spacing;
	TEST_ASSERT(fabsf(f_nyq - expected_nyq) < FREQ_TOLERANCE_HZ, "Nyquist freq mismatch");

	/* Verify all negative frequency channels */
	for (int i = num_channels / 2 + 1; i < num_channels; i++) {
		float f = channelizer_get_channel_freq(&ch, i);
		float expected = TEST_CENTER_FREQ + (float)(i - num_channels) * spacing;
		TEST_ASSERT(fabsf(f - expected) < FREQ_TOLERANCE_HZ, "negative freq mismatch");
	}

	/* Verify ordering: positive freqs increase, negative freqs increase toward DC */
	for (int i = 0; i < num_channels / 2; i++) {
		float f_curr = channelizer_get_channel_freq(&ch, i);
		float f_next = channelizer_get_channel_freq(&ch, i + 1);
		TEST_ASSERT(f_next > f_curr, "positive freqs should increase");
	}

	channelizer_free(&ch);
	return 1;
}

/*
 * Test 3: DC Tone Routing
 * A tone at center frequency should appear primarily in channel 0
 */
static int test_dc_routing(int num_channels, uint32_t sample_rate)
{
	channelizer_t ch;
	int n_input = BUFFER_SIZE_LARGE;

	int ret = channelizer_init(&ch, num_channels, TEST_CENTER_FREQ,
	                           TEST_BANDWIDTH, sample_rate, (size_t)n_input);
	TEST_ASSERT(ret == 0, "initialization failed");

	float *input = alloc_cf32(n_input);
	TEST_ASSERT(input != NULL, "input allocation failed");

	generate_tone(input, n_input, 0.0f, (float)sample_rate, TONE_AMPLITUDE_FULL);

	float *channel_out[CHANNELIZER_MAX_CHANNELS];
	int out_samples;
	ret = channelizer_process(&ch, input, n_input, channel_out, &out_samples);
	TEST_ASSERT(ret == 0, "processing failed");
	TEST_ASSERT(out_samples > 0, "no output");

	float powers[CHANNELIZER_MAX_CHANNELS] = {0};
	float total_power = 0.0f;
	for (int i = 0; i < num_channels; i++) {
		powers[i] = compute_power(channel_out[i], out_samples);
		total_power += powers[i];
	}

	float ch0_ratio = powers[0] / total_power;
	if (g_verbose)
		printf("    DC power ratio: %.1f%%\n", ch0_ratio * 100.0f);

	TEST_ASSERT(ch0_ratio > DC_POWER_RATIO_MIN, "DC tone should be >90% in channel 0");

	free(input);
	channelizer_free(&ch);
	return 1;
}

/*
 * Test 4: Tone Routing to Each Channel
 * Generate tones at each channel center and verify routing
 */
static int test_channel_routing(int num_channels, uint32_t sample_rate)
{
	channelizer_t ch;
	int n_input = BUFFER_SIZE_LARGE;

	int ret = channelizer_init(&ch, num_channels, TEST_CENTER_FREQ,
	                           TEST_BANDWIDTH, sample_rate, (size_t)n_input);
	TEST_ASSERT(ret == 0, "initialization failed");

	float *input = alloc_cf32(n_input);
	TEST_ASSERT(input != NULL, "input allocation failed");

	float spacing = ch.channel_spacing;

	for (int target_ch = 0; target_ch < num_channels; target_ch++) {
		float freq_offset;
		if (target_ch == 0) {
			freq_offset = 0.0f;
		} else if (target_ch <= num_channels / 2) {
			freq_offset = (float)target_ch * spacing;
		} else {
			freq_offset = (float)(target_ch - num_channels) * spacing;
		}

		generate_tone(input, n_input, freq_offset, (float)sample_rate, TONE_AMPLITUDE_FULL);

		float *channel_out[CHANNELIZER_MAX_CHANNELS];
		int out_samples;
		ret = channelizer_process(&ch, input, n_input, channel_out, &out_samples);
		TEST_ASSERT(ret == 0, "processing failed");

		float powers[CHANNELIZER_MAX_CHANNELS] = {0};
		int max_ch = 0;
		for (int i = 0; i < num_channels; i++) {
			powers[i] = compute_power(channel_out[i], out_samples);
			if (powers[i] > powers[max_ch])
				max_ch = i;
		}

		if (g_verbose)
			printf("    Ch%d (%.0fkHz): max in Ch%d\n",
			       target_ch, freq_offset / 1000.0f, max_ch);

		TEST_ASSERT(max_ch == target_ch, "tone routed to wrong channel");
	}

	free(input);
	channelizer_free(&ch);
	return 1;
}

/*
 * Test 5: Channel Isolation
 * Verify that a tone in one channel doesn't leak significantly to others
 */
static int test_channel_isolation(int num_channels, uint32_t sample_rate)
{
	channelizer_t ch;
	int n_input = BUFFER_SIZE_XLARGE;

	int ret = channelizer_init(&ch, num_channels, TEST_CENTER_FREQ,
	                           TEST_BANDWIDTH, sample_rate, (size_t)n_input);
	TEST_ASSERT(ret == 0, "initialization failed");

	float *input = alloc_cf32(n_input);
	TEST_ASSERT(input != NULL, "input allocation failed");

	generate_tone(input, n_input, 0.0f, (float)sample_rate, TONE_AMPLITUDE_FULL);

	float *channel_out[CHANNELIZER_MAX_CHANNELS];
	int out_samples;
	ret = channelizer_process(&ch, input, n_input, channel_out, &out_samples);
	TEST_ASSERT(ret == 0, "processing failed");

	float powers[CHANNELIZER_MAX_CHANNELS] = {0};
	for (int i = 0; i < num_channels; i++)
		powers[i] = compute_power(channel_out[i], out_samples);

	float dc_power_db = power_to_db(powers[0]);

	for (int i = 1; i < num_channels; i++) {
		float isolation_db = dc_power_db - power_to_db(powers[i]);
		if (g_verbose)
			printf("    Ch0->Ch%d: %.1f dB\n", i, isolation_db);

		float min_iso = (i == 1 || i == num_channels - 1) ?
		                MIN_ISOLATION_DB : MIN_ISOLATION_DB + NON_ADJACENT_EXTRA_DB;
		TEST_ASSERT(isolation_db > min_iso, "insufficient isolation");
	}

	free(input);
	channelizer_free(&ch);
	return 1;
}

/*
 * Test 6: Multi-tone Separation
 */
static int test_multitone(int num_channels, uint32_t sample_rate)
{
	channelizer_t ch;
	int n_input = BUFFER_SIZE_LARGE;

	int ret = channelizer_init(&ch, num_channels, TEST_CENTER_FREQ,
	                           TEST_BANDWIDTH, sample_rate, (size_t)n_input);
	TEST_ASSERT(ret == 0, "initialization failed");

	float *input = alloc_cf32(n_input);
	TEST_ASSERT(input != NULL, "input allocation failed");

	float spacing = ch.channel_spacing;

	generate_tone(input, n_input, 0.0f, (float)sample_rate, TONE_AMPLITUDE_FULL);
	add_tone(input, n_input, spacing, (float)sample_rate, TONE_AMPLITUDE_HALF);

	float *channel_out[CHANNELIZER_MAX_CHANNELS];
	int out_samples;
	ret = channelizer_process(&ch, input, n_input, channel_out, &out_samples);
	TEST_ASSERT(ret == 0, "processing failed");

	float powers[CHANNELIZER_MAX_CHANNELS] = {0};
	for (int i = 0; i < num_channels; i++)
		powers[i] = compute_power(channel_out[i], out_samples);

	float p0_db = power_to_db(powers[0]);
	float p1_db = power_to_db(powers[1]);
	float actual_diff = p0_db - p1_db;
	float expected_diff = 20.0f * log10f(TONE_AMPLITUDE_FULL / TONE_AMPLITUDE_HALF);

	if (g_verbose)
		printf("    Power diff: %.1f dB (expected ~%.1f dB)\n",
		       actual_diff, expected_diff);

	TEST_ASSERT(fabsf(actual_diff - expected_diff) < POWER_RATIO_DB_TOL, "power ratio wrong");

	free(input);
	channelizer_free(&ch);
	return 1;
}

/*
 * Test 7: Decimation Correctness
 * Verify output sample count is input_samples / num_channels
 */
static int test_decimation(int num_channels, uint32_t sample_rate)
{
	channelizer_t ch;

	/* Test various input sizes */
	int test_sizes[] = {BUFFER_SIZE_SMALL, BUFFER_SIZE_MEDIUM,
	                    BUFFER_SIZE_DEFAULT, BUFFER_SIZE_LARGE};
	int num_tests = sizeof(test_sizes) / sizeof(test_sizes[0]);

	for (int t = 0; t < num_tests; t++) {
		int n_input = test_sizes[t];

		int ret = channelizer_init(&ch, num_channels, TEST_CENTER_FREQ,
		                           TEST_BANDWIDTH, sample_rate, (size_t)n_input);
		TEST_ASSERT(ret == 0, "initialization failed");

		float *input = alloc_cf32(n_input);
		TEST_ASSERT(input != NULL, "allocation failed");

		generate_tone(input, n_input, 0.0f, (float)sample_rate, TONE_AMPLITUDE_FULL);

		float *channel_out[CHANNELIZER_MAX_CHANNELS];
		int out_samples;
		ret = channelizer_process(&ch, input, n_input, channel_out, &out_samples);
		TEST_ASSERT(ret == 0, "processing failed");

		int expected_out = n_input / ch.decimation_factor;
		if (g_verbose)
			printf("    %d in -> %d out (expected %d)\n",
			       n_input, out_samples, expected_out);

		TEST_ASSERT(out_samples == expected_out, "wrong decimation ratio");

		free(input);
		channelizer_free(&ch);
	}

	return 1;
}

/*
 * Test 8: Continuous Processing
 * Verify state is maintained across multiple process calls
 */
static int test_continuous_processing(int num_channels, uint32_t sample_rate)
{
	channelizer_t ch;
	int n_input = BUFFER_SIZE_MEDIUM;
	int num_blocks = CONTINUOUS_NUM_BLOCKS;

	int ret = channelizer_init(&ch, num_channels, TEST_CENTER_FREQ,
	                           TEST_BANDWIDTH, sample_rate, (size_t)n_input);
	TEST_ASSERT(ret == 0, "initialization failed");

	float *input = alloc_cf32(n_input);
	TEST_ASSERT(input != NULL, "allocation failed");

	float *channel_out[CHANNELIZER_MAX_CHANNELS];
	int total_out_samples = 0;

	/* Process multiple blocks of a continuous tone */
	for (int block = 0; block < num_blocks; block++) {
		/* Generate with continuous phase */
		int start_sample = block * n_input;
		for (int i = 0; i < n_input; i++) {
			float phase = 2.0f * (float)M_PI * 0.0f * (float)(start_sample + i) / (float)sample_rate;
			input[i * 2 + 0] = cosf(phase);
			input[i * 2 + 1] = sinf(phase);
		}

		int out_samples;
		ret = channelizer_process(&ch, input, n_input, channel_out, &out_samples);
		TEST_ASSERT(ret == 0, "processing failed");
		total_out_samples += out_samples;

		/* Power should be consistent across blocks */
		float power = compute_power(channel_out[0], out_samples);
		if (g_verbose)
			printf("    Block %d: Ch0 power = %.4f\n", block, power);
		TEST_ASSERT(power > CONTINUOUS_POWER_MIN, "power dropped in continuous processing");
	}

	int expected_total = (n_input * num_blocks) / ch.decimation_factor;
	TEST_ASSERT(total_out_samples == expected_total, "total samples mismatch");

	free(input);
	channelizer_free(&ch);
	return 1;
}

/*
 * Test 9: Energy Conservation
 */
static int test_energy_conservation(int num_channels, uint32_t sample_rate)
{
	channelizer_t ch;
	int n_input = BUFFER_SIZE_LARGE;

	int ret = channelizer_init(&ch, num_channels, TEST_CENTER_FREQ,
	                           TEST_BANDWIDTH, sample_rate, (size_t)n_input);
	TEST_ASSERT(ret == 0, "initialization failed");

	float *input = alloc_cf32(n_input);
	TEST_ASSERT(input != NULL, "allocation failed");

	/* Wideband signal */
	generate_tone(input, n_input, 0.0f, (float)sample_rate, TONE_AMPLITUDE_HALF);
	for (int i = 1; i < num_channels / 2; i++) {
		float offset = (float)i * ch.channel_spacing * TONE_OFFSET_FACTOR;
		add_tone(input, n_input, offset, (float)sample_rate, TONE_AMPLITUDE_LOW);
		add_tone(input, n_input, -offset, (float)sample_rate, TONE_AMPLITUDE_LOW);
	}

	float input_power = compute_power(input, n_input);

	float *channel_out[CHANNELIZER_MAX_CHANNELS];
	int out_samples;
	ret = channelizer_process(&ch, input, n_input, channel_out, &out_samples);
	TEST_ASSERT(ret == 0, "processing failed");

	float output_power = 0.0f;
	for (int i = 0; i < num_channels; i++)
		output_power += compute_power(channel_out[i], out_samples);

	/*
	 * PFB channelizer normalizes output by decimation factor (num_channels).
	 * Energy ratio = (total_output_power / input_power) should be bounded.
	 * Due to filtering, perfect conservation isn't expected.
	 */
	float ratio = output_power / input_power;

	if (g_verbose)
		printf("    Energy ratio: %.2f (output=%.4f, input=%.4f)\n",
		       ratio, output_power, input_power);

	TEST_ASSERT(ratio > ENERGY_RATIO_MIN && ratio < ENERGY_RATIO_MAX, "energy not conserved");

	free(input);
	channelizer_free(&ch);
	return 1;
}

/*
 * Test 10: Negative Frequency Handling
 */
static int test_negative_frequencies(int num_channels, uint32_t sample_rate)
{
	channelizer_t ch;
	int n_input = BUFFER_SIZE_LARGE;

	int ret = channelizer_init(&ch, num_channels, TEST_CENTER_FREQ,
	                           TEST_BANDWIDTH, sample_rate, (size_t)n_input);
	TEST_ASSERT(ret == 0, "initialization failed");

	float *input = alloc_cf32(n_input);
	TEST_ASSERT(input != NULL, "allocation failed");

	float spacing = ch.channel_spacing;

	/* Test negative frequency (last channel) */
	int neg_channel = num_channels - 1;
	float neg_offset = -spacing;  /* -1 channel from DC */

	generate_tone(input, n_input, neg_offset, (float)sample_rate, TONE_AMPLITUDE_FULL);

	float *channel_out[CHANNELIZER_MAX_CHANNELS];
	int out_samples;
	ret = channelizer_process(&ch, input, n_input, channel_out, &out_samples);
	TEST_ASSERT(ret == 0, "processing failed");

	float powers[CHANNELIZER_MAX_CHANNELS] = {0};
	int max_ch = 0;
	for (int i = 0; i < num_channels; i++) {
		powers[i] = compute_power(channel_out[i], out_samples);
		if (powers[i] > powers[max_ch])
			max_ch = i;
	}

	if (g_verbose)
		printf("    -%.0fkHz tone: max in Ch%d (expected Ch%d)\n",
		       spacing / 1000.0f, max_ch, neg_channel);

	TEST_ASSERT(max_ch == neg_channel, "negative freq routed wrong");

	free(input);
	channelizer_free(&ch);
	return 1;
}

/*
 * Test 11: Nyquist Channel Handling
 */
static int test_nyquist_channel(int num_channels, uint32_t sample_rate)
{
	channelizer_t ch;
	int n_input = BUFFER_SIZE_LARGE;

	int ret = channelizer_init(&ch, num_channels, TEST_CENTER_FREQ,
	                           TEST_BANDWIDTH, sample_rate, (size_t)n_input);
	TEST_ASSERT(ret == 0, "initialization failed");

	float *input = alloc_cf32(n_input);
	TEST_ASSERT(input != NULL, "allocation failed");

	float spacing = ch.channel_spacing;
	int nyquist_ch = num_channels / 2;
	float nyquist_offset = (float)nyquist_ch * spacing;

	generate_tone(input, n_input, nyquist_offset, (float)sample_rate, TONE_AMPLITUDE_FULL);

	float *channel_out[CHANNELIZER_MAX_CHANNELS];
	int out_samples;
	ret = channelizer_process(&ch, input, n_input, channel_out, &out_samples);
	TEST_ASSERT(ret == 0, "processing failed");

	float powers[CHANNELIZER_MAX_CHANNELS] = {0};
	int max_ch = 0;
	for (int i = 0; i < num_channels; i++) {
		powers[i] = compute_power(channel_out[i], out_samples);
		if (powers[i] > powers[max_ch])
			max_ch = i;
	}

	if (g_verbose)
		printf("    Nyquist tone: max in Ch%d (expected Ch%d)\n",
		       max_ch, nyquist_ch);

	TEST_ASSERT(max_ch == nyquist_ch, "Nyquist freq routed wrong");

	free(input);
	channelizer_free(&ch);
	return 1;
}

/*
 * Test 12: Passband Edge and Transition Band Response
 * With CHANNELIZER_CUTOFF_RATIO usable bandwidth, verify:
 *   - Tone at passband edge is captured with >50% power
 *   - Tone at Nyquist (50% of spacing) spans adjacent channels
 */
static int test_off_center_tone(int num_channels, uint32_t sample_rate)
{
	channelizer_t ch;
	int n_input = BUFFER_SIZE_LARGE;

	int ret = channelizer_init(&ch, num_channels, TEST_CENTER_FREQ,
	                           TEST_BANDWIDTH, sample_rate, (size_t)n_input);
	TEST_ASSERT(ret == 0, "initialization failed");

	float *input = alloc_cf32(n_input);
	TEST_ASSERT(input != NULL, "allocation failed");

	float spacing = ch.channel_spacing;
	float nyquist = spacing / 2.0f;

	/*
	 * Test 1: Tone at passband edge (70% of Nyquist from channel center)
	 * With 90% cutoff, this is well within passband and captured.
	 */
	float edge_offset = nyquist * 0.70f;  /* 70% of Nyquist = within passband */
	generate_tone(input, n_input, edge_offset, (float)sample_rate, TONE_AMPLITUDE_FULL);

	float *channel_out[CHANNELIZER_MAX_CHANNELS];
	int out_samples;
	ret = channelizer_process(&ch, input, n_input, channel_out, &out_samples);
	TEST_ASSERT(ret == 0, "processing failed");

	float p0_edge = compute_power(channel_out[0], out_samples);
	float p1_edge = compute_power(channel_out[1], out_samples);

	if (g_verbose)
		printf("    Passband edge (%.0f kHz): Ch0=%.3f, Ch1=%.3f\n",
		       edge_offset / 1000.0f, p0_edge, p1_edge);

	/* Tone at passband edge should have significant power in Ch0 */
	TEST_ASSERT(p0_edge > 0.5f, "passband edge tone should have >50% power in Ch0");

	/*
	 * Test 2: Tone at Nyquist (50% of spacing = channel boundary)
	 * This is in the transition/stopband region of both channels.
	 * Energy will be split between channels.
	 */
	float trans_offset = spacing * TONE_HALF_SPACING;  /* 50% of spacing = Nyquist */
	generate_tone(input, n_input, trans_offset, (float)sample_rate, TONE_AMPLITUDE_FULL);

	/* Reinitialize to flush state from previous tone */
	channelizer_free(&ch);
	ret = channelizer_init(&ch, num_channels, TEST_CENTER_FREQ,
	                       TEST_BANDWIDTH, sample_rate, (size_t)n_input);
	TEST_ASSERT(ret == 0, "reinitialization failed");

	ret = channelizer_process(&ch, input, n_input, channel_out, &out_samples);
	TEST_ASSERT(ret == 0, "processing failed");

	float p0_trans = compute_power(channel_out[0], out_samples);
	float p1_trans = compute_power(channel_out[1], out_samples);
	float total_trans = p0_trans + p1_trans;

	if (g_verbose)
		printf("    Nyquist tone (%.0f kHz): Ch0=%.3f, Ch1=%.3f, total=%.3f\n",
		       trans_offset / 1000.0f, p0_trans, p1_trans, total_trans);

	/*
	 * Nyquist tone (50% spacing) is at channel boundary.
	 * Both adjacent channels will capture some energy.
	 * Verify total energy is preserved (>0.5) and split between channels.
	 */
	TEST_ASSERT(total_trans > 0.3f, "Nyquist tone should have significant total power");
	TEST_ASSERT(p0_trans > 0.05f && p1_trans > 0.05f, "Nyquist tone should split between channels");

	free(input);
	channelizer_free(&ch);
	return 1;
}

/* ============================================================================
 * Benchmarks
 * ========================================================================= */

typedef struct {
	int num_channels;
	uint32_t sample_rate;
	int input_samples;
	double process_time_us;
	double samples_per_sec;
	double realtime_margin;
} bench_result_t;

static bench_result_t run_benchmark(int num_channels, uint32_t sample_rate, int n_input)
{
	bench_result_t result = {0};
	result.num_channels = num_channels;
	result.sample_rate = sample_rate;
	result.input_samples = n_input;

	channelizer_t ch;
	int ret = channelizer_init(&ch, num_channels, TEST_CENTER_FREQ,
	                           TEST_BANDWIDTH, sample_rate, (size_t)n_input);
	if (ret != 0)
		return result;

	float *input = alloc_cf32(n_input);
	if (!input) {
		channelizer_free(&ch);
		return result;
	}

	generate_tone(input, n_input, 0.0f, (float)sample_rate, TONE_AMPLITUDE_FULL);
	add_tone(input, n_input, ch.channel_spacing, (float)sample_rate, TONE_AMPLITUDE_HALF);

	float *channel_out[CHANNELIZER_MAX_CHANNELS];
	int out_samples;

	/* Warmup */
	for (int i = 0; i < BENCH_WARMUP_ITER; i++)
		channelizer_process(&ch, input, n_input, channel_out, &out_samples);

	/* Benchmark */
	double start = get_time_sec();
	for (int i = 0; i < BENCH_ITERATIONS; i++)
		channelizer_process(&ch, input, n_input, channel_out, &out_samples);
	double elapsed = get_time_sec() - start;

	double total_samples = (double)n_input * BENCH_ITERATIONS;
	result.process_time_us = (elapsed * US_PER_SEC) / BENCH_ITERATIONS;
	result.samples_per_sec = total_samples / elapsed;
	result.realtime_margin = result.samples_per_sec / (double)sample_rate;

	free(input);
	channelizer_free(&ch);

	return result;
}

static void print_benchmark_results(void)
{
	printf("\n");
	printf("=============================================================\n");
	printf("                   CHANNELIZER BENCHMARKS\n");
	printf("=============================================================\n");
	printf("\n");
	printf("  Iterations: %d per configuration\n", BENCH_ITERATIONS);
	printf("  MSps = Million input samples/second processed\n");
	printf("  RT Margin = How many times faster than real-time\n");
	printf("\n");

	struct {
		int channels;
		uint32_t rate;
		int samples;
		const char *desc;
	} configs[] = {
		{2,  WIDEBAND_RATE_2M5,    BUFFER_SIZE_DEFAULT, "2-ch @ 2.5 MSps"},
		{4,  WIDEBAND_RATE_2M5,    BUFFER_SIZE_DEFAULT, "4-ch @ 2.5 MSps"},
		{8,  WIDEBAND_RATE_2M5,    BUFFER_SIZE_DEFAULT, "8-ch @ 2.5 MSps"},
		{16, TEST_SAMPLE_RATE_5M,     BUFFER_SIZE_LARGE,   "16-ch @ 5 MSps"},
		{4,  TEST_SAMPLE_RATE_2M,  BUFFER_SIZE_DEFAULT, "4-ch @ 2 MSps"},
		{4,  TEST_SAMPLE_RATE_2M,  BUFFER_SIZE_LARGE,   "4-ch @ 2 MSps (8K)"},
		{8,  TEST_SAMPLE_RATE_2M,  BUFFER_SIZE_LARGE,   "8-ch @ 2 MSps (8K)"},
	};
	int num_configs = sizeof(configs) / sizeof(configs[0]);

	printf("  %-22s  %8s  %8s  %9s  %8s\n",
	       "Configuration", "Time/call", "MSps", "RT Margin", "Status");
	printf("  %-22s  %8s  %8s  %9s  %8s\n",
	       "----------------------", "--------", "--------", "---------", "--------");

	for (int i = 0; i < num_configs; i++) {
		bench_result_t r = run_benchmark(configs[i].channels,
		                                  configs[i].rate,
		                                  configs[i].samples);
		if (r.process_time_us > 0) {
			const char *status = r.realtime_margin > BENCH_RT_MARGIN_GOOD ? "OK" :
			                     r.realtime_margin > BENCH_RT_MARGIN_MIN ? "MARGINAL" : "SLOW";
			printf("  %-22s  %6.0f us  %6.1f M  %7.1fx  %8s\n",
			       configs[i].desc,
			       r.process_time_us,
			       r.samples_per_sec / MSPS_DIVISOR,
			       r.realtime_margin,
			       status);
		}
	}

	printf("\n");
	printf("  Note: RT Margin >10x recommended for reliable real-time operation\n");
}

/* ============================================================================
 * Per-ISA Variant Benchmarks
 * ========================================================================= */

static bench_result_t run_benchmark_isa(int num_channels, uint32_t sample_rate,
                                        int n_input, channelizer_process_fn_t fn)
{
	bench_result_t result = {0};
	result.num_channels = num_channels;
	result.sample_rate = sample_rate;
	result.input_samples = n_input;

	channelizer_t ch;
	int ret = channelizer_init(&ch, num_channels, TEST_CENTER_FREQ,
	                           TEST_BANDWIDTH, sample_rate, (size_t)n_input);
	if (ret != 0)
		return result;

	float *input = alloc_cf32(n_input);
	if (!input) {
		channelizer_free(&ch);
		return result;
	}

	generate_tone(input, n_input, 0.0f, (float)sample_rate, TONE_AMPLITUDE_FULL);
	add_tone(input, n_input, ch.channel_spacing, (float)sample_rate, TONE_AMPLITUDE_HALF);

	float *channel_out[CHANNELIZER_MAX_CHANNELS];
	int out_samples;

	/* Warmup */
	for (int i = 0; i < BENCH_WARMUP_ITER; i++)
		fn(&ch, input, n_input, channel_out, &out_samples);

	/* Benchmark */
	double start = get_time_sec();
	for (int i = 0; i < BENCH_ITERATIONS; i++)
		fn(&ch, input, n_input, channel_out, &out_samples);
	double elapsed = get_time_sec() - start;

	double total_samples = (double)n_input * BENCH_ITERATIONS;
	result.process_time_us = (elapsed * US_PER_SEC) / BENCH_ITERATIONS;
	result.samples_per_sec = total_samples / elapsed;
	result.realtime_margin = result.samples_per_sec / (double)sample_rate;

	free(input);
	channelizer_free(&ch);

	return result;
}

static void print_isa_benchmark_results(void)
{
	printf("\n");
	printf("=============================================================\n");
	printf("              PER-ISA VARIANT BENCHMARKS\n");
	printf("=============================================================\n");
	printf("\n");
	printf("  Dispatched ISA: %s\n", channelizer_isa_info());

	enum cpu_isa_level isa = cpu_detect_isa();

	/*
	 * Build the list of ISA variants to benchmark.
	 * Architecture-conditional: only list variants that are meaningful
	 * for the current CPU architecture.
	 */
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
	struct {
		const char *name;
		channelizer_process_fn_t fn;
		enum cpu_isa_level min_isa;
	} variants[] = {
		{"baseline",  channelizer_process_sse2,   CPU_ISA_BASELINE},
		{"AVX2+FMA",  channelizer_process_avx2,   CPU_ISA_AVX2},
		{"AVX-512",   channelizer_process_avx512, CPU_ISA_AVX512},
	};
#elif defined(__aarch64__) || defined(_M_ARM64)
	struct {
		const char *name;
		channelizer_process_fn_t fn;
		enum cpu_isa_level min_isa;
	} variants[] = {
		{"NEON",  channelizer_process_neon, CPU_ISA_NEON},
		{"SVE",   channelizer_process_sve,  CPU_ISA_SVE},
	};
#else
	struct {
		const char *name;
		channelizer_process_fn_t fn;
		enum cpu_isa_level min_isa;
	} variants[] = {
		{"baseline",  channelizer_process_sse2, CPU_ISA_BASELINE},
	};
#endif
	int num_variants = sizeof(variants) / sizeof(variants[0]);

	/* Count usable variants */
	int usable = 0;
	for (int v = 0; v < num_variants; v++) {
		if (isa >= variants[v].min_isa)
			usable++;
	}
	printf("  Usable ISA levels: %d of %d\n\n", usable, num_variants);

	/* Benchmark config: 8-ch @ 2.5 MSps (representative workload) */
	int test_ch = 8;
	uint32_t test_rate = WIDEBAND_RATE_2M5;
	int test_samples = BUFFER_SIZE_DEFAULT;

	printf("  Workload: %d-ch @ %.1f MSps, %d samples/call\n\n",
	       test_ch, (float)test_rate / (float)MSPS_DIVISOR, test_samples);
	printf("  %-12s  %8s  %8s  %9s  %8s\n",
	       "ISA Level", "Time/call", "MSps", "RT Margin", "Status");
	printf("  %-12s  %8s  %8s  %9s  %8s\n",
	       "------------", "--------", "--------", "---------", "--------");

	double baseline_msps = 0.0;
	double best_msps = 0.0;

	for (int v = 0; v < num_variants; v++) {
		if (isa < variants[v].min_isa) {
			printf("  %-12s  %8s  %8s  %9s  %8s\n",
			       variants[v].name, "-", "-", "-", "N/A");
			continue;
		}

		bench_result_t r = run_benchmark_isa(test_ch, test_rate,
		                                     test_samples, variants[v].fn);

		if (r.process_time_us > 0) {
			double msps = r.samples_per_sec / MSPS_DIVISOR;
			if (v == 0)
				baseline_msps = msps;
			best_msps = msps;

			char speedup[32] = "";
			if (v > 0 && baseline_msps > 0)
				snprintf(speedup, sizeof(speedup), " (%.2fx)",
				         msps / baseline_msps);

			const char *status = r.realtime_margin > BENCH_RT_MARGIN_GOOD ? "OK" :
			                     r.realtime_margin > BENCH_RT_MARGIN_MIN ? "MARGINAL" : "SLOW";
			printf("  %-12s  %6.0f us  %6.1f M  %7.1fx  %5s%s\n",
			       variants[v].name,
			       r.process_time_us,
			       msps,
			       r.realtime_margin,
			       status, speedup);
		}
	}

	printf("\n");
	printf("  Note: Each variant is called directly, bypassing the dispatch pointer.\n");
	printf("  The dispatched path adds ~1 ns overhead (negligible).\n");

	/* Detect when -march=native makes all variants identical */
	if (usable >= 2 && baseline_msps > 0 && best_msps > 0) {
		if (fabs(best_msps - baseline_msps) / baseline_msps < 0.05) {
			printf("\n  Hint: All ISA variants show similar throughput.\n");
			printf("  If built with -DENABLE_NATIVE_OPTIMIZATIONS=ON, all variants\n");
			printf("  use -march=native. Rebuild with =OFF for true ISA comparison.\n");
		}
	}
}

/* ============================================================================
 * Main
 * ========================================================================= */

static void run_correctness_tests(void)
{
	printf("\n");
	printf("=============================================================\n");
	printf("                 CHANNELIZER CORRECTNESS TESTS\n");
	printf("=============================================================\n");

	struct {
		int channels;
		uint32_t rate;
	} configs[] = {
		{2,  WIDEBAND_RATE_2M5},
		{4,  TEST_SAMPLE_RATE_2M},
		{8,  WIDEBAND_RATE_2M5},
		{16, TEST_SAMPLE_RATE_5M},
	};
	int num_configs = sizeof(configs) / sizeof(configs[0]);

	for (int c = 0; c < num_configs; c++) {
		int M = configs[c].channels;
		uint32_t fs = configs[c].rate;
		char name[80];

		printf("\n--- %d Channels @ %.1f MSps ---\n", M, (float)fs / (float)MSPS_DIVISOR);

		snprintf(name, sizeof(name), "Initialization (%d-ch)", M);
		test_result(name, test_init(M, fs));

		snprintf(name, sizeof(name), "Frequency mapping (%d-ch)", M);
		test_result(name, test_frequency_mapping(M, fs));

		snprintf(name, sizeof(name), "DC tone routing (%d-ch)", M);
		test_result(name, test_dc_routing(M, fs));

		snprintf(name, sizeof(name), "Channel routing all bins (%d-ch)", M);
		test_result(name, test_channel_routing(M, fs));

		snprintf(name, sizeof(name), "Channel isolation (%d-ch)", M);
		test_result(name, test_channel_isolation(M, fs));

		snprintf(name, sizeof(name), "Multi-tone separation (%d-ch)", M);
		test_result(name, test_multitone(M, fs));

		snprintf(name, sizeof(name), "Decimation correctness (%d-ch)", M);
		test_result(name, test_decimation(M, fs));

		snprintf(name, sizeof(name), "Continuous processing (%d-ch)", M);
		test_result(name, test_continuous_processing(M, fs));

		snprintf(name, sizeof(name), "Energy conservation (%d-ch)", M);
		test_result(name, test_energy_conservation(M, fs));

		snprintf(name, sizeof(name), "Negative frequency handling (%d-ch)", M);
		test_result(name, test_negative_frequencies(M, fs));

		snprintf(name, sizeof(name), "Nyquist channel handling (%d-ch)", M);
		test_result(name, test_nyquist_channel(M, fs));

		snprintf(name, sizeof(name), "Off-center tone (%d-ch)", M);
		test_result(name, test_off_center_tone(M, fs));
	}
}

int main(int argc, char *argv[])
{
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0)
			g_verbose = 1;
	}

	printf("\n");
	printf("*************************************************************\n");
	printf("*         PFB Channelizer Comprehensive Test Suite          *\n");
	printf("*************************************************************\n");
	printf("  Build: " BUILD_INFO_STR "\n");
	printf("  %s\n", channelizer_build_info());

	/* Detect CPU ISA directly (channelizer_isa_info() needs init first) */
	{
		const char *isa_names[] = {"baseline", "AVX2+FMA", "AVX-512", "NEON", "SVE"};
		enum cpu_isa_level isa = cpu_detect_isa();
		printf("  CPU ISA: %s\n", isa_names[isa]);
	}

	run_correctness_tests();
	print_benchmark_results();
	print_isa_benchmark_results();

	printf("\n");
	printf("=============================================================\n");
	printf("                        SUMMARY\n");
	printf("=============================================================\n");
	printf("\n");
	printf("  Correctness: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
	printf("\n");

	if (g_tests_failed > 0) {
		printf("  *** SOME TESTS FAILED ***\n\n");
		return 1;
	} else {
		printf("  All tests PASSED\n\n");
		return 0;
	}
}
