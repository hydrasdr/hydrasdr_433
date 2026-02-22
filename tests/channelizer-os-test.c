/** @file
    Validation tests for 2x oversampled PFB channelizer (OS-PFB).

    Verifies that the OS-PFB (D=M/2) eliminates dead zones between channels
    where the critically-sampled PFB had transition-band gaps.

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
#include "channelizer.h"
#include "rtl_433.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*===========================================================================
 * Test Configuration
 *===========================================================================*/

#define TEST_CENTER_FREQ     868.5e6f
#define TEST_BANDWIDTH       2.0e6f
#define TEST_SAMPLE_RATE     WIDEBAND_RATE_2M5  /* 2500000 Hz */
#define TEST_NUM_CHANNELS    8
#define TEST_INPUT_SAMPLES   16384

/*===========================================================================
 * Test Utilities
 *===========================================================================*/

static int tests_run = 0;
static int tests_passed = 0;

#define TEST_ASSERT(cond, msg) do { \
	tests_run++; \
	if (!(cond)) { \
		fprintf(stderr, "  FAIL: %s\n", msg); \
		return 1; \
	} else { \
		tests_passed++; \
	} \
} while (0)

/**
 * Generate a complex sinusoid at specified frequency offset from center.
 */
static void generate_tone(float *buf, int n_samples, float freq_offset,
                          float sample_rate, float amplitude)
{
	for (int i = 0; i < n_samples; i++) {
		float phase = 2.0f * (float)M_PI * freq_offset * (float)i / sample_rate;
		buf[i * 2 + 0] = amplitude * cosf(phase);
		buf[i * 2 + 1] = amplitude * sinf(phase);
	}
}

/**
 * Compute average power of CF32 signal.
 */
static float compute_power(const float *buf, int n_samples)
{
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
	if (power < 1e-20f)
		return -200.0f;
	return 10.0f * log10f(power);
}

/*===========================================================================
 * Test 1: Gap Coverage
 *
 * Tones at channel boundaries must be captured by at least one channel
 * above -3 dB relative to an on-center tone.
 *===========================================================================*/

static int test_gap_coverage(void)
{
	channelizer_t ch;
	float *input;
	float *channel_out[CHANNELIZER_MAX_CHANNELS];
	int out_samples;
	int ret;
	int result = 1;

	printf("\nTest 1: Gap Coverage (boundary tones above -3 dB)\n");
	printf("===================================================\n");

	input = (float *)calloc(TEST_INPUT_SAMPLES * 2, sizeof(float));
	if (!input)
		return 1;

	/* Get reference power at DC (channel 0 center) */
	ret = channelizer_init(&ch, TEST_NUM_CHANNELS, TEST_CENTER_FREQ,
	                       TEST_BANDWIDTH, TEST_SAMPLE_RATE, TEST_INPUT_SAMPLES);
	if (ret != 0) {
		free(input);
		return 1;
	}

	generate_tone(input, TEST_INPUT_SAMPLES, 0.0f, (float)TEST_SAMPLE_RATE, 1.0f);
	channelizer_process(&ch, input, TEST_INPUT_SAMPLES, channel_out, &out_samples);
	float ref_power = compute_power(channel_out[0], out_samples);
	float ref_db = power_to_db(ref_power);
	channelizer_free(&ch);

	printf("  Reference power (DC): %.1f dB\n", ref_db);

	/* Test tones at each channel boundary (midpoint between adjacent centers).
	 * Channel spacing = fs/M = 312.5 kHz. Boundary at +/-156.25 kHz from center. */
	float spacing = (float)TEST_SAMPLE_RATE / (float)TEST_NUM_CHANNELS;
	int boundaries_ok = 0;
	int boundaries_total = 0;

	/* Test boundaries between channels 0-1, 1-2, 2-3, 3-4 (positive side) */
	for (int b = 0; b < TEST_NUM_CHANNELS / 2; b++) {
		float boundary_offset = ((float)b + 0.5f) * spacing;

		channelizer_free(&ch);
		ret = channelizer_init(&ch, TEST_NUM_CHANNELS, TEST_CENTER_FREQ,
		                       TEST_BANDWIDTH, TEST_SAMPLE_RATE, TEST_INPUT_SAMPLES);
		if (ret != 0)
			goto cleanup;

		memset(input, 0, TEST_INPUT_SAMPLES * 2 * sizeof(float));
		generate_tone(input, TEST_INPUT_SAMPLES, boundary_offset,
		              (float)TEST_SAMPLE_RATE, 1.0f);
		channelizer_process(&ch, input, TEST_INPUT_SAMPLES, channel_out, &out_samples);

		/* Find the best channel for this boundary tone */
		float best_db = -200.0f;
		int best_chan = -1;
		for (int c = 0; c < TEST_NUM_CHANNELS; c++) {
			float pdb = power_to_db(compute_power(channel_out[c], out_samples));
			if (pdb > best_db) {
				best_db = pdb;
				best_chan = c;
			}
		}

		float delta = best_db - ref_db;
		int ok = (delta > -3.0f);
		boundaries_total++;
		if (ok) boundaries_ok++;

		printf("  Boundary +%.1f kHz: best Ch%d at %.1f dB (%+.1f dB) %s\n",
		       boundary_offset / 1000.0f, best_chan, best_db, delta,
		       ok ? "OK" : "DEAD ZONE!");
	}

	char msg[128];
	snprintf(msg, sizeof(msg), "All %d boundaries above -3 dB (%d/%d)",
	         boundaries_total, boundaries_ok, boundaries_total);
	TEST_ASSERT(boundaries_ok == boundaries_total, msg);

	printf("  PASS\n");
	result = 0;

cleanup:
	free(input);
	channelizer_free(&ch);
	return result;
}

