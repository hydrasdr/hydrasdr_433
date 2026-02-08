/** @file
    Unit tests for PFB channelizer.

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

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s\n", msg); \
        return 1; \
    } \
} while (0)

/* Test configuration constants */
#define TEST_CENTER_FREQ    868.5e6f
#define TEST_BANDWIDTH      2e6f
#define TEST_SAMPLE_RATE    2000000
#define TEST_INPUT_SAMPLES  4096
/* Offset for channel routing test.
 * With 4 channels @ 500 kHz spacing and 90% cutoff ratio, passband is ±225 kHz.
 * Use ±500 kHz (channel 1 center exactly) for clean routing test. */
#define TEST_OFFSET_500K    500000.0f
#define TEST_FREQ_TOLERANCE 1000.0f  /* 1 kHz tolerance for floating point */
#define TEST_MIN_POWER      0.001f   /* Minimum detectable power */

/**
 * Generate a complex sinusoid at specified frequency offset from center.
 * Output is CF32 (interleaved I/Q).
 */
static void generate_tone(float *buf, int n_samples, float freq_offset, float sample_rate)
{
	for (int i = 0; i < n_samples; i++) {
		float phase = 2.0f * (float)M_PI * freq_offset * (float)i / sample_rate;
		buf[i * 2 + 0] = cosf(phase);  /* I */
		buf[i * 2 + 1] = sinf(phase);  /* Q */
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

/**
 * Test basic channelizer initialization.
 */
static int test_init(void)
{
	channelizer_t ch = {0};
	int ret;
	int result = 1;

	printf("Test: channelizer initialization...\n");

	/* Test valid initialization */
	ret = channelizer_init(&ch, 4, TEST_CENTER_FREQ, TEST_BANDWIDTH, TEST_SAMPLE_RATE, 1024);
	if (ret != 0) {
		fprintf(stderr, "FAIL: Failed to initialize 4-channel channelizer\n");
		goto cleanup;
	}
	if (ch.num_channels != 4) {
		fprintf(stderr, "FAIL: Wrong number of channels\n");
		goto cleanup;
	}
	if (ch.channel_rate != TEST_SAMPLE_RATE / 4) {
		fprintf(stderr, "FAIL: Wrong channel rate\n");
		goto cleanup;
	}
	if (ch.initialized != 1) {
		fprintf(stderr, "FAIL: Not marked as initialized\n");
		goto cleanup;
	}

	channelizer_free(&ch);
	if (ch.initialized != 0) {
		fprintf(stderr, "FAIL: Not properly freed\n");
		return 1; /* Already freed, just return */
	}

	/* Test invalid channel count */
	ret = channelizer_init(&ch, 3, TEST_CENTER_FREQ, TEST_BANDWIDTH, TEST_SAMPLE_RATE, 1024);
	if (ret != -1) {
		fprintf(stderr, "FAIL: Should reject non-power-of-2 channel count\n");
		goto cleanup;
	}

	ret = channelizer_init(&ch, 32, TEST_CENTER_FREQ, TEST_BANDWIDTH, TEST_SAMPLE_RATE, 1024);
	if (ret != -1) {
		fprintf(stderr, "FAIL: Should reject channel count > 16\n");
		goto cleanup;
	}

	printf("  PASS\n");
	result = 0;

cleanup:
	channelizer_free(&ch);
	return result;
}

/**
 * Test channel frequency assignments.
 * Channels are in natural FFT order (same as liquid-dsp):
 *   Ch0: DC, Ch1-Ch(M/2-1): positive, ChM/2: Nyquist, Ch(M/2+1)-Ch(M-1): negative
 */
static int test_channel_frequencies(void)
{
	channelizer_t ch = {0};
	int ret;
	int result = 1;

	printf("Test: channel frequency assignments...\n");

	/* 4 channels across 2 MHz, centered at 868.5 MHz */
	ret = channelizer_init(&ch, 4, TEST_CENTER_FREQ, TEST_BANDWIDTH, TEST_SAMPLE_RATE, 1024);
	if (ret != 0) {
		fprintf(stderr, "FAIL: Failed to initialize channelizer\n");
		goto cleanup;
	}

	/* Channel frequencies (natural FFT order):
	 * Ch0: 868.5 MHz  (DC, center)
	 * Ch1: 869.0 MHz  (+500 kHz)
	 * Ch2: 869.5 MHz  (Nyquist, +1 MHz)
	 * Ch3: 868.0 MHz  (-500 kHz)
	 */
	float f0 = channelizer_get_channel_freq(&ch, 0);
	float f1 = channelizer_get_channel_freq(&ch, 1);
	float f2 = channelizer_get_channel_freq(&ch, 2);
	float f3 = channelizer_get_channel_freq(&ch, 3);

	printf("  Ch0: %.3f MHz\n", f0 / 1e6f);
	printf("  Ch1: %.3f MHz\n", f1 / 1e6f);
	printf("  Ch2: %.3f MHz\n", f2 / 1e6f);
	printf("  Ch3: %.3f MHz\n", f3 / 1e6f);

	/* Verify expected frequencies */
	float spacing = ch.channel_spacing;  /* fs/M = 500 kHz */
	if (fabsf(f0 - TEST_CENTER_FREQ) >= TEST_FREQ_TOLERANCE) {
		fprintf(stderr, "FAIL: Ch0 should be at center (DC)\n");
		goto cleanup;
	}
	if (fabsf(f1 - (TEST_CENTER_FREQ + spacing)) >= TEST_FREQ_TOLERANCE) {
		fprintf(stderr, "FAIL: Ch1 wrong\n");
		goto cleanup;
	}
	if (fabsf(f2 - (TEST_CENTER_FREQ + 2*spacing)) >= TEST_FREQ_TOLERANCE) {
		fprintf(stderr, "FAIL: Ch2 wrong (Nyquist)\n");
		goto cleanup;
	}
	if (fabsf(f3 - (TEST_CENTER_FREQ - spacing)) >= TEST_FREQ_TOLERANCE) {
		fprintf(stderr, "FAIL: Ch3 wrong\n");
		goto cleanup;
	}

	/* Verify positive/negative ordering in natural FFT order */
	if (f0 >= f1) {
		fprintf(stderr, "FAIL: DC should be less than +fs/M\n");
		goto cleanup;
	}
	if (f1 >= f2) {
		fprintf(stderr, "FAIL: +fs/M should be less than Nyquist\n");
		goto cleanup;
	}
	if (f3 >= f0) {
		fprintf(stderr, "FAIL: -fs/M should be less than DC\n");
		goto cleanup;
	}

	printf("  PASS\n");
	result = 0;

cleanup:
	channelizer_free(&ch);
	return result;
}

/**
 * Test that a tone at DC (center freq) appears in channel 0.
 * With natural FFT order, channel 0 is the DC bin.
 */
static int test_dc_tone(void)
{
	channelizer_t ch = {0};
	float *input = NULL;
	float *channel_out[CHANNELIZER_MAX_CHANNELS];
	int out_samples;
	int n_input = TEST_INPUT_SAMPLES;
	int ret;
	int result = 1; /* Assume failure */

	printf("Test: DC tone routing...\n");

	ret = channelizer_init(&ch, 4, TEST_CENTER_FREQ, TEST_BANDWIDTH, TEST_SAMPLE_RATE, n_input);
	if (ret != 0) {
		fprintf(stderr, "FAIL: Failed to initialize channelizer\n");
		goto cleanup;
	}

	/* Allocate and generate DC tone (freq offset = 0) */
	input = (float *)malloc(n_input * 2 * sizeof(float));
	if (!input) {
		fprintf(stderr, "FAIL: Failed to allocate input buffer\n");
		goto cleanup;
	}

	generate_tone(input, n_input, 0.0f, (float)TEST_SAMPLE_RATE);

	/* Process through channelizer */
	ret = channelizer_process(&ch, input, n_input, channel_out, &out_samples);
	if (ret != 0) {
		fprintf(stderr, "FAIL: Channelizer processing failed\n");
		goto cleanup;
	}
	if (out_samples <= 0) {
		fprintf(stderr, "FAIL: No output samples\n");
		goto cleanup;
	}

	/* Compute power in each channel */
	float powers[4];
	for (int i = 0; i < 4; i++) {
		powers[i] = compute_power(channel_out[i], out_samples);
		printf("  Ch%d power: %.3f\n", i, powers[i]);
	}

	/* DC (0 Hz offset) should appear in channel 0 */
	float dc_power = powers[0];
	float other_power = powers[1] + powers[2] + powers[3];
	printf("  DC channel power: %.3f, Other channels: %.3f\n", dc_power, other_power);

	/* Channel 0 should have most of the power (>95%) */
	if (dc_power <= 0.9f) {
		fprintf(stderr, "FAIL: DC tone power too low in channel 0\n");
		goto cleanup;
	}
	if (dc_power <= other_power * 10.0f) {
		fprintf(stderr, "FAIL: DC tone leaking to other channels\n");
		goto cleanup;
	}

	printf("  PASS\n");
	result = 0;

cleanup:
	free(input);
	channelizer_free(&ch);
	return result;
}

/**
 * Test that offset tones appear in correct channels.
 */
static int test_offset_tones(void)
{
	channelizer_t ch = {0};
	float *input = NULL;
	float *channel_out[CHANNELIZER_MAX_CHANNELS];
	int out_samples;
	int n_input = TEST_INPUT_SAMPLES;
	int ret;
	int result = 1;

	printf("Test: offset tone routing...\n");

	/* 4 channels, 500 kHz each, total 2 MHz bandwidth */
	ret = channelizer_init(&ch, 4, TEST_CENTER_FREQ, TEST_BANDWIDTH, TEST_SAMPLE_RATE, n_input);
	if (ret != 0) {
		fprintf(stderr, "FAIL: Failed to initialize channelizer\n");
		goto cleanup;
	}

	input = (float *)malloc(n_input * 2 * sizeof(float));
	if (!input) {
		fprintf(stderr, "FAIL: Failed to allocate input buffer\n");
		goto cleanup;
	}

	/* Test tone at +500 kHz (exactly at Channel 1 center)
	 * Channel 1 is centered at +500 kHz.
	 * Tone at channel center should have maximum power and best isolation. */
	generate_tone(input, n_input, TEST_OFFSET_500K, (float)TEST_SAMPLE_RATE);

	ret = channelizer_process(&ch, input, n_input, channel_out, &out_samples);
	if (ret != 0) {
		fprintf(stderr, "FAIL: Channelizer processing failed\n");
		goto cleanup;
	}

	float powers[4];
	for (int i = 0; i < 4; i++) {
		powers[i] = compute_power(channel_out[i], out_samples);
		printf("  +500kHz: Ch%d power: %.3f\n", i, powers[i]);
	}

	/* +500 kHz with 4 channels @ 500kHz spacing:
	 * This tone is exactly at Ch1 center (869.0 MHz).
	 * Should appear primarily in channel 1. */
	int max_ch = 0;
	for (int i = 1; i < 4; i++) {
		if (powers[i] > powers[max_ch])
			max_ch = i;
	}
	printf("  Maximum power in channel %d (freq %.3f MHz)\n",
	       max_ch, channelizer_get_channel_freq(&ch, max_ch) / 1e6f);
	/* Verify tone has detectable power in channel 1 */
	if (powers[1] <= TEST_MIN_POWER) {
		fprintf(stderr, "FAIL: +500kHz tone has no power in Ch1\n");
		goto cleanup;
	}
	/* Verify good isolation - other channels should be much weaker (>40dB) */
	if (powers[0] > powers[1] * 0.01f || powers[2] > powers[1] * 0.01f || powers[3] > powers[1] * 0.01f) {
		fprintf(stderr, "FAIL: +500kHz tone leaking to other channels (expect >40dB isolation)\n");
		goto cleanup;
	}

	/* Reinitialize channelizer to flush window buffer state from previous tone */
	channelizer_free(&ch);
	ret = channelizer_init(&ch, 4, TEST_CENTER_FREQ, TEST_BANDWIDTH, TEST_SAMPLE_RATE, n_input);
	if (ret != 0) {
		fprintf(stderr, "FAIL: Failed to reinitialize channelizer\n");
		goto cleanup;
	}

	/* Test tone at -500 kHz (exactly at Channel 3 center)
	 * Channel 3 is centered at -500 kHz.
	 * Tone at channel center should have maximum power and best isolation. */
	generate_tone(input, n_input, -TEST_OFFSET_500K, (float)TEST_SAMPLE_RATE);

	ret = channelizer_process(&ch, input, n_input, channel_out, &out_samples);
	if (ret != 0) {
		fprintf(stderr, "FAIL: Channelizer processing failed\n");
		goto cleanup;
	}

	for (int i = 0; i < 4; i++) {
		powers[i] = compute_power(channel_out[i], out_samples);
		printf("  -500kHz: Ch%d power: %.3f\n", i, powers[i]);
	}

	/* -500 kHz should appear primarily in channel 3 */
	/* Verify tone has detectable power in channel 3 */
	if (powers[3] <= TEST_MIN_POWER) {
		fprintf(stderr, "FAIL: -500kHz tone has no power in Ch3\n");
		goto cleanup;
	}
	/* Verify good isolation (>40dB) */
	if (powers[0] > powers[3] * 0.01f || powers[1] > powers[3] * 0.01f || powers[2] > powers[3] * 0.01f) {
		fprintf(stderr, "FAIL: -500kHz tone leaking to other channels (expect >40dB isolation)\n");
		goto cleanup;
	}

	printf("  PASS\n");
	result = 0;

cleanup:
	free(input);
	channelizer_free(&ch);
	return result;
}

/**
 * Test 8-channel configuration with natural FFT order.
 */
static int test_8_channels(void)
{
	channelizer_t ch = {0};
	int ret;
	int result = 1;

	printf("Test: 8-channel configuration...\n");

	ret = channelizer_init(&ch, 8, TEST_CENTER_FREQ, TEST_BANDWIDTH, WIDEBAND_RATE_2M5, 2048);
	if (ret != 0) {
		fprintf(stderr, "FAIL: Failed to initialize 8-channel channelizer\n");
		goto cleanup;
	}
	if (ch.num_channels != 8) {
		fprintf(stderr, "FAIL: Wrong number of channels\n");
		goto cleanup;
	}
	if (ch.channel_rate != 312500) {
		fprintf(stderr, "FAIL: Wrong channel rate\n");
		goto cleanup;
	}

	/* Verify all channel frequencies */
	printf("  Channel frequencies:\n");
	for (int i = 0; i < 8; i++) {
		float freq = channelizer_get_channel_freq(&ch, i);
		printf("    Ch%d: %.3f MHz\n", i, freq / 1e6f);
	}

	/* Verify natural FFT ordering:
	 * Ch0=DC, Ch1-3=positive, Ch4=Nyquist, Ch5-7=negative */
	float spacing = ch.channel_spacing;  /* fs/M = 312.5 kHz */
	(void)spacing; /* Unused but documents the expected value */
	float f0 = channelizer_get_channel_freq(&ch, 0);
	float f4 = channelizer_get_channel_freq(&ch, 4);  /* Nyquist */
	float f5 = channelizer_get_channel_freq(&ch, 5);  /* Most negative */

	if (fabsf(f0 - TEST_CENTER_FREQ) >= TEST_FREQ_TOLERANCE) {
		fprintf(stderr, "FAIL: Ch0 not at center (DC)\n");
		goto cleanup;
	}
	if (f4 <= f0) {
		fprintf(stderr, "FAIL: Nyquist should be above DC\n");
		goto cleanup;
	}
	if (f5 >= f0) {
		fprintf(stderr, "FAIL: Ch5 should be below DC (negative freq)\n");
		goto cleanup;
	}

	/* Positive channels should increase */
	for (int i = 0; i < 4; i++) {
		float f_curr = channelizer_get_channel_freq(&ch, i);
		float f_next = channelizer_get_channel_freq(&ch, i + 1);
		if (f_curr >= f_next) {
			fprintf(stderr, "FAIL: Positive freqs not increasing\n");
			goto cleanup;
		}
	}

	/* Negative channels (5-7) should increase toward DC */
	for (int i = 5; i < 7; i++) {
		float f_curr = channelizer_get_channel_freq(&ch, i);
		float f_next = channelizer_get_channel_freq(&ch, i + 1);
		if (f_curr >= f_next) {
			fprintf(stderr, "FAIL: Negative freqs not increasing\n");
			goto cleanup;
		}
	}

	printf("  PASS\n");
	result = 0;

cleanup:
	channelizer_free(&ch);
	return result;
}

/**
 * Test that non-aligned sample counts (n_samples % M != 0) are handled
 * correctly: trailing samples should be silently dropped, not cause errors.
 * Finding #27.
 */
static int test_non_aligned_samples(void)
{
	channelizer_t ch = {0};
	float *input = NULL;
	float *channel_out[CHANNELIZER_MAX_CHANNELS];
	int out_samples;
	int ret;
	int result = 1;

	printf("Test: non-aligned sample counts (n_samples %% M != 0)...\n");

	int M = 4;
	int base = 1024; /* aligned sample count */

	ret = channelizer_init(&ch, M, TEST_CENTER_FREQ, TEST_BANDWIDTH,
	                       TEST_SAMPLE_RATE, base + M);
	if (ret != 0) {
		fprintf(stderr, "FAIL: Failed to initialize channelizer\n");
		goto cleanup;
	}

	input = (float *)malloc((base + M) * 2 * sizeof(float));
	if (!input) {
		fprintf(stderr, "FAIL: Failed to allocate input buffer\n");
		goto cleanup;
	}

	/* Generate a DC tone for all samples */
	generate_tone(input, base + M, 0.0f, (float)TEST_SAMPLE_RATE);

	/* Process aligned count first for reference */
	ret = channelizer_process(&ch, input, base, channel_out, &out_samples);
	if (ret != 0) {
		fprintf(stderr, "FAIL: Aligned processing failed\n");
		goto cleanup;
	}
	int aligned_out = out_samples;

	/* Expected: base / M output samples */
	if (aligned_out != base / M) {
		fprintf(stderr, "FAIL: Expected %d output samples, got %d\n",
		        base / M, aligned_out);
		goto cleanup;
	}
	printf("  Aligned (%d samples): %d outputs OK\n", base, aligned_out);

	/* Test various non-aligned counts */
	int offsets[] = {1, 2, 3}; /* remainders 1, 2, 3 for M=4 */
	for (int t = 0; t < 3; t++) {
		int n = base + offsets[t];

		/* Reinitialize to reset state */
		channelizer_free(&ch);
		ret = channelizer_init(&ch, M, TEST_CENTER_FREQ, TEST_BANDWIDTH,
		                       TEST_SAMPLE_RATE, base + M);
		if (ret != 0) {
			fprintf(stderr, "FAIL: Reinit failed\n");
			goto cleanup;
		}

		ret = channelizer_process(&ch, input, n, channel_out, &out_samples);
		if (ret != 0) {
			fprintf(stderr, "FAIL: Non-aligned processing failed for n=%d\n", n);
			goto cleanup;
		}

		/* Should produce same output as aligned (trailing samples dropped) */
		if (out_samples != aligned_out) {
			fprintf(stderr, "FAIL: n=%d (%d extra): expected %d outputs, got %d\n",
			        n, offsets[t], aligned_out, out_samples);
			goto cleanup;
		}

		/* Verify output power is valid (not garbage) */
		float power = compute_power(channel_out[0], out_samples);
		if (power < 0.5f) {
			fprintf(stderr, "FAIL: DC power too low for n=%d (%.3f)\n",
			        n, power);
			goto cleanup;
		}

		printf("  Non-aligned (%d samples, +%d extra): %d outputs, "
		       "DC power=%.3f OK\n", n, offsets[t], out_samples, power);
	}

	/* Test with fewer than M samples - should produce 0 outputs */
	for (int n = 0; n < M; n++) {
		channelizer_free(&ch);
		ret = channelizer_init(&ch, M, TEST_CENTER_FREQ, TEST_BANDWIDTH,
		                       TEST_SAMPLE_RATE, base + M);
		if (ret != 0) {
			fprintf(stderr, "FAIL: Reinit failed\n");
			goto cleanup;
		}

		if (n == 0) {
			/* n_samples=0 should return error */
			ret = channelizer_process(&ch, input, 0, channel_out, &out_samples);
			if (ret == 0) {
				fprintf(stderr, "FAIL: n=0 should return error\n");
				goto cleanup;
			}
			printf("  n=0: returns error as expected OK\n");
		} else {
			ret = channelizer_process(&ch, input, n, channel_out, &out_samples);
			if (ret != 0) {
				fprintf(stderr, "FAIL: n=%d should not return error\n", n);
				goto cleanup;
			}
			if (out_samples != 0) {
				fprintf(stderr, "FAIL: n=%d: expected 0 outputs, got %d\n",
				        n, out_samples);
				goto cleanup;
			}
			printf("  n=%d (< M=%d): 0 outputs OK\n", n, M);
		}
	}

	printf("  PASS\n");
	result = 0;

cleanup:
	free(input);
	channelizer_free(&ch);
	return result;
}

int main(void)
{
	int failures = 0;

	printf("=== PFB Channelizer Tests ===\n\n");

	failures += test_init();
	failures += test_channel_frequencies();
	failures += test_dc_tone();
	failures += test_offset_tones();
	failures += test_8_channels();
	failures += test_non_aligned_samples();

	printf("\n");
	if (failures == 0) {
		printf("All tests PASSED\n");
		return 0;
	} else {
		printf("%d test(s) FAILED\n", failures);
		return 1;
	}
}