/*===========================================================================
 * Test 2: Full Band Sweep
 *
 * Sweep every 5 kHz from 0 to fs/2. At least one channel must capture
 * each tone above -6 dB.
 *===========================================================================*/

static int test_full_band_sweep(void)
{
	channelizer_t ch;
	float *input;
	float *channel_out[CHANNELIZER_MAX_CHANNELS];
	int out_samples;
	int ret;
	int result = 1;

	printf("\nTest 2: Full Band Sweep (every 5 kHz, >-6 dB)\n");
	printf("=================================================\n");

	input = (float *)calloc(TEST_INPUT_SAMPLES * 2, sizeof(float));
	if (!input)
		return 1;

	/* Get reference at DC */
	ret = channelizer_init(&ch, TEST_NUM_CHANNELS, TEST_CENTER_FREQ,
	                       TEST_BANDWIDTH, TEST_SAMPLE_RATE, TEST_INPUT_SAMPLES);
	if (ret != 0) {
		free(input);
		return 1;
	}
	generate_tone(input, TEST_INPUT_SAMPLES, 0.0f, (float)TEST_SAMPLE_RATE, 1.0f);
	channelizer_process(&ch, input, TEST_INPUT_SAMPLES, channel_out, &out_samples);
	float ref_db = power_to_db(compute_power(channel_out[0], out_samples));
	channelizer_free(&ch);

	float nyquist = (float)TEST_SAMPLE_RATE / 2.0f;
	float step = 5000.0f;  /* 5 kHz steps */
	int sweep_ok = 0;
	int sweep_total = 0;
	float worst_delta = 0.0f;
	float worst_freq = 0.0f;

	/* Sweep from 5 kHz to Nyquist - 5 kHz (skip DC and exact Nyquist) */
	for (float offset = step; offset < nyquist - step; offset += step) {
		ret = channelizer_init(&ch, TEST_NUM_CHANNELS, TEST_CENTER_FREQ,
		                       TEST_BANDWIDTH, TEST_SAMPLE_RATE, TEST_INPUT_SAMPLES);
		if (ret != 0)
			goto cleanup;

		memset(input, 0, TEST_INPUT_SAMPLES * 2 * sizeof(float));
		generate_tone(input, TEST_INPUT_SAMPLES, offset,
		              (float)TEST_SAMPLE_RATE, 1.0f);
		channelizer_process(&ch, input, TEST_INPUT_SAMPLES, channel_out, &out_samples);

		float best_db = -200.0f;
		for (int c = 0; c < TEST_NUM_CHANNELS; c++) {
			float pdb = power_to_db(compute_power(channel_out[c], out_samples));
			if (pdb > best_db)
				best_db = pdb;
		}

		float delta = best_db - ref_db;
		sweep_total++;
		if (delta > -6.0f) {
			sweep_ok++;
		} else {
			printf("  GAP at +%.0f kHz: best %.1f dB (%+.1f dB)\n",
			       offset / 1000.0f, best_db, delta);
		}

		if (delta < worst_delta) {
			worst_delta = delta;
			worst_freq = offset;
		}

		channelizer_free(&ch);
	}

	printf("  Swept %d frequencies, %d above -6 dB\n", sweep_total, sweep_ok);
	printf("  Worst: %+.1f dB at +%.0f kHz\n", worst_delta, worst_freq / 1000.0f);

	char msg[128];
	snprintf(msg, sizeof(msg), "All %d sweep points above -6 dB", sweep_total);
	TEST_ASSERT(sweep_ok == sweep_total, msg);

	result = 0;

cleanup:
	free(input);
	channelizer_free(&ch);
	return result;
}

/*===========================================================================
 * Test 3: Output Rate
 *
 * Verify out_samples == 2 * n_samples / M (integer arithmetic).
 *===========================================================================*/

static int test_output_rate(void)
{
	channelizer_t ch;
	float *input;
	float *channel_out[CHANNELIZER_MAX_CHANNELS];
	int out_samples;
	int ret;
	int result = 1;

	printf("\nTest 3: Output Rate (2x oversampled)\n");
	printf("=====================================\n");

	int test_sizes[] = {256, 512, 1024, 4096, 8192, 16384};
	int num_sizes = sizeof(test_sizes) / sizeof(test_sizes[0]);

	for (int t = 0; t < num_sizes; t++) {
		int n = test_sizes[t];

		input = (float *)calloc(n * 2, sizeof(float));
		if (!input) goto cleanup;

		generate_tone(input, n, 0.0f, (float)TEST_SAMPLE_RATE, 1.0f);

		ret = channelizer_init(&ch, TEST_NUM_CHANNELS, TEST_CENTER_FREQ,
		                       TEST_BANDWIDTH, TEST_SAMPLE_RATE, n);
		if (ret != 0) {
			free(input);
			goto cleanup;
		}

		ret = channelizer_process(&ch, input, n, channel_out, &out_samples);

		int D = ch.decimation_factor;
		int expected = n / D;

		printf("  n=%5d: D=%d, expected=%d, got=%d", n, D, expected, out_samples);

		char msg[128];
		snprintf(msg, sizeof(msg), "n=%d: out_samples=%d (expected %d)",
		         n, out_samples, expected);
		TEST_ASSERT(ret == 0 && out_samples == expected, msg);
		printf(" OK\n");

		free(input);
		channelizer_free(&ch);
	}

	/* Verify channel_rate = input_rate / D */
	ret = channelizer_init(&ch, TEST_NUM_CHANNELS, TEST_CENTER_FREQ,
	                       TEST_BANDWIDTH, TEST_SAMPLE_RATE, 1024);
	if (ret != 0)
		goto cleanup;

	int D = ch.decimation_factor;
	uint32_t expected_rate = TEST_SAMPLE_RATE / (uint32_t)D;
	printf("  channel_rate: expected=%u, got=%u (D=%d)\n",
	       expected_rate, ch.channel_rate, D);

	char msg[128];
	snprintf(msg, sizeof(msg), "channel_rate=%u (expected %u)",
	         ch.channel_rate, expected_rate);
	TEST_ASSERT(ch.channel_rate == expected_rate, msg);

	/* Verify D = M/2 */
	snprintf(msg, sizeof(msg), "decimation_factor=%d (expected M/2=%d)",
	         D, TEST_NUM_CHANNELS / 2);
	TEST_ASSERT(D == TEST_NUM_CHANNELS / 2, msg);

	channelizer_free(&ch);
	result = 0;

cleanup:
	return result;
}

/*===========================================================================
 * Test 4: Channel Isolation
 *
 * On-center tones must be >20 dB above non-adjacent channels.
 *===========================================================================*/

static int test_channel_isolation(void)
{
	channelizer_t ch;
	float *input;
	float *channel_out[CHANNELIZER_MAX_CHANNELS];
	int out_samples;
	int ret;
	int result = 1;

	printf("\nTest 4: Channel Isolation (>20 dB non-adjacent)\n");
	printf("=================================================\n");

	input = (float *)calloc(TEST_INPUT_SAMPLES * 2, sizeof(float));
	if (!input)
		return 1;

	/* Test channels 0, 1, 2, 3 (positive side, skip Nyquist ch4) */
	int test_channels[] = {0, 1, 2, 3};
	int num_test = sizeof(test_channels) / sizeof(test_channels[0]);
	int all_ok = 1;

	for (int tc = 0; tc < num_test; tc++) {
		int src_chan = test_channels[tc];

		ret = channelizer_init(&ch, TEST_NUM_CHANNELS, TEST_CENTER_FREQ,
		                       TEST_BANDWIDTH, TEST_SAMPLE_RATE, TEST_INPUT_SAMPLES);
		if (ret != 0)
			goto cleanup;

		float freq_offset = channelizer_get_channel_freq(&ch, src_chan) - TEST_CENTER_FREQ;

		memset(input, 0, TEST_INPUT_SAMPLES * 2 * sizeof(float));
		generate_tone(input, TEST_INPUT_SAMPLES, freq_offset,
		              (float)TEST_SAMPLE_RATE, 1.0f);
		channelizer_process(&ch, input, TEST_INPUT_SAMPLES, channel_out, &out_samples);

		float src_power = compute_power(channel_out[src_chan], out_samples);
		float src_db = power_to_db(src_power);

		printf("  Tone in Ch%d (%.3f MHz): %.1f dB\n",
		       src_chan, channelizer_get_channel_freq(&ch, src_chan) / 1e6f, src_db);

		for (int c = 0; c < TEST_NUM_CHANNELS; c++) {
			if (c == src_chan) continue;

			float other_power = compute_power(channel_out[c], out_samples);
			float other_db = power_to_db(other_power);
			float rejection = src_db - other_db;

			/* Adjacent channels (distance 1) may have some leakage.
			 * Non-adjacent (distance >= 2) must have >20 dB rejection. */
			int distance = abs(c - src_chan);
			if (distance > TEST_NUM_CHANNELS / 2)
				distance = TEST_NUM_CHANNELS - distance;

			if (distance >= 2 && rejection < 20.0f) {
				printf("    Ch%d: %.1f dB (rejection %.1f dB) LEAK!\n",
				       c, other_db, rejection);
				all_ok = 0;
			}
		}

		channelizer_free(&ch);
	}

	TEST_ASSERT(all_ok, "Non-adjacent channel isolation >20 dB");
	printf("  PASS\n");
	result = 0;

cleanup:
	free(input);
	channelizer_free(&ch);
	return result;
}

/*===========================================================================
 * Test 5: Overlap Verification
 *
 * Boundary tones must appear in BOTH adjacent channels at >-6 dB.
 *===========================================================================*/

static int test_overlap_verification(void)
{
	channelizer_t ch;
	float *input;
	float *channel_out[CHANNELIZER_MAX_CHANNELS];
	int out_samples;
	int ret;
	int result = 1;

	printf("\nTest 5: Overlap Verification (boundary in both channels >-6 dB)\n");
	printf("================================================================\n");

	input = (float *)calloc(TEST_INPUT_SAMPLES * 2, sizeof(float));
	if (!input)
		return 1;

	/* Get reference at DC */
	ret = channelizer_init(&ch, TEST_NUM_CHANNELS, TEST_CENTER_FREQ,
	                       TEST_BANDWIDTH, TEST_SAMPLE_RATE, TEST_INPUT_SAMPLES);
	if (ret != 0) {
		free(input);
		return 1;
	}
	generate_tone(input, TEST_INPUT_SAMPLES, 0.0f, (float)TEST_SAMPLE_RATE, 1.0f);
	channelizer_process(&ch, input, TEST_INPUT_SAMPLES, channel_out, &out_samples);
	float ref_db = power_to_db(compute_power(channel_out[0], out_samples));
	channelizer_free(&ch);

	float spacing = (float)TEST_SAMPLE_RATE / (float)TEST_NUM_CHANNELS;
	int overlaps_ok = 0;
	int overlaps_total = 0;

	/* Test boundary between ch0 and ch1, ch1 and ch2, ch2 and ch3 */
	for (int b = 0; b < 3; b++) {
		float boundary_offset = ((float)b + 0.5f) * spacing;
		int chan_lo = b;
		int chan_hi = b + 1;

		ret = channelizer_init(&ch, TEST_NUM_CHANNELS, TEST_CENTER_FREQ,
		                       TEST_BANDWIDTH, TEST_SAMPLE_RATE, TEST_INPUT_SAMPLES);
		if (ret != 0)
			goto cleanup;

		memset(input, 0, TEST_INPUT_SAMPLES * 2 * sizeof(float));
		generate_tone(input, TEST_INPUT_SAMPLES, boundary_offset,
		              (float)TEST_SAMPLE_RATE, 1.0f);
		channelizer_process(&ch, input, TEST_INPUT_SAMPLES, channel_out, &out_samples);

		float power_lo = power_to_db(compute_power(channel_out[chan_lo], out_samples));
		float power_hi = power_to_db(compute_power(channel_out[chan_hi], out_samples));
		float delta_lo = power_lo - ref_db;
		float delta_hi = power_hi - ref_db;

		overlaps_total++;
		int ok = (delta_lo > -6.0f && delta_hi > -6.0f);
		if (ok) overlaps_ok++;

		printf("  Boundary Ch%d/Ch%d (+%.1f kHz): Ch%d=%+.1f dB, Ch%d=%+.1f dB %s\n",
		       chan_lo, chan_hi, boundary_offset / 1000.0f,
		       chan_lo, delta_lo, chan_hi, delta_hi,
		       ok ? "OK" : "FAIL");

		channelizer_free(&ch);
	}

	char msg[128];
	snprintf(msg, sizeof(msg), "All %d boundaries in both channels >-6 dB",
	         overlaps_total);
	TEST_ASSERT(overlaps_ok == overlaps_total, msg);

	printf("  PASS\n");
	result = 0;

cleanup:
	free(input);
	channelizer_free(&ch);
	return result;
}

/*===========================================================================
 * Test 6: Non-aligned Samples
 *
 * Trailing n_samples % D samples must be silently dropped.
 *===========================================================================*/

static int test_non_aligned_samples(void)
{
	channelizer_t ch;
	float *input;
	float *channel_out[CHANNELIZER_MAX_CHANNELS];
	int out_samples;
	int ret;
	int result = 1;

	printf("\nTest 6: Non-aligned Samples (trailing %% D dropped)\n");
	printf("=====================================================\n");

	int M = TEST_NUM_CHANNELS;
	int base = 4096;

	ret = channelizer_init(&ch, M, TEST_CENTER_FREQ, TEST_BANDWIDTH,
	                       TEST_SAMPLE_RATE, base + M);
	if (ret != 0)
		return 1;

	int D = ch.decimation_factor;

	input = (float *)calloc((base + M) * 2, sizeof(float));
	if (!input) {
		channelizer_free(&ch);
		return 1;
	}

	generate_tone(input, base + M, 0.0f, (float)TEST_SAMPLE_RATE, 1.0f);

	/* Aligned reference */
	ret = channelizer_process(&ch, input, base, channel_out, &out_samples);
	if (ret != 0)
		goto cleanup;

	int aligned_out = out_samples;
	int expected = base / D;

	printf("  D=%d, base=%d, expected_out=%d, got=%d\n", D, base, expected, aligned_out);

	char msg[128];
	snprintf(msg, sizeof(msg), "Aligned: %d outputs (expected %d)", aligned_out, expected);
	TEST_ASSERT(aligned_out == expected, msg);

	/* Test each non-zero remainder mod D */
	for (int r = 1; r < D; r++) {
		int n = base + r;

		channelizer_free(&ch);
		ret = channelizer_init(&ch, M, TEST_CENTER_FREQ, TEST_BANDWIDTH,
		                       TEST_SAMPLE_RATE, base + M);
		if (ret != 0)
			goto cleanup;

		ret = channelizer_process(&ch, input, n, channel_out, &out_samples);
		if (ret != 0) {
			fprintf(stderr, "  FAIL: process failed for n=%d\n", n);
			goto cleanup;
		}

		snprintf(msg, sizeof(msg), "n=%d (+%d trailing): %d outputs (expected %d)",
		         n, r, out_samples, aligned_out);
		TEST_ASSERT(out_samples == aligned_out, msg);
		printf("  %s OK\n", msg);
	}

	/* Test n < D: should produce 0 outputs */
	for (int n = 1; n < D; n++) {
		channelizer_free(&ch);
		ret = channelizer_init(&ch, M, TEST_CENTER_FREQ, TEST_BANDWIDTH,
		                       TEST_SAMPLE_RATE, base + M);
		if (ret != 0)
			goto cleanup;

		ret = channelizer_process(&ch, input, n, channel_out, &out_samples);

		snprintf(msg, sizeof(msg), "n=%d (< D=%d): %d outputs (expected 0)",
		         n, D, out_samples);
		TEST_ASSERT(ret == 0 && out_samples == 0, msg);
	}

	printf("  PASS\n");
	result = 0;

cleanup:
	free(input);
	channelizer_free(&ch);
	return result;
}

/*===========================================================================
 * Test 7: Real-world 433 MHz
 *
 * Tones at 433.92 MHz and 434.05 MHz (near channel boundary) must be
 * detected with good power.
 *===========================================================================*/

static int test_realworld_433(void)
{
	channelizer_t ch;
	float *input;
	float *channel_out[CHANNELIZER_MAX_CHANNELS];
	int out_samples;
	int ret;
	int result = 1;

	float center = 433.9e6f;

	printf("\nTest 7: Real-world 433 MHz (boundary signals detected)\n");
	printf("=======================================================\n");

	input = (float *)calloc(TEST_INPUT_SAMPLES * 2, sizeof(float));
	if (!input)
		return 1;

	/* Get reference at center */
	ret = channelizer_init(&ch, TEST_NUM_CHANNELS, center,
	                       TEST_BANDWIDTH, TEST_SAMPLE_RATE, TEST_INPUT_SAMPLES);
	if (ret != 0) {
		free(input);
		return 1;
	}
	generate_tone(input, TEST_INPUT_SAMPLES, 0.0f, (float)TEST_SAMPLE_RATE, 1.0f);
	channelizer_process(&ch, input, TEST_INPUT_SAMPLES, channel_out, &out_samples);
	float ref_db = power_to_db(compute_power(channel_out[0], out_samples));
	channelizer_free(&ch);

	struct {
		float signal_mhz;
		const char *desc;
	} signals[] = {
		{433.920f, "433.92 MHz (telecommand, +20 kHz from Ch0)"},
		{434.050f, "434.05 MHz (near Ch0/Ch1 boundary, +150 kHz)"},
		{433.750f, "433.75 MHz (near Ch0/Ch7 boundary, -150 kHz)"},
		{434.212f, "434.21 MHz (Ch1 center, +312.5 kHz)"},
	};
	int num_signals = sizeof(signals) / sizeof(signals[0]);

	printf("  Center: %.3f MHz, ref=%.1f dB\n\n", center / 1e6f, ref_db);

	int all_detected = 1;

	for (int i = 0; i < num_signals; i++) {
		float signal_hz = signals[i].signal_mhz * 1e6f;
		float offset_hz = signal_hz - center;

		ret = channelizer_init(&ch, TEST_NUM_CHANNELS, center,
		                       TEST_BANDWIDTH, TEST_SAMPLE_RATE, TEST_INPUT_SAMPLES);
		if (ret != 0) {
			all_detected = 0;
			continue;
		}

		memset(input, 0, TEST_INPUT_SAMPLES * 2 * sizeof(float));
		generate_tone(input, TEST_INPUT_SAMPLES, offset_hz,
		              (float)TEST_SAMPLE_RATE, 1.0f);
		channelizer_process(&ch, input, TEST_INPUT_SAMPLES, channel_out, &out_samples);

		/* Find best channel */
		float best_db = -200.0f;
		int best_chan = -1;
		for (int c = 0; c < TEST_NUM_CHANNELS; c++) {
			float pdb = power_to_db(compute_power(channel_out[c], out_samples));
			if (pdb > best_db) {
				best_db = pdb;
				best_chan = c;
			}
		}

		float delta = best_db - ref_db;
		int detected = (delta > -6.0f);
		if (!detected) all_detected = 0;

		printf("  %s\n", signals[i].desc);
		printf("    Best channel: Ch%d, power: %+.1f dB (%+.1f vs ref) %s\n",
		       best_chan, best_db, delta, detected ? "DETECTED" : "MISSED!");

		channelizer_free(&ch);
	}

	char msg[128];
	snprintf(msg, sizeof(msg), "All %d real-world signals detected (>-6 dB)",
	         num_signals);
	TEST_ASSERT(all_detected, msg);

	printf("  PASS\n");
	result = 0;

	free(input);
	return result;
}

/*===========================================================================
 * Test 8: Multi-M Struct Validation
 *
 * Verify decimation_factor, channel_rate, and channel spacing for
 * all supported M values (2, 4, 8, 16).
 *===========================================================================*/

static int test_multi_m_struct(void)
{
	channelizer_t ch;
	int ret;
	int result = 0;

	printf("\nTest 8: Multi-M Struct Validation (M=2,4,8,16)\n");
	printf("=================================================\n");

	struct {
		int M;
		uint32_t sample_rate;
	} configs[] = {
		{2, 1000000},
		{4, 2000000},
		{8, 2500000},
		{16, 5000000},
	};
	int num_configs = sizeof(configs) / sizeof(configs[0]);

	for (int i = 0; i < num_configs; i++) {
		int M = configs[i].M;
		uint32_t sr = configs[i].sample_rate;
		int D = M / 2;
		uint32_t expected_rate = sr / (uint32_t)D;

		ret = channelizer_init(&ch, M, TEST_CENTER_FREQ, TEST_BANDWIDTH,
		                       sr, 1024);
		if (ret != 0) {
			fprintf(stderr, "  FAIL: init failed for M=%d\n", M);
			result = 1;
			continue;
		}

		char msg[128];

		snprintf(msg, sizeof(msg), "M=%d: decimation_factor=%d (expected %d)",
		         M, ch.decimation_factor, D);
		TEST_ASSERT(ch.decimation_factor == D, msg);

		snprintf(msg, sizeof(msg), "M=%d: channel_rate=%u (expected %u)",
		         M, ch.channel_rate, expected_rate);
		TEST_ASSERT(ch.channel_rate == expected_rate, msg);

		snprintf(msg, sizeof(msg), "M=%d: num_channels=%d", M, ch.num_channels);
		TEST_ASSERT(ch.num_channels == M, msg);

		printf("  M=%2d: D=%d, channel_rate=%7u Hz OK\n",
		       M, ch.decimation_factor, ch.channel_rate);

		channelizer_free(&ch);
	}

	if (result == 0)
		printf("  PASS\n");
	return result;
}

/*===========================================================================
 * Test 9: M=2 Edge Case (D=1, degenerate)
 *
 * With M=2, D=1: output rate = input rate (no decimation).
 * Both channels should still separate positive/negative frequencies.
 *===========================================================================*/

static int test_m2_degenerate(void)
{
	channelizer_t ch;
	float *input;
	float *channel_out[CHANNELIZER_MAX_CHANNELS];
	int out_samples;
	int ret;
	int result = 1;
	int fail = 0;

	printf("\nTest 9: M=2 Edge Case (D=1, output rate = input rate)\n");
	printf("=======================================================\n");

	int M = 2;
	uint32_t sr = 1000000;  /* 1 MSps */
	int n = 4096;

	input = (float *)calloc(n * 2, sizeof(float));
	if (!input)
		return 1;

	ret = channelizer_init(&ch, M, TEST_CENTER_FREQ, TEST_BANDWIDTH, sr, n);
	if (ret != 0)
		goto cleanup;

	char msg[128];

	/* D should be 1 */
	tests_run++;
	if (ch.decimation_factor != 1) {
		fprintf(stderr, "  FAIL: M=2: D=%d (expected 1)\n",
		        ch.decimation_factor);
		fail = 1;
		goto cleanup_ch;
	}
	tests_passed++;
	printf("  D=%d (degenerate: no decimation)\n", ch.decimation_factor);

	/* channel_rate = input_rate / 1 = input_rate */
	tests_run++;
	if (ch.channel_rate != sr) {
		fprintf(stderr, "  FAIL: M=2: channel_rate=%u (expected %u)\n",
		        ch.channel_rate, sr);
		fail = 1;
		goto cleanup_ch;
	}
	tests_passed++;

	/* Output samples should equal input samples (D=1) */
	generate_tone(input, n, 0.0f, (float)sr, 1.0f);
	channelizer_process(&ch, input, n, channel_out, &out_samples);

	tests_run++;
	if (out_samples != n) {
		fprintf(stderr, "  FAIL: M=2: out_samples=%d (expected %d)\n",
		        out_samples, n);
		fail = 1;
		goto cleanup_ch;
	}
	tests_passed++;
	printf("  out_samples=%d (equals input, D=1)\n", out_samples);

	/* DC tone should appear in channel 0 with good power */
	float ch0_power = power_to_db(compute_power(channel_out[0], out_samples));
	float ch1_power = power_to_db(compute_power(channel_out[1], out_samples));
	float rejection = ch0_power - ch1_power;

	printf("  DC tone: Ch0=%.1f dB, Ch1=%.1f dB, rejection=%.1f dB\n",
	       ch0_power, ch1_power, rejection);

	snprintf(msg, sizeof(msg), "M=2: DC in Ch0 with >10 dB over Ch1 (got %.1f dB)",
	         rejection);
	TEST_ASSERT(rejection > 10.0f, msg);

	printf("  PASS\n");
	result = 0;

cleanup_ch:
	channelizer_free(&ch);
cleanup:
	free(input);
	return fail ? 1 : result;
}

/*===========================================================================
 * Test 10: M=16 Large Configuration
 *
 * Verify 16-channel channelizer works correctly with OS-PFB.
 *===========================================================================*/

static int test_m16_large(void)
{
	channelizer_t ch;
	float *input;
	float *channel_out[CHANNELIZER_MAX_CHANNELS];
	int out_samples;
	int ret;
	int result = 1;

	printf("\nTest 10: M=16 Large Configuration (D=8)\n");
	printf("==========================================\n");

	int M = 16;
	uint32_t sr = 5000000;  /* 5 MSps */
	int n = 8192;

	input = (float *)calloc(n * 2, sizeof(float));
	if (!input)
		return 1;

	ret = channelizer_init(&ch, M, TEST_CENTER_FREQ, TEST_BANDWIDTH, sr, n);
	if (ret != 0)
		goto cleanup;

	char msg[128];

	/* Verify struct fields */
	tests_run++;
	if (ch.decimation_factor != 8) {
		fprintf(stderr, "  FAIL: M=16: D=%d (expected 8)\n",
		        ch.decimation_factor);
		goto cleanup_ch;
	}
	tests_passed++;

	tests_run++;
	if (ch.channel_rate != sr / 8) {
		fprintf(stderr, "  FAIL: M=16: channel_rate=%u (expected %u)\n",
		        ch.channel_rate, sr / 8);
		goto cleanup_ch;
	}
	tests_passed++;

	/* Verify output count: n / D */
	generate_tone(input, n, 0.0f, (float)sr, 1.0f);
	channelizer_process(&ch, input, n, channel_out, &out_samples);

	int expected_out = n / ch.decimation_factor;
	tests_run++;
	if (out_samples != expected_out) {
		fprintf(stderr, "  FAIL: M=16: out_samples=%d (expected %d)\n",
		        out_samples, expected_out);
		goto cleanup_ch;
	}
	tests_passed++;
	printf("  D=8, out_samples=%d/%d input, channel_rate=%u Hz\n",
	       out_samples, n, ch.channel_rate);

	/* Tone at channel 1 center: should be >20 dB above non-adjacent */
	float spacing = (float)sr / (float)M;
	float tone_offset = spacing;  /* Channel 1 center */

	memset(input, 0, n * 2 * sizeof(float));
	generate_tone(input, n, tone_offset, (float)sr, 1.0f);
	channelizer_process(&ch, input, n, channel_out, &out_samples);

	float ch1_power = power_to_db(compute_power(channel_out[1], out_samples));
	int isolation_ok = 1;

	for (int c = 0; c < M; c++) {
		if (c == 1 || c == 0 || c == 2)
			continue;  /* Skip target and adjacent */

		float pwr = power_to_db(compute_power(channel_out[c], out_samples));
		float rej = ch1_power - pwr;
		if (rej < 20.0f) {
			printf("  LEAK: Ch%d at %.1f dB (rejection %.1f dB)\n",
			       c, pwr, rej);
			isolation_ok = 0;
		}
	}

	snprintf(msg, sizeof(msg),
	         "M=16: Ch1 tone >20 dB above non-adjacent channels");
	TEST_ASSERT(isolation_ok, msg);
	printf("  Ch1 tone: %.1f dB, isolation OK\n", ch1_power);

	printf("  PASS\n");
	result = 0;

cleanup_ch:
	channelizer_free(&ch);
cleanup:
	free(input);
	return result;
}

/*===========================================================================
 * Test 11: Phase Correction Validation
 *
 * Verify that odd channels preserve frequency content correctly
 * (the OS-PFB phase correction negates I/Q for odd ch on odd output index).
 *===========================================================================*/

static int test_phase_correction(void)
{
	channelizer_t ch;
	float *input;
	float *channel_out[CHANNELIZER_MAX_CHANNELS];
	int out_samples;
	int ret;
	int result = 1;

	printf("\nTest 11: Phase Correction (odd/even channel frequency preservation)\n");
	printf("====================================================================\n");

	input = (float *)calloc(TEST_INPUT_SAMPLES * 2, sizeof(float));
	if (!input)
		return 1;

	float spacing = (float)TEST_SAMPLE_RATE / (float)TEST_NUM_CHANNELS;

	/* Test both even channel (Ch0) and odd channel (Ch1).
	 * Without phase correction, odd channels would show a frequency offset
	 * of spacing/2 due to the exp(-j*pi*k*n) rotation. */
	struct {
		int channel;
		float freq_offset;
		const char *desc;
	} tests[] = {
		{0, 0.0f, "Ch0 (even) at DC"},
		{1, spacing, "Ch1 (odd) at +312.5 kHz"},
		{3, 3.0f * spacing, "Ch3 (odd) at +937.5 kHz"},
	};
	int num_tests = sizeof(tests) / sizeof(tests[0]);
	int all_ok = 1;

	for (int t = 0; t < num_tests; t++) {
		ret = channelizer_init(&ch, TEST_NUM_CHANNELS, TEST_CENTER_FREQ,
		                       TEST_BANDWIDTH, TEST_SAMPLE_RATE,
		                       TEST_INPUT_SAMPLES);
		if (ret != 0)
			goto cleanup;

		memset(input, 0, TEST_INPUT_SAMPLES * 2 * sizeof(float));
		generate_tone(input, TEST_INPUT_SAMPLES, tests[t].freq_offset,
		              (float)TEST_SAMPLE_RATE, 1.0f);
		channelizer_process(&ch, input, TEST_INPUT_SAMPLES,
		                    channel_out, &out_samples);

		/* Estimate frequency of channel output by measuring phase rotation
		 * between consecutive samples. A properly corrected channel should
		 * show near-zero frequency (DC in baseband). */
		int ch_idx = tests[t].channel;
		float *out = channel_out[ch_idx];
		double phase_sum = 0.0;
		int skip = 64;  /* Skip startup transient */

		for (int i = skip; i < out_samples - 1; i++) {
			float I0 = out[i * 2 + 0];
			float Q0 = out[i * 2 + 1];
			float I1 = out[(i + 1) * 2 + 0];
			float Q1 = out[(i + 1) * 2 + 1];
			/* Phase difference via complex conjugate multiply */
			float dI = I1 * I0 + Q1 * Q0;
			float dQ = Q1 * I0 - I1 * Q0;
			phase_sum += atan2f(dQ, dI);
		}

		double avg_phase = phase_sum / (double)(out_samples - skip - 1);
		double est_freq = avg_phase * (double)ch.channel_rate / (2.0 * M_PI);
		double tolerance = 5000.0;  /* 5 kHz tolerance */

		int ok = (fabs(est_freq) < tolerance);
		if (!ok) all_ok = 0;

		printf("  %s: est_freq=%.1f Hz (expect ~0 Hz baseband) %s\n",
		       tests[t].desc, est_freq, ok ? "OK" : "FAIL");

		channelizer_free(&ch);
	}

	char msg[128];
	snprintf(msg, sizeof(msg),
	         "Phase correction: all channels show baseband DC");
	TEST_ASSERT(all_ok, msg);

	printf("  PASS\n");
	result = 0;

cleanup:
	free(input);
	channelizer_free(&ch);
	return result;
}

/*===========================================================================
 * Test 12: Error Path Validation
 *
 * Verify graceful handling of invalid inputs.
 *===========================================================================*/

static int test_error_paths(void)
{
	channelizer_t ch;
	int ret;

	printf("\nTest 12: Error Path Validation\n");
	printf("================================\n");

	char msg[128];

	/* Invalid channel count (not power of 2) */
	ret = channelizer_init(&ch, 3, TEST_CENTER_FREQ, TEST_BANDWIDTH,
	                       TEST_SAMPLE_RATE, 1024);
	snprintf(msg, sizeof(msg), "M=3 (not power of 2) returns error");
	TEST_ASSERT(ret != 0, msg);
	printf("  M=3 rejected OK\n");

	/* Too many channels */
	ret = channelizer_init(&ch, 32, TEST_CENTER_FREQ, TEST_BANDWIDTH,
	                       TEST_SAMPLE_RATE, 1024);
	snprintf(msg, sizeof(msg), "M=32 (exceeds max) returns error");
	TEST_ASSERT(ret != 0, msg);
	printf("  M=32 rejected OK\n");

	/* M=1 (invalid, need at least 2) */
	ret = channelizer_init(&ch, 1, TEST_CENTER_FREQ, TEST_BANDWIDTH,
	                       TEST_SAMPLE_RATE, 1024);
	snprintf(msg, sizeof(msg), "M=1 returns error");
	TEST_ASSERT(ret != 0, msg);
	printf("  M=1 rejected OK\n");

	/* Zero sample rate */
	ret = channelizer_init(&ch, 8, TEST_CENTER_FREQ, TEST_BANDWIDTH,
	                       0, 1024);
	snprintf(msg, sizeof(msg), "sample_rate=0 returns error");
	TEST_ASSERT(ret != 0, msg);
	printf("  sample_rate=0 rejected OK\n");

	/* Double-free safety: free on uninitialized struct */
	memset(&ch, 0, sizeof(ch));
	channelizer_free(&ch);
	printf("  Double-free on zeroed struct: no crash OK\n");

	/* Zero input to process */
	ret = channelizer_init(&ch, 8, TEST_CENTER_FREQ, TEST_BANDWIDTH,
	                       TEST_SAMPLE_RATE, 1024);
	if (ret == 0) {
		float *channel_out[CHANNELIZER_MAX_CHANNELS];
		int out_samples = -1;
		float dummy[2] = {0};

		ret = channelizer_process(&ch, dummy, 0, channel_out, &out_samples);
		snprintf(msg, sizeof(msg), "n_samples=0: out=%d (expected 0)",
		         out_samples);
		TEST_ASSERT(ret == 0 && out_samples == 0, msg);
		printf("  n_samples=0: 0 outputs OK\n");

		channelizer_free(&ch);
	}

	printf("  PASS\n");
	return 0;
}

/*===========================================================================
 * Test 13: Multi-block Continuity
 *
 * Verify that processing the same signal in two blocks produces
 * consistent output (no discontinuities at block boundaries).
 *===========================================================================*/

static int test_multi_block_continuity(void)
{
	channelizer_t ch;
	float *input;
	float *channel_out[CHANNELIZER_MAX_CHANNELS];
	int out_samples;
	int ret;
	int result = 1;

	printf("\nTest 13: Multi-block Continuity\n");
	printf("=================================\n");

	int block_size = 4096;
	int total_samples = block_size * 2;

	input = (float *)calloc(total_samples * 2, sizeof(float));
	if (!input)
		return 1;

	/* Generate continuous tone spanning both blocks */
	float tone_freq = 100000.0f;  /* 100 kHz offset → channel 0 */
	generate_tone(input, total_samples, tone_freq, (float)TEST_SAMPLE_RATE, 1.0f);

	/* Process as a single large block */
	ret = channelizer_init(&ch, TEST_NUM_CHANNELS, TEST_CENTER_FREQ,
	                       TEST_BANDWIDTH, TEST_SAMPLE_RATE, total_samples);
	if (ret != 0) {
		free(input);
		return 1;
	}

	channelizer_process(&ch, input, total_samples, channel_out, &out_samples);
	int single_out = out_samples;

	channelizer_free(&ch);

	/* Process as two blocks */
	ret = channelizer_init(&ch, TEST_NUM_CHANNELS, TEST_CENTER_FREQ,
	                       TEST_BANDWIDTH, TEST_SAMPLE_RATE, total_samples);
	if (ret != 0) {
		free(input);
		return 1;
	}

	channelizer_process(&ch, input, block_size, channel_out, &out_samples);
	int block1_out = out_samples;
	float block1_power = compute_power(channel_out[0], block1_out);

	channelizer_process(&ch, input + block_size * 2, block_size,
	                    channel_out, &out_samples);
	int block2_out = out_samples;
	float block2_power = compute_power(channel_out[0], block2_out);

	channelizer_free(&ch);

	char msg[128];

	/* Total output count should match */
	snprintf(msg, sizeof(msg), "2-block total=%d (single=%d)",
	         block1_out + block2_out, single_out);
	TEST_ASSERT(block1_out + block2_out == single_out, msg);
	printf("  Output count: block1=%d + block2=%d = %d (single=%d) OK\n",
	       block1_out, block2_out, block1_out + block2_out, single_out);

	/* Power should be consistent between blocks (within 1 dB) */
	float power_diff = fabsf(power_to_db(block1_power) - power_to_db(block2_power));
	snprintf(msg, sizeof(msg),
	         "Block power consistency: %.1f dB difference (< 1 dB)",
	         power_diff);
	TEST_ASSERT(power_diff < 1.0f, msg);
	printf("  Power: block1=%.1f dB, block2=%.1f dB (diff=%.2f dB) OK\n",
	       power_to_db(block1_power), power_to_db(block2_power), power_diff);

	printf("  PASS\n");
	result = 0;

	free(input);
	return result;
}

/*===========================================================================
 * Main
 *===========================================================================*/

int main(void)
{
	int total_failures = 0;

	printf("============================================================\n");
	printf("    OS-PFB Validation Test Suite (2x Oversampled)\n");
	printf("============================================================\n");
	printf("  Configuration: %d channels @ %u Hz, D=M/2=%d\n",
	       TEST_NUM_CHANNELS, (unsigned)TEST_SAMPLE_RATE,
	       TEST_NUM_CHANNELS / 2);
	printf("  Channel spacing: %.1f kHz\n",
	       (float)TEST_SAMPLE_RATE / (float)TEST_NUM_CHANNELS / 1000.0f);
	printf("  Channel output rate: %u Hz\n",
	       (unsigned)(TEST_SAMPLE_RATE / (TEST_NUM_CHANNELS / 2)));

	total_failures += test_gap_coverage();
	total_failures += test_full_band_sweep();
	total_failures += test_output_rate();
	total_failures += test_channel_isolation();
	total_failures += test_overlap_verification();
	total_failures += test_non_aligned_samples();
	total_failures += test_realworld_433();
	total_failures += test_multi_m_struct();
	total_failures += test_m2_degenerate();
	total_failures += test_m16_large();
	total_failures += test_phase_correction();
	total_failures += test_error_paths();
	total_failures += test_multi_block_continuity();

	printf("\n============================================================\n");
	printf("  Assertions: %d/%d passed\n", tests_passed, tests_run);
	if (total_failures == 0 && tests_passed == tests_run) {
		printf("  ALL OS-PFB TESTS PASSED\n");
	} else {
		printf("  OS-PFB TESTS FAILED (%d test functions failed)\n", total_failures);
	}
	printf("============================================================\n");

	return (tests_passed == tests_run && total_failures == 0) ? 0 : 1;
}
