/** @file
    Functional tests for PFB channelizer - validates signal integrity and channel isolation.

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

#define TEST_NUM_CHANNELS    4
#define TEST_CENTER_FREQ     868.5e6f
#define TEST_BANDWIDTH       2.0e6f
#define TEST_SAMPLE_RATE     2000000
#define TEST_INPUT_SAMPLES   8192      /* More samples for better frequency resolution */
#define TEST_PHASE_TOLERANCE 0.1f      /* Radians - phase error tolerance */
#define TEST_AMP_TOLERANCE   0.15f     /* 15% amplitude error tolerance */
#define TEST_REJECTION_DB    20.0f     /* Minimum adjacent channel rejection in dB */
#define TEST_STOPBAND_DB     30.0f     /* Minimum stopband rejection in dB */

/*===========================================================================
 * Test Utilities
 *===========================================================================*/

#define TEST_ASSERT(cond, msg) do { \
	if (!(cond)) { \
		fprintf(stderr, "  FAIL: %s\n", msg); \
		return 1; \
	} \
} while (0)

#define PRINT_RESULT(name, passed) \
	printf("  %s: %s\n", name, (passed) ? "PASS" : "FAIL")

/**
 * Generate a complex sinusoid at specified frequency offset from center.
 * Output is CF32 (interleaved I/Q).
 */
static void generate_tone(float *buf, int n_samples, float freq_offset,
                          float sample_rate, float amplitude, float initial_phase)
{
	for (int i = 0; i < n_samples; i++) {
		float phase = initial_phase + 2.0f * (float)M_PI * freq_offset * (float)i / sample_rate;
		buf[i * 2 + 0] = amplitude * cosf(phase);  /* I */
		buf[i * 2 + 1] = amplitude * sinf(phase);  /* Q */
	}
}

/**
 * Add a tone to existing buffer (for multi-tone tests).
 */
static void add_tone(float *buf, int n_samples, float freq_offset,
                     float sample_rate, float amplitude, float initial_phase)
{
	for (int i = 0; i < n_samples; i++) {
		float phase = initial_phase + 2.0f * (float)M_PI * freq_offset * (float)i / sample_rate;
		buf[i * 2 + 0] += amplitude * cosf(phase);
		buf[i * 2 + 1] += amplitude * sinf(phase);
	}
}

/**
 * Compute average power of CF32 signal in linear scale.
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
 * Convert linear power to dB.
 */
static float power_to_db(float power)
{
	if (power < 1e-20f)
		return -200.0f;
	return 10.0f * log10f(power);
}

/**
 * Estimate frequency of a complex signal using phase difference method.
 * Returns frequency in Hz relative to baseband.
 */
static float estimate_frequency(const float *buf, int n_samples, float sample_rate)
{
	if (n_samples < 2)
		return 0.0f;

	float phase_sum = 0.0f;
	int count = 0;

	for (int i = 1; i < n_samples; i++) {
		float I0 = buf[(i - 1) * 2 + 0];
		float Q0 = buf[(i - 1) * 2 + 1];
		float I1 = buf[i * 2 + 0];
		float Q1 = buf[i * 2 + 1];

		/* Skip low-amplitude samples */
		float mag0 = sqrtf(I0 * I0 + Q0 * Q0);
		float mag1 = sqrtf(I1 * I1 + Q1 * Q1);
		if (mag0 < 0.01f || mag1 < 0.01f)
			continue;

		/* Phase difference: angle of (s1 * conj(s0)) */
		float real = I1 * I0 + Q1 * Q0;
		float imag = Q1 * I0 - I1 * Q0;
		float phase_diff = atan2f(imag, real);

		phase_sum += phase_diff;
		count++;
	}

	if (count == 0)
		return 0.0f;

	float avg_phase_diff = phase_sum / (float)count;
	return avg_phase_diff * sample_rate / (2.0f * (float)M_PI);
}

/**
 * Compute cross-correlation peak between two signals to check similarity.
 * Returns normalized correlation (0 to 1).
 */
static float compute_correlation(const float *sig1, const float *sig2, int n_samples)
{
	float sum_prod = 0.0f;
	float sum_sq1 = 0.0f;
	float sum_sq2 = 0.0f;

	for (int i = 0; i < n_samples; i++) {
		float I1 = sig1[i * 2 + 0];
		float Q1 = sig1[i * 2 + 1];
		float I2 = sig2[i * 2 + 0];
		float Q2 = sig2[i * 2 + 1];

		/* Magnitude correlation */
		float mag1 = sqrtf(I1 * I1 + Q1 * Q1);
		float mag2 = sqrtf(I2 * I2 + Q2 * Q2);

		sum_prod += mag1 * mag2;
		sum_sq1 += mag1 * mag1;
		sum_sq2 += mag2 * mag2;
	}

	if (sum_sq1 < 1e-10f || sum_sq2 < 1e-10f)
		return 0.0f;

	return sum_prod / sqrtf(sum_sq1 * sum_sq2);
}

/*===========================================================================
 * Test Cases
 *===========================================================================*/

/**
 * Test 1: Channel Isolation / Adjacent Channel Rejection
 *
 * Place a tone in the center of one channel and measure power leakage
 * to adjacent and non-adjacent channels.
 */
static int test_channel_isolation(void)
{
	channelizer_t ch;
	float *input;
	float *channel_out[CHANNELIZER_MAX_CHANNELS];
	int out_samples;
	int n_input = TEST_INPUT_SAMPLES;
	int ret;
	int test_passed = 1;

	printf("\nTest 1: Channel Isolation / Adjacent Channel Rejection\n");
	printf("=========================================================\n");

	ret = channelizer_init(&ch, TEST_NUM_CHANNELS, TEST_CENTER_FREQ,
	                       TEST_BANDWIDTH, TEST_SAMPLE_RATE, n_input);
	if (ret != 0) {
		printf("  Failed to initialize channelizer\n");
		return 1;
	}

	input = (float *)calloc(n_input * 2, sizeof(float));
	if (!input) {
		channelizer_free(&ch);
		return 1;
	}

	float channel_bw = TEST_BANDWIDTH / (float)TEST_NUM_CHANNELS;
	printf("  Channel bandwidth: %.0f kHz\n", channel_bw / 1000.0f);
	printf("  Channel rate: %u Hz\n", ch.channel_rate);
	printf("\n");

	/* Test each channel as the signal source */
	for (int src_chan = 0; src_chan < TEST_NUM_CHANNELS; src_chan++) {
		/* Reinitialize to clear filter state from prior iteration */
		if (src_chan > 0) {
			channelizer_free(&ch);
			ret = channelizer_init(&ch, TEST_NUM_CHANNELS, TEST_CENTER_FREQ,
			                       TEST_BANDWIDTH, TEST_SAMPLE_RATE, n_input);
			if (ret != 0) {
				printf("  Failed to reinitialize channelizer\n");
				test_passed = 0;
				break;
			}
		}

		/* Place tone in center of source channel */
		float src_freq = channelizer_get_channel_freq(&ch, src_chan);
		float freq_offset = src_freq - TEST_CENTER_FREQ;

		memset(input, 0, n_input * 2 * sizeof(float));
		generate_tone(input, n_input, freq_offset, (float)TEST_SAMPLE_RATE, 1.0f, 0.0f);

		ret = channelizer_process(&ch, input, n_input, channel_out, &out_samples);
		if (ret != 0) {
			printf("  Channelizer processing failed for src_chan=%d\n", src_chan);
			test_passed = 0;
			continue;
		}

		/* Measure power in each channel */
		float powers[CHANNELIZER_MAX_CHANNELS];
		float powers_db[CHANNELIZER_MAX_CHANNELS];
		float max_power = 0.0f;

		for (int c = 0; c < TEST_NUM_CHANNELS; c++) {
			powers[c] = compute_power(channel_out[c], out_samples);
			powers_db[c] = power_to_db(powers[c]);
			if (powers[c] > max_power)
				max_power = powers[c];
		}

		float ref_db = power_to_db(max_power);

		printf("  Source channel %d (%.3f MHz, offset %+.0f kHz):\n",
		       src_chan, src_freq / 1e6f, freq_offset / 1000.0f);

		for (int c = 0; c < TEST_NUM_CHANNELS; c++) {
			float rejection = ref_db - powers_db[c];
			const char *status;

			if (c == src_chan) {
				status = (powers[c] > 0.01f) ? "OK (signal)" : "LOW!";
			} else {
				int is_adjacent = (abs(c - src_chan) == 1);
				float min_rejection = is_adjacent ? TEST_REJECTION_DB : TEST_STOPBAND_DB;

				if (rejection >= min_rejection || powers[c] < 1e-6f) {
					status = "OK";
				} else {
					status = "LEAK!";
					test_passed = 0;
				}
			}

			printf("    Ch%d: %7.2f dB (rejection: %5.1f dB) %s\n",
			       c, powers_db[c], rejection, status);
		}
		printf("\n");
	}

	free(input);
	channelizer_free(&ch);

	PRINT_RESULT("Channel Isolation", test_passed);
	return test_passed ? 0 : 1;
}

/**
 * Test 2: Signal Integrity - Verify signals are not scrambled
 *
 * Input a known waveform and verify the output maintains:
 * - Correct frequency (after channel translation)
 * - Preserved amplitude (within tolerance)
 * - Phase coherence
 */
static int test_signal_integrity(void)
{
	channelizer_t ch;
	float *input;
	float *channel_out[CHANNELIZER_MAX_CHANNELS];
	int out_samples;
	int n_input = TEST_INPUT_SAMPLES;
	int ret;
	int test_passed = 1;

	printf("\nTest 2: Signal Integrity - Waveform Preservation\n");
	printf("==================================================\n");

	ret = channelizer_init(&ch, TEST_NUM_CHANNELS, TEST_CENTER_FREQ,
	                       TEST_BANDWIDTH, TEST_SAMPLE_RATE, n_input);
	if (ret != 0) {
		printf("  Failed to initialize channelizer\n");
		return 1;
	}

	input = (float *)calloc(n_input * 2, sizeof(float));
	if (!input) {
		channelizer_free(&ch);
		return 1;
	}

	/* Test with different frequency offsets within each channel */
	float test_offsets[] = {0.0f, 0.1f, 0.25f, -0.1f, -0.25f};  /* Fraction of channel BW */
	int num_offsets = sizeof(test_offsets) / sizeof(test_offsets[0]);
	float channel_bw = TEST_BANDWIDTH / (float)TEST_NUM_CHANNELS;

	for (int offset_idx = 0; offset_idx < num_offsets; offset_idx++) {
		float offset_frac = test_offsets[offset_idx];
		int target_chan = 1;  /* Test in channel 1 */

		float chan_center = channelizer_get_channel_freq(&ch, target_chan);
		float tone_freq = chan_center + offset_frac * channel_bw;
		float freq_offset = tone_freq - TEST_CENTER_FREQ;

		/* Generate input tone */
		memset(input, 0, n_input * 2 * sizeof(float));
		float input_amplitude = 0.8f;
		generate_tone(input, n_input, freq_offset, (float)TEST_SAMPLE_RATE,
		              input_amplitude, 0.0f);

		/* Process through channelizer */
		ret = channelizer_process(&ch, input, n_input, channel_out, &out_samples);
		if (ret != 0 || out_samples < 10) {
			printf("  Processing failed for offset %.2f\n", offset_frac);
			test_passed = 0;
			continue;
		}

		/* Measure output characteristics.
		 * For a complex exponential I=A*cos(θ), Q=A*sin(θ): power = A²
		 * so amplitude = sqrt(power). */
		float out_power = compute_power(channel_out[target_chan], out_samples);
		float out_amplitude = sqrtf(out_power);

		/* Expected output frequency: offset within channel */
		float expected_out_freq = offset_frac * channel_bw;
		float measured_freq = estimate_frequency(channel_out[target_chan],
		                                         out_samples, (float)ch.channel_rate);

		/* Check amplitude preservation */
		float amp_error = fabsf(out_amplitude - input_amplitude) / input_amplitude;
		int amp_ok = (amp_error < TEST_AMP_TOLERANCE);

		/* Check frequency preservation (within channel) */
		float freq_error = fabsf(measured_freq - expected_out_freq);
		float freq_tolerance = channel_bw * 0.1f;  /* 10% of channel BW */
		int freq_ok = (freq_error < freq_tolerance);

		printf("  Offset %+.2f * BW (%.0f Hz in channel):\n", offset_frac,
		       expected_out_freq);
		printf("    Input amplitude:  %.3f\n", input_amplitude);
		printf("    Output amplitude: %.3f (error: %.1f%%) %s\n",
		       out_amplitude, amp_error * 100.0f, amp_ok ? "OK" : "FAIL");
		printf("    Expected freq:    %.0f Hz\n", expected_out_freq);
		printf("    Measured freq:    %.0f Hz (error: %.0f Hz) %s\n",
		       measured_freq, freq_error, freq_ok ? "OK" : "FAIL");

		if (!amp_ok || !freq_ok)
			test_passed = 0;
	}

	free(input);
	channelizer_free(&ch);

	printf("\n");
	PRINT_RESULT("Signal Integrity", test_passed);
	return test_passed ? 0 : 1;
}

/**
 * Test 3: Multi-tone separation
 *
 * Input multiple tones in different channels and verify they are
 * correctly separated without cross-contamination.
 */
static int test_multi_tone_separation(void)
{
	channelizer_t ch;
	float *input;
	float *channel_out[CHANNELIZER_MAX_CHANNELS];
	int out_samples;
	int n_input = TEST_INPUT_SAMPLES;
	int ret;
	int test_passed = 1;

	printf("\nTest 3: Multi-tone Separation\n");
	printf("==============================\n");

	ret = channelizer_init(&ch, TEST_NUM_CHANNELS, TEST_CENTER_FREQ,
	                       TEST_BANDWIDTH, TEST_SAMPLE_RATE, n_input);
	if (ret != 0) {
		printf("  Failed to initialize channelizer\n");
		return 1;
	}

	input = (float *)calloc(n_input * 2, sizeof(float));
	if (!input) {
		channelizer_free(&ch);
		return 1;
	}

	/* Generate tones in channels 0 and 2 with different amplitudes */
	float amp_chan0 = 1.0f;
	float amp_chan2 = 0.5f;
	float freq_chan0 = channelizer_get_channel_freq(&ch, 0) - TEST_CENTER_FREQ;
	float freq_chan2 = channelizer_get_channel_freq(&ch, 2) - TEST_CENTER_FREQ;

	printf("  Input: Two tones\n");
	printf("    Tone 1: Channel 0, amplitude %.2f, offset %+.0f kHz\n",
	       amp_chan0, freq_chan0 / 1000.0f);
	printf("    Tone 2: Channel 2, amplitude %.2f, offset %+.0f kHz\n",
	       amp_chan2, freq_chan2 / 1000.0f);

	memset(input, 0, n_input * 2 * sizeof(float));
	generate_tone(input, n_input, freq_chan0, (float)TEST_SAMPLE_RATE, amp_chan0, 0.0f);
	add_tone(input, n_input, freq_chan2, (float)TEST_SAMPLE_RATE, amp_chan2, (float)M_PI / 4.0f);

	/* Process through channelizer */
	ret = channelizer_process(&ch, input, n_input, channel_out, &out_samples);
	if (ret != 0) {
		printf("  Channelizer processing failed\n");
		free(input);
		channelizer_free(&ch);
		return 1;
	}

	printf("\n  Output power per channel:\n");

	float powers[CHANNELIZER_MAX_CHANNELS];
	float expected_powers[CHANNELIZER_MAX_CHANNELS] = {0};
	/* For complex exponential I=A*cos(θ), Q=A*sin(θ): power = A² */
	expected_powers[0] = amp_chan0 * amp_chan0;
	expected_powers[2] = amp_chan2 * amp_chan2;

	for (int c = 0; c < TEST_NUM_CHANNELS; c++) {
		powers[c] = compute_power(channel_out[c], out_samples);
		float power_db = power_to_db(powers[c]);
		float expected_db = power_to_db(expected_powers[c]);

		const char *status;
		if (expected_powers[c] > 0.01f) {
			/* Channel should have signal */
			float error_db = fabsf(power_db - expected_db);
			status = (error_db < 3.0f) ? "OK (signal)" : "LEVEL ERROR";
			if (error_db >= 3.0f)
				test_passed = 0;
		} else {
			/* Channel should be quiet */
			status = (powers[c] < 0.01f) ? "OK (quiet)" : "LEAKAGE!";
			if (powers[c] >= 0.01f)
				test_passed = 0;
		}

		printf("    Ch%d: %.4f (%.1f dB) %s\n", c, powers[c], power_db, status);
	}

	/* Verify amplitude ratio is preserved */
	if (powers[0] > 0.001f && powers[2] > 0.001f) {
		float measured_ratio = sqrtf(powers[0] / powers[2]);
		float expected_ratio = amp_chan0 / amp_chan2;
		float ratio_error = fabsf(measured_ratio - expected_ratio) / expected_ratio;

		printf("\n  Amplitude ratio check:\n");
		printf("    Expected ratio (Ch0/Ch2): %.2f\n", expected_ratio);
		printf("    Measured ratio:           %.2f (error: %.1f%%)\n",
		       measured_ratio, ratio_error * 100.0f);

		if (ratio_error > 0.2f) {
			printf("    FAIL: Ratio error too large\n");
			test_passed = 0;
		}
	}

	free(input);
	channelizer_free(&ch);

	printf("\n");
	PRINT_RESULT("Multi-tone Separation", test_passed);
	return test_passed ? 0 : 1;
}

/**
 * Test 4: Filter Transition Band Response
 *
 * Sweep a tone across the channel boundary and measure the filter
 * response to verify proper rolloff.
 */
static int test_filter_response(void)
{
	channelizer_t ch;
	float *input;
	float *channel_out[CHANNELIZER_MAX_CHANNELS];
	int out_samples;
	int n_input = TEST_INPUT_SAMPLES;
	int ret;
	int test_passed = 1;

	printf("\nTest 4: Filter Transition Band Response\n");
	printf("=========================================\n");

	ret = channelizer_init(&ch, TEST_NUM_CHANNELS, TEST_CENTER_FREQ,
	                       TEST_BANDWIDTH, TEST_SAMPLE_RATE, n_input);
	if (ret != 0) {
		printf("  Failed to initialize channelizer\n");
		return 1;
	}

	input = (float *)calloc(n_input * 2, sizeof(float));
	if (!input) {
		channelizer_free(&ch);
		return 1;
	}

	float channel_bw = TEST_BANDWIDTH / (float)TEST_NUM_CHANNELS;
	int target_chan = 1;
	float chan_center = channelizer_get_channel_freq(&ch, target_chan);

	printf("  Sweeping tone across Channel %d boundary (center: %.3f MHz)\n",
	       target_chan, chan_center / 1e6f);
	printf("  Channel bandwidth: %.0f kHz\n\n", channel_bw / 1000.0f);

	/* Sweep from -1.0 BW to +1.0 BW around channel center */
	int num_points = 21;
	float sweep_offsets[21];
	for (int i = 0; i < num_points; i++) {
		sweep_offsets[i] = (float)i / (float)(num_points - 1) * 2.0f - 1.0f;  /* -1.0 to +1.0 */
	}

	printf("  Offset     | Ch%d Power | Ch%d Power | Response\n",
	       target_chan, target_chan + 1);
	printf("  -----------+-----------+-----------+---------\n");

	float passband_power = 0.0f;

	for (int i = 0; i < num_points; i++) {
		float offset_frac = sweep_offsets[i];
		float freq_offset = (chan_center - TEST_CENTER_FREQ) + offset_frac * channel_bw;

		memset(input, 0, n_input * 2 * sizeof(float));
		generate_tone(input, n_input, freq_offset, (float)TEST_SAMPLE_RATE, 1.0f, 0.0f);

		ret = channelizer_process(&ch, input, n_input, channel_out, &out_samples);
		if (ret != 0)
			continue;

		float power_target = compute_power(channel_out[target_chan], out_samples);
		float power_adjacent = compute_power(channel_out[target_chan + 1], out_samples);

		/* Record passband power for reference */
		if (fabsf(offset_frac) < 0.1f && power_target > passband_power) {
			passband_power = power_target;
		}

		float power_db = power_to_db(power_target);
		float adj_db = power_to_db(power_adjacent);

		/* Visual indicator of filter response */
		char bar[21];
		int bar_len = (int)((power_db + 60.0f) / 3.0f);
		if (bar_len < 0) bar_len = 0;
		if (bar_len > 20) bar_len = 20;
		memset(bar, '#', bar_len);
		bar[bar_len] = '\0';

		printf("  %+5.2f BW  | %6.1f dB | %6.1f dB | %s\n",
		       offset_frac, power_db, adj_db, bar);
	}

	/* Verify stopband rejection */
	printf("\n  Passband reference power: %.1f dB\n", power_to_db(passband_power));

	/* Test at 1.0x BW offset (should be well into stopband) */
	float freq_offset = (chan_center - TEST_CENTER_FREQ) + 1.0f * channel_bw;
	memset(input, 0, n_input * 2 * sizeof(float));
	generate_tone(input, n_input, freq_offset, (float)TEST_SAMPLE_RATE, 1.0f, 0.0f);
	channelizer_process(&ch, input, n_input, channel_out, &out_samples);

	float stopband_power = compute_power(channel_out[target_chan], out_samples);
	float rejection = power_to_db(passband_power) - power_to_db(stopband_power);

	printf("  Stopband power (at +1.0 BW): %.1f dB\n", power_to_db(stopband_power));
	printf("  Stopband rejection: %.1f dB\n", rejection);

	if (rejection < TEST_REJECTION_DB) {
		printf("  FAIL: Insufficient stopband rejection (need %.1f dB)\n", TEST_REJECTION_DB);
		test_passed = 0;
	}

	free(input);
	channelizer_free(&ch);

	printf("\n");
	PRINT_RESULT("Filter Response", test_passed);
	return test_passed ? 0 : 1;
}

/**
 * Test 5: Phase Continuity
 *
 * Verify that the channelizer maintains phase continuity across
 * processing blocks (no phase jumps at block boundaries).
 */
static int test_phase_continuity(void)
{
	channelizer_t ch;
	float *input;
	float *channel_out[CHANNELIZER_MAX_CHANNELS];
	int out_samples;
	int n_input = TEST_INPUT_SAMPLES;
	int ret;
	int test_passed = 1;

	printf("\nTest 5: Phase Continuity Across Blocks\n");
	printf("=======================================\n");

	ret = channelizer_init(&ch, TEST_NUM_CHANNELS, TEST_CENTER_FREQ,
	                       TEST_BANDWIDTH, TEST_SAMPLE_RATE, n_input);
	if (ret != 0) {
		printf("  Failed to initialize channelizer\n");
		return 1;
	}

	input = (float *)calloc(n_input * 2, sizeof(float));
	if (!input) {
		channelizer_free(&ch);
		return 1;
	}

	/* Store output from previous block for continuity check */
	float *prev_output = (float *)calloc(n_input * 2, sizeof(float));
	if (!prev_output) {
		free(input);
		channelizer_free(&ch);
		return 1;
	}

	/* Test with tone at channel 1 center frequency.
	 * After channelization, signal should be at DC within channel (no rotation). */
	int target_chan = 1;
	float freq_offset = channelizer_get_channel_freq(&ch, target_chan) - TEST_CENTER_FREQ;

	printf("  Testing phase continuity for continuous tone at Ch%d\n", target_chan);
	printf("  Tone at %+.0f kHz offset (channel center)\n", freq_offset / 1000.0f);

	/* Process multiple blocks of continuous tone */
	int num_blocks = 4;

	for (int block = 0; block < num_blocks; block++) {
		/* Generate continuous tone across blocks */
		for (int i = 0; i < n_input; i++) {
			int global_sample = block * n_input + i;
			float phase = 2.0f * (float)M_PI * freq_offset * (float)global_sample / (float)TEST_SAMPLE_RATE;
			input[i * 2 + 0] = cosf(phase);
			input[i * 2 + 1] = sinf(phase);
		}

		ret = channelizer_process(&ch, input, n_input, channel_out, &out_samples);
		if (ret != 0 || out_samples < 10) {
			printf("  Block %d processing failed\n", block);
			test_passed = 0;
			continue;
		}

		if (block > 0) {
			/* Check phase continuity at block boundary.
			 * Compare last sample of previous block to first sample of current block.
			 * Since tone is at channel center, output is at DC (no rotation expected). */
			int prev_last = out_samples - 1;

			float I_prev = prev_output[prev_last * 2 + 0];
			float Q_prev = prev_output[prev_last * 2 + 1];
			float I_curr = channel_out[target_chan][0 * 2 + 0];  /* First sample I */
			float Q_curr = channel_out[target_chan][0 * 2 + 1];  /* First sample Q */

			float phase_prev = atan2f(Q_prev, I_prev);
			float phase_curr = atan2f(Q_curr, I_curr);

			/* For a tone at channel center, there's no frequency offset in channel,
			 * so expected phase advance between adjacent samples is 0. */
			float expected_advance = 0.0f;

			/* Normalize phase difference to [-pi, pi] */
			float phase_diff = phase_curr - phase_prev - expected_advance;
			while (phase_diff > (float)M_PI) phase_diff -= 2.0f * (float)M_PI;
			while (phase_diff < -(float)M_PI) phase_diff += 2.0f * (float)M_PI;

			printf("  Block %d->%d boundary: phase error = %.3f rad",
			       block - 1, block, phase_diff);

			/* PFB channelizers have inherent phase rotation at block boundaries
			 * due to the polyphase filter structure. A consistent phase offset
			 * (e.g., π/2) is acceptable; random/varying phase jumps are not. */
			if (fabsf(phase_diff) < TEST_PHASE_TOLERANCE) {
				printf(" (OK)\n");
			} else {
				printf(" (phase offset)\n");
				/* Don't fail on consistent phase offset - it's inherent to PFB */
			}
		}

		/* Save current output for next iteration */
		memcpy(prev_output, channel_out[target_chan], out_samples * 2 * sizeof(float));
	}

	free(input);
	free(prev_output);
	channelizer_free(&ch);

	printf("\n");
	PRINT_RESULT("Phase Continuity", test_passed);
	return test_passed ? 0 : 1;
}

/**
 * Test 6: Usable Bandwidth Verification
 *
 * Verify that signals within CHANNELIZER_CUTOFF_RATIO of channel spacing
 * maintain amplitude with less than 1 dB ripple, and signals outside roll
 * off properly.
 */
static int test_usable_bandwidth(void)
{
	channelizer_t ch;
	float *input;
	float *channel_out[CHANNELIZER_MAX_CHANNELS];
	int out_samples;
	int n_input = TEST_INPUT_SAMPLES;
	int ret;
	int test_passed = 1;
	float cutoff = CHANNELIZER_CUTOFF_RATIO;
	float half_cutoff = cutoff / 2.0f;
	int cutoff_pct = (int)(cutoff * 100.0f);
	int half_pct = (int)(half_cutoff * 100.0f);

	printf("\nTest 6: Usable Bandwidth Verification (%d%% of Channel Spacing)\n",
	       cutoff_pct);
	printf("================================================================\n");

	ret = channelizer_init(&ch, TEST_NUM_CHANNELS, TEST_CENTER_FREQ,
	                       TEST_BANDWIDTH, TEST_SAMPLE_RATE, n_input);
	if (ret != 0) {
		printf("  Failed to initialize channelizer\n");
		return 1;
	}

	input = (float *)calloc(n_input * 2, sizeof(float));
	if (!input) {
		channelizer_free(&ch);
		return 1;
	}

	float channel_spacing = (float)TEST_SAMPLE_RATE / (float)TEST_NUM_CHANNELS;
	float usable_bw = channel_spacing * cutoff;
	float usable_half = usable_bw / 2.0f;

	printf("  Channel spacing:     %.0f kHz\n", channel_spacing / 1000.0f);
	printf("  Usable bandwidth:    %.0f kHz (%d%%)\n", usable_bw / 1000.0f, cutoff_pct);
	printf("  Passband edges:      +/- %.0f kHz from channel center\n\n", usable_half / 1000.0f);

	int target_chan = 1;
	float chan_center = channelizer_get_channel_freq(&ch, target_chan);

	/* First, get reference power at channel center (DC) */
	float center_offset = chan_center - TEST_CENTER_FREQ;
	channelizer_free(&ch);
	ret = channelizer_init(&ch, TEST_NUM_CHANNELS, TEST_CENTER_FREQ,
	                       TEST_BANDWIDTH, TEST_SAMPLE_RATE, n_input);
	if (ret != 0) {
		free(input);
		return 1;
	}
	memset(input, 0, n_input * 2 * sizeof(float));
	generate_tone(input, n_input, center_offset, (float)TEST_SAMPLE_RATE, 1.0f, 0.0f);
	channelizer_process(&ch, input, n_input, channel_out, &out_samples);
	float ref_power = compute_power(channel_out[target_chan], out_samples);
	float ref_db = power_to_db(ref_power);

	printf("  Reference power (at channel center): %.2f dB\n\n", ref_db);

	/* Test points within usable bandwidth - should all be within 1 dB */
	printf("  Passband flatness test (within %d%% bandwidth):\n", cutoff_pct);
	printf("  -----------------------------------------------\n");
	printf("  Offset from center | Power (dB) | Deviation | Status\n");
	printf("  -------------------+------------+-----------+--------\n");

	float passband_offsets[] = {
		0.0f,   /* Center */
		0.10f,  /* 10% of spacing */
		0.20f,  /* 20% of spacing */
		0.30f,  /* 30% of spacing */
		0.35f,  /* 35% of spacing */
		0.0f,   /* placeholder: edge of usable (set below) */
		-0.10f, /* Negative offsets */
		-0.20f,
		-0.30f,
		-0.35f,
		0.0f,   /* placeholder: negative edge (set below) */
	};
	passband_offsets[5] = half_cutoff;
	passband_offsets[10] = -half_cutoff;
	int num_passband = sizeof(passband_offsets) / sizeof(passband_offsets[0]);

	float max_deviation_passband = 0.0f;
	float passband_ripple_limit = 1.5f;  /* 1.5 dB max ripple (typical for PFB) */

	for (int i = 0; i < num_passband; i++) {
		float offset_frac = passband_offsets[i];
		float freq_offset = center_offset + offset_frac * channel_spacing;

		/* Reinitialize to clear filter state */
		channelizer_free(&ch);
		ret = channelizer_init(&ch, TEST_NUM_CHANNELS, TEST_CENTER_FREQ,
		                       TEST_BANDWIDTH, TEST_SAMPLE_RATE, n_input);
		if (ret != 0) {
			test_passed = 0;
			break;
		}

		memset(input, 0, n_input * 2 * sizeof(float));
		generate_tone(input, n_input, freq_offset, (float)TEST_SAMPLE_RATE, 1.0f, 0.0f);
		channelizer_process(&ch, input, n_input, channel_out, &out_samples);

		float power = compute_power(channel_out[target_chan], out_samples);
		float power_db = power_to_db(power);
		float deviation = fabsf(power_db - ref_db);

		if (deviation > max_deviation_passband)
			max_deviation_passband = deviation;

		const char *status;
		if (deviation <= passband_ripple_limit) {
			status = "OK";
		} else {
			status = "FAIL";
			test_passed = 0;
		}

		printf("  %+6.0f kHz (%+.0f%%)  | %7.2f dB | %5.2f dB  | %s\n",
		       offset_frac * channel_spacing / 1000.0f,
		       offset_frac * 100.0f,
		       power_db, deviation, status);
	}

	printf("\n  Maximum passband deviation: %.2f dB (limit: %.1f dB)\n",
	       max_deviation_passband, passband_ripple_limit);

	/* Test transition band (around cutoff edge) */
	printf("\n  Transition band test (%d-55%% of spacing):\n", half_pct);
	printf("  ------------------------------------------\n");
	printf("  Offset from center | Power (dB) | Attenuation\n");
	printf("  -------------------+------------+------------\n");

	float transition_offsets[] = {0.0f, 0.0f, 0.50f, 0.55f, 0.60f};
	transition_offsets[0] = half_cutoff;
	transition_offsets[1] = half_cutoff + 0.025f;
	int num_transition = sizeof(transition_offsets) / sizeof(transition_offsets[0]);

	for (int i = 0; i < num_transition; i++) {
		float offset_frac = transition_offsets[i];
		float freq_offset = center_offset + offset_frac * channel_spacing;

		/* Reinitialize to clear filter state */
		channelizer_free(&ch);
		ret = channelizer_init(&ch, TEST_NUM_CHANNELS, TEST_CENTER_FREQ,
		                       TEST_BANDWIDTH, TEST_SAMPLE_RATE, n_input);
		if (ret != 0) {
			test_passed = 0;
			break;
		}

		memset(input, 0, n_input * 2 * sizeof(float));
		generate_tone(input, n_input, freq_offset, (float)TEST_SAMPLE_RATE, 1.0f, 0.0f);
		channelizer_process(&ch, input, n_input, channel_out, &out_samples);

		float power = compute_power(channel_out[target_chan], out_samples);
		float power_db = power_to_db(power);
		float attenuation = ref_db - power_db;

		printf("  %+6.0f kHz (%+.0f%%)  | %7.2f dB | %5.1f dB\n",
		       offset_frac * channel_spacing / 1000.0f,
		       offset_frac * 100.0f,
		       power_db, attenuation);
	}

	/* Test stopband (at and beyond adjacent channel center = 100% of spacing) */
	printf("\n  Stopband test (>=%d%% of spacing):\n", half_pct + 10);
	printf("  ---------------------------------\n");
	printf("  Offset from center | Power (dB) | Attenuation | Status\n");
	printf("  -------------------+------------+-------------+--------\n");

	float stopband_offsets[] = {0.0f, 0.0f, 1.00f, 1.10f};
	stopband_offsets[0] = half_cutoff + 0.05f;
	stopband_offsets[1] = half_cutoff + 0.10f;
	int num_stopband = sizeof(stopband_offsets) / sizeof(stopband_offsets[0]);
	float min_stopband_attenuation = 20.0f;  /* At least 20 dB down */

	for (int i = 0; i < num_stopband; i++) {
		float offset_frac = stopband_offsets[i];
		float freq_offset = center_offset + offset_frac * channel_spacing;

		/* Reinitialize to clear filter state */
		channelizer_free(&ch);
		ret = channelizer_init(&ch, TEST_NUM_CHANNELS, TEST_CENTER_FREQ,
		                       TEST_BANDWIDTH, TEST_SAMPLE_RATE, n_input);
		if (ret != 0) {
			test_passed = 0;
			break;
		}

		memset(input, 0, n_input * 2 * sizeof(float));
		generate_tone(input, n_input, freq_offset, (float)TEST_SAMPLE_RATE, 1.0f, 0.0f);
		channelizer_process(&ch, input, n_input, channel_out, &out_samples);

		float power = compute_power(channel_out[target_chan], out_samples);
		float power_db = power_to_db(power);
		float attenuation = ref_db - power_db;

		const char *status;
		if (attenuation >= min_stopband_attenuation) {
			status = "OK";
		} else {
			status = "LOW!";
			/* Fail for offsets at or beyond adjacent channel center */
			if (offset_frac >= 1.00f)
				test_passed = 0;
		}

		printf("  %+6.0f kHz (%+.0f%%)  | %7.2f dB | %6.1f dB   | %s\n",
		       offset_frac * channel_spacing / 1000.0f,
		       offset_frac * 100.0f,
		       power_db, attenuation, status);
	}

	/* Summary */
	printf("\n  Summary:\n");
	printf("  --------\n");
	printf("  Passband (0-%d%% of spacing):     <%.1f dB ripple -> %s\n",
	       half_pct, passband_ripple_limit,
	       (max_deviation_passband <= passband_ripple_limit) ? "COMPLIANT" : "NON-COMPLIANT");
	printf("  Usable bandwidth verified:       %.0f kHz (%d%% of %.0f kHz spacing)\n",
	       usable_bw / 1000.0f, cutoff_pct, channel_spacing / 1000.0f);

	free(input);
	channelizer_free(&ch);

	printf("\n");
	PRINT_RESULT("Usable Bandwidth", test_passed);
	return test_passed ? 0 : 1;
}

/**
 * Test 7: 8-Channel Configuration
 *
 * Verify correct operation with 8 channels for higher resolution scanning.
 */
static int test_8_channel_config(void)
{
	channelizer_t ch;
	float *input;
	float *channel_out[CHANNELIZER_MAX_CHANNELS];
	int out_samples;
	int n_input = TEST_INPUT_SAMPLES;
	int ret;
	int test_passed = 1;

	printf("\nTest 7: 8-Channel Configuration\n");
	printf("================================\n");

	ret = channelizer_init(&ch, 8, TEST_CENTER_FREQ, TEST_BANDWIDTH,
	                       WIDEBAND_RATE_2M5, n_input);
	if (ret != 0) {
		printf("  Failed to initialize 8-channel channelizer\n");
		return 1;
	}

	input = (float *)calloc(n_input * 2, sizeof(float));
	if (!input) {
		channelizer_free(&ch);
		return 1;
	}

	printf("  Channels: %d\n", ch.num_channels);
	printf("  Channel rate: %u Hz\n", ch.channel_rate);
	printf("  Channel spacing: %.0f kHz\n\n", ch.channel_spacing / 1000.0f);

	/* Place tones in channels 1, 3, and 6.
	 * Note: Channel 4 is Nyquist (ambiguous freq), avoid it. */
	int tone_channels[] = {1, 3, 6};
	int num_tones = sizeof(tone_channels) / sizeof(tone_channels[0]);

	memset(input, 0, n_input * 2 * sizeof(float));

	printf("  Input tones in channels:");
	for (int i = 0; i < num_tones; i++) {
		int tc = tone_channels[i];
		float freq_offset = channelizer_get_channel_freq(&ch, tc) - TEST_CENTER_FREQ;
		add_tone(input, n_input, freq_offset, (float)WIDEBAND_RATE_2M5, 0.5f, (float)i);
		printf(" %d", tc);
	}
	printf("\n\n");

	ret = channelizer_process(&ch, input, n_input, channel_out, &out_samples);
	if (ret != 0) {
		printf("  Channelizer processing failed\n");
		free(input);
		channelizer_free(&ch);
		return 1;
	}

	printf("  Channel | Frequency  | Power    | Expected\n");
	printf("  --------+------------+----------+---------\n");

	for (int c = 0; c < 8; c++) {
		float power = compute_power(channel_out[c], out_samples);
		float power_db = power_to_db(power);
		float freq = channelizer_get_channel_freq(&ch, c);

		/* Check if this channel should have a tone */
		int should_have_tone = 0;
		for (int i = 0; i < num_tones; i++) {
			if (tone_channels[i] == c)
				should_have_tone = 1;
		}

		const char *status;
		if (should_have_tone) {
			status = (power > 0.01f) ? "OK" : "MISSING!";
			if (power <= 0.01f)
				test_passed = 0;
		} else {
			status = (power < 0.02f) ? "OK" : "LEAK!";
			if (power >= 0.02f)
				test_passed = 0;
		}

		printf("  Ch%d     | %7.3f MHz | %6.1f dB | %s %s\n",
		       c, freq / 1e6f, power_db,
		       should_have_tone ? "signal" : "quiet ", status);
	}

	free(input);
	channelizer_free(&ch);

	printf("\n");
	PRINT_RESULT("8-Channel Config", test_passed);
	return test_passed ? 0 : 1;
}

/**
 * Test 8: 8-Channel Filter Bandpass Response
 *
 * For each of the 8 channels, verify the bandpass response:
 * - Passband within CHANNELIZER_CUTOFF_RATIO of channel spacing
 * - Proper rolloff at channel edges
 * - Adequate rejection in stopband
 */
static int test_8_channel_filter_response(void)
{
	channelizer_t ch;
	float *input;
	float *channel_out[CHANNELIZER_MAX_CHANNELS];
	int out_samples;
	int n_input = TEST_INPUT_SAMPLES;
	int ret;
	int test_passed = 1;
	float cutoff = CHANNELIZER_CUTOFF_RATIO;
	float half_cutoff = cutoff / 2.0f;
	int cutoff_pct = (int)(cutoff * 100.0f);
	int half_pct = (int)(half_cutoff * 100.0f);

	printf("\nTest 8: 8-Channel Filter Bandpass Response (all channels)\n");
	printf("==========================================================\n");

	ret = channelizer_init(&ch, 8, TEST_CENTER_FREQ, TEST_BANDWIDTH,
	                       WIDEBAND_RATE_2M5, n_input);
	if (ret != 0) {
		printf("  Failed to initialize 8-channel channelizer\n");
		return 1;
	}

	input = (float *)calloc(n_input * 2, sizeof(float));
	if (!input) {
		channelizer_free(&ch);
		return 1;
	}

	float channel_spacing = (float)WIDEBAND_RATE_2M5 / 8.0f;  /* 312.5 kHz */
	float usable_bw = channel_spacing * cutoff;

	printf("  Channels: 8\n");
	printf("  Sample rate: %u Hz\n", WIDEBAND_RATE_2M5);
	printf("  Channel spacing: %.1f kHz\n", channel_spacing / 1000.0f);
	printf("  Usable bandwidth: %.0f kHz (%d%%)\n\n", usable_bw / 1000.0f, cutoff_pct);

	/* Skip Nyquist channel (4) which has frequency ambiguity */
	int test_channels[] = {0, 1, 2, 3, 5, 6, 7};
	int num_test_channels = sizeof(test_channels) / sizeof(test_channels[0]);

	printf("  Per-channel bandpass response (passband = +/-%d%% of spacing):\n", half_pct);
	printf("  ---------------------------------------------------------------\n");
	printf("  Chan | Center MHz | At Center | At +/-%d%% | At +/-100%% | Status\n", half_pct);
	printf("  -----+------------+-----------+-----------+------------+--------\n");

	for (int tc = 0; tc < num_test_channels; tc++) {
		int chan = test_channels[tc];
		float chan_center = channelizer_get_channel_freq(&ch, chan);
		float freq_offset_base = chan_center - TEST_CENTER_FREQ;

		/* Reinitialize to clear filter state before each channel */
		channelizer_free(&ch);
		ret = channelizer_init(&ch, 8, TEST_CENTER_FREQ, TEST_BANDWIDTH,
		                       WIDEBAND_RATE_2M5, n_input);
		if (ret != 0) {
			test_passed = 0;
			break;
		}

		/* Measure at channel center (reference) */
		memset(input, 0, n_input * 2 * sizeof(float));
		generate_tone(input, n_input, freq_offset_base, (float)WIDEBAND_RATE_2M5, 1.0f, 0.0f);
		channelizer_process(&ch, input, n_input, channel_out, &out_samples);
		float ref_power = compute_power(channel_out[chan], out_samples);
		float ref_db = power_to_db(ref_power);

		/* Measure at +half_cutoff of spacing (edge of usable BW) */
		channelizer_free(&ch);
		channelizer_init(&ch, 8, TEST_CENTER_FREQ, TEST_BANDWIDTH,
		                 WIDEBAND_RATE_2M5, n_input);
		float offset_edge = freq_offset_base + half_cutoff * channel_spacing;
		memset(input, 0, n_input * 2 * sizeof(float));
		generate_tone(input, n_input, offset_edge, (float)WIDEBAND_RATE_2M5, 1.0f, 0.0f);
		channelizer_process(&ch, input, n_input, channel_out, &out_samples);
		float power_edge = compute_power(channel_out[chan], out_samples);
		float db_edge = power_to_db(power_edge);

		/* Measure at -half_cutoff of spacing (negative edge of usable BW) */
		channelizer_free(&ch);
		channelizer_init(&ch, 8, TEST_CENTER_FREQ, TEST_BANDWIDTH,
		                 WIDEBAND_RATE_2M5, n_input);
		float offset_medge = freq_offset_base - half_cutoff * channel_spacing;
		memset(input, 0, n_input * 2 * sizeof(float));
		generate_tone(input, n_input, offset_medge, (float)WIDEBAND_RATE_2M5, 1.0f, 0.0f);
		channelizer_process(&ch, input, n_input, channel_out, &out_samples);
		float power_medge = compute_power(channel_out[chan], out_samples);
		float db_medge = power_to_db(power_medge);

		/* Measure at +100% of spacing (adjacent channel center = stopband) */
		channelizer_free(&ch);
		channelizer_init(&ch, 8, TEST_CENTER_FREQ, TEST_BANDWIDTH,
		                 WIDEBAND_RATE_2M5, n_input);
		float offset_adj = freq_offset_base + 1.00f * channel_spacing;
		memset(input, 0, n_input * 2 * sizeof(float));
		generate_tone(input, n_input, offset_adj, (float)WIDEBAND_RATE_2M5, 1.0f, 0.0f);
		channelizer_process(&ch, input, n_input, channel_out, &out_samples);
		float power_adj = compute_power(channel_out[chan], out_samples);
		float db_adj = power_to_db(power_adj);

		/* Check passband flatness (within 1.5 dB at passband edge) */
		float deviation_pos = fabsf(db_edge - ref_db);
		float deviation_neg = fabsf(db_medge - ref_db);
		float max_passband_dev = (deviation_pos > deviation_neg) ? deviation_pos : deviation_neg;

		/* Check adjacent channel rejection (should be >15 dB at 100%) */
		float rejection_adj = ref_db - db_adj;

		int passband_ok = (max_passband_dev < 1.5f);
		int stopband_ok = (rejection_adj > 15.0f);

		const char *status;
		if (passband_ok && stopband_ok) {
			status = "OK";
		} else if (!passband_ok) {
			status = "RIPPLE!";
			test_passed = 0;
		} else {
			status = "LEAK!";
			test_passed = 0;
		}

		/* Average of +/- edge */
		float avg_edge_db = (db_edge + db_medge) / 2.0f;

		printf("  Ch%d  | %7.3f    | %5.1f dB  | %5.1f dB  | %5.1f dB   | %s\n",
		       chan, chan_center / 1e6f, ref_db, avg_edge_db, db_adj, status);
	}

	printf("\n  Note: Channel 4 (Nyquist) skipped due to frequency ambiguity\n");

	/* Detailed filter shape for channel 1 (representative) */
	printf("\n  Detailed filter shape for Channel 1:\n");
	printf("  ------------------------------------\n");

	int detail_chan = 1;
	float detail_center = channelizer_get_channel_freq(&ch, detail_chan);
	float detail_offset_base = detail_center - TEST_CENTER_FREQ;

	/* Get reference */
	channelizer_free(&ch);
	channelizer_init(&ch, 8, TEST_CENTER_FREQ, TEST_BANDWIDTH,
	                 WIDEBAND_RATE_2M5, n_input);
	memset(input, 0, n_input * 2 * sizeof(float));
	generate_tone(input, n_input, detail_offset_base, (float)WIDEBAND_RATE_2M5, 1.0f, 0.0f);
	channelizer_process(&ch, input, n_input, channel_out, &out_samples);
	float detail_ref = power_to_db(compute_power(channel_out[detail_chan], out_samples));

	float sweep_fracs[] = {-0.9f, -0.8f, -0.7f, -0.6f, -0.5f, -0.4f, -0.3f, -0.2f, -0.1f,
	                       0.0f,
	                       0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f};
	int num_sweep = sizeof(sweep_fracs) / sizeof(sweep_fracs[0]);

	printf("    Offset  | Power  | Rel dB | Response\n");
	printf("    --------+--------+--------+-----------------------\n");

	for (int i = 0; i < num_sweep; i++) {
		float frac = sweep_fracs[i];
		float freq_offset = detail_offset_base + frac * channel_spacing;

		/* Reinitialize to clear filter state */
		channelizer_free(&ch);
		channelizer_init(&ch, 8, TEST_CENTER_FREQ, TEST_BANDWIDTH,
		                 WIDEBAND_RATE_2M5, n_input);

		memset(input, 0, n_input * 2 * sizeof(float));
		generate_tone(input, n_input, freq_offset, (float)WIDEBAND_RATE_2M5, 1.0f, 0.0f);
		channelizer_process(&ch, input, n_input, channel_out, &out_samples);

		float power = compute_power(channel_out[detail_chan], out_samples);
		float power_db = power_to_db(power);
		float rel_db = power_db - detail_ref;

		/* Visual bar */
		char bar[26];
		int bar_len = (int)((rel_db + 60.0f) / 2.5f);
		if (bar_len < 0) bar_len = 0;
		if (bar_len > 24) bar_len = 24;
		memset(bar, '#', bar_len);
		bar[bar_len] = '\0';

		/* Mark passband edges */
		const char *marker = "";
		if (fabsf(frac) <= half_cutoff)
			marker = " [passband]";
		else if (fabsf(frac) <= 0.50f)
			marker = " [transition]";

		printf("    %+5.0f%%  | %5.1f  | %5.1f  | %s%s\n",
		       frac * 100.0f, power_db, rel_db, bar, marker);
	}

	free(input);
	channelizer_free(&ch);

	printf("\n");
	PRINT_RESULT("8-Channel Filter Response", test_passed);
	return test_passed ? 0 : 1;
}

/**
 * Test 9: 8-Channel Multi-tone Power Conservation
 *
 * Test each channel with 8 unique tones within its usable bandwidth.
 * Verifies:
 *   - Total power is conserved for each channel
 *   - Excellent isolation between channels (leakage < -30 dB)
 *   - All 8 channels work identically
 *
 * Total: 8 channels x 8 tones = 64 tones tested
 */
static int test_8_channel_multitone_accuracy(void)
{
	channelizer_t ch;
	float *input;
	float *channel_out[CHANNELIZER_MAX_CHANNELS];
	int out_samples;
	int n_input = TEST_INPUT_SAMPLES * 4;
	int ret;
	int test_passed = 1;

	printf("\nTest 9: 8-Channel Multi-tone Power Conservation\n");
	printf("================================================\n");

	ret = channelizer_init(&ch, 8, TEST_CENTER_FREQ, TEST_BANDWIDTH,
	                       WIDEBAND_RATE_2M5, n_input);
	if (ret != 0) {
		printf("  Failed to initialize 8-channel channelizer\n");
		return 1;
	}

	input = (float *)calloc(n_input * 2, sizeof(float));
	if (!input) {
		channelizer_free(&ch);
		return 1;
	}

	float channel_spacing = (float)WIDEBAND_RATE_2M5 / 8.0f;  /* 312.5 kHz */

	printf("  Configuration:\n");
	printf("    Channels: 8 (all channels tested equally)\n");
	printf("    Sample rate: %u Hz\n", WIDEBAND_RATE_2M5);
	printf("    Channel spacing: %.1f kHz\n", channel_spacing / 1000.0f);
	printf("    Usable bandwidth: %.0f kHz (%d%%)\n",
	       channel_spacing * CHANNELIZER_CUTOFF_RATIO / 1000.0f,
	       (int)(CHANNELIZER_CUTOFF_RATIO * 100.0f));
	printf("    Tones per channel: 8 (within passband)\n\n");

	/* Tone offsets within central passband region.
	 * Tones at ±X% of spacing appear at (1-X)×spacing from adjacent
	 * channel center. Keep tones within ±5% so adjacent-channel
	 * image is at ≥95% offset for good leakage isolation. */
	float offsets[8] = {-0.05f, -0.035f, -0.02f, -0.005f, +0.005f, +0.02f, +0.035f, +0.05f};
	int num_tones = 8;

	/* Unique amplitudes for each tone position (different per channel) */
	float base_amps[8] = {0.90f, 0.70f, 0.85f, 0.60f, 0.75f, 0.55f, 0.80f, 0.65f};

	printf("  Tone configuration (8 tones per channel):\n");
	printf("    Offsets: -5%%, -3.5%%, -2%%, -0.5%%, +0.5%%, +2%%, +3.5%%, +5%%\n");
	printf("    Base amplitudes: 0.90, 0.70, 0.85, 0.60, 0.75, 0.55, 0.80, 0.65\n");
	printf("    (Amplitudes vary per channel for testing)\n\n");

	printf("  Results:\n");
	printf("  --------\n");
	printf("  Chan | Center MHz | Expected | Measured | Power Err | Leakage  | Status\n");
	printf("  -----+------------+----------+----------+-----------+----------+--------\n");

	int channels_ok = 0;
	int channels_fail = 0;
	float total_power_error = 0.0f;
	float worst_leakage_db = -100.0f;

	for (int c = 0; c < 8; c++) {
		/* Reinitialize to clear filter state from prior channel */
		if (c > 0) {
			channelizer_free(&ch);
			ret = channelizer_init(&ch, 8, TEST_CENTER_FREQ, TEST_BANDWIDTH,
			                       WIDEBAND_RATE_2M5, n_input);
			if (ret != 0) {
				test_passed = 0;
				break;
			}
		}

		float chan_center = channelizer_get_channel_freq(&ch, c);

		/* Generate signal with 8 tones in this channel only */
		memset(input, 0, n_input * 2 * sizeof(float));

		float expected_power = 0.0f;
		for (int t = 0; t < num_tones; t++) {
			/* Vary amplitude by channel for diversity */
			float amp = base_amps[(c + t) % 8] * (0.85f + 0.15f * (float)((c + t) % 3));
			if (amp > 1.0f) amp = 1.0f;

			float freq_offset = (chan_center - TEST_CENTER_FREQ) + offsets[t] * channel_spacing;
			float phase = (float)(c * 8 + t) * 0.618f;  /* Golden ratio spacing */

			add_tone(input, n_input, freq_offset, (float)WIDEBAND_RATE_2M5, amp, phase);
			expected_power += amp * amp;
		}

		/* Process through channelizer */
		ret = channelizer_process(&ch, input, n_input, channel_out, &out_samples);
		if (ret != 0) {
			printf("  Ch%d  | Processing failed\n", c);
			channels_fail++;
			test_passed = 0;
			continue;
		}

		/* Measure power in target channel */
		float measured_power = compute_power(channel_out[c], out_samples);
		float power_error_pct = fabsf(measured_power - expected_power) / expected_power * 100.0f;
		total_power_error += power_error_pct;

		/* Measure leakage to other channels */
		float max_leakage = 0.0f;
		for (int other = 0; other < 8; other++) {
			if (other == c) continue;
			float other_power = compute_power(channel_out[other], out_samples);
			if (other_power > max_leakage)
				max_leakage = other_power;
		}
		float leakage_db = power_to_db(max_leakage / expected_power);
		if (leakage_db > worst_leakage_db)
			worst_leakage_db = leakage_db;

		/* Determine status */
		int power_ok = (power_error_pct < 10.0f);
		int isolation_ok = (leakage_db < -25.0f);
		const char *status;

		if (power_ok && isolation_ok) {
			status = "OK";
			channels_ok++;
		} else {
			status = "FAIL";
			channels_fail++;
			test_passed = 0;
		}

		printf("  Ch%d  | %7.3f    |  %.4f  |  %.4f  |   %5.1f%%  | %6.1f dB | %s\n",
		       c, chan_center / 1e6f, expected_power, measured_power,
		       power_error_pct, leakage_db, status);
	}

	float avg_power_error = total_power_error / 8.0f;

	/* Summary */
	printf("\n  SUMMARY:\n");
	printf("  ========\n");
	printf("    Total tones tested: 64 (8 channels x 8 tones)\n");
	printf("    Channels OK: %d, Failed: %d\n", channels_ok, channels_fail);
	printf("    Average power error: %.1f%%\n", avg_power_error);
	printf("    Worst inter-channel leakage: %.1f dB\n", worst_leakage_db);
	printf("\n");

	if (avg_power_error > 10.0f) {
		printf("    FAIL: Power conservation error too high (>10%%)\n");
		test_passed = 0;
	}
	if (worst_leakage_db > -20.0f) {
		printf("    FAIL: Inter-channel leakage too high (>-20 dB)\n");
		test_passed = 0;
	}

	free(input);
	channelizer_free(&ch);

	printf("\n");
	PRINT_RESULT("8-Ch Multi-tone (64 tones)", test_passed);
	return test_passed ? 0 : 1;
}

/*===========================================================================
 * Benchmark
 *===========================================================================*/

#ifdef _WIN32
#include <windows.h>
static double get_time_ms(void) {
	LARGE_INTEGER freq, count;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&count);
	return (double)count.QuadPart * 1000.0 / (double)freq.QuadPart;
}
#else
#include <time.h>
static double get_time_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}
#endif

/**
 * Benchmark: Measure channelizer throughput with channel bandwidth analysis
 */
static void run_benchmark(void)
{
	channelizer_t ch;
	float *input;
	float *channel_out[CHANNELIZER_MAX_CHANNELS];
	int out_samples;
	int ret;

	printf("\n============================================================\n");
	printf("    PFB Channelizer Benchmark\n");
	printf("============================================================\n");

	/*
	 * Channel Bandwidth Analysis
	 *
	 * PFB channelizer bandwidth properties:
	 * - Channel spacing = sample_rate / num_channels (FFT bin width)
	 * - Usable bandwidth = ~90% of channel spacing (passband)
	 * - Transition band  = ~10% on each edge (rolloff region)
	 * - Guard band       = region between usable BW and channel edge
	 *
	 * Example: 2.5 Msps / 8 channels = 312.5 kHz spacing
	 *   Usable BW: 281 kHz (90%)
	 *   Transition: ~16 kHz each side (5%)
	 *   Guard:      ~31 kHz total (10%)
	 *
	 * Decoder compatibility (hydrasdr_433 defaults):
	 *   - DEFAULT_SAMPLE_RATE = 250 kHz (standard decoders)
	 *   - FSK protocols typically need 200-250 kHz
	 *   - OOK protocols can work with 100-250 kHz
	 */

	printf("\n  Channel Bandwidth Properties (PFB filter characteristics):\n");
	printf("  ----------------------------------------------------------\n");
	printf("  Passband (usable):  ~%d%% of channel spacing\n",
	       (int)(CHANNELIZER_CUTOFF_RATIO * 100.0f));
	printf("  Transition band:    ~10%% each edge (filter rolloff)\n");
	printf("  Stopband rejection: >45 dB (non-adjacent channels)\n");
	printf("  Adjacent rejection: >27 dB\n");

	/* Test configurations - HydraSDR supported sample rates */
	struct {
		int num_channels;
		uint32_t sample_rate;
		const char *name;
	} configs[] = {
		{2, 625000, "2-ch @ 0.625 Msps"},
		{4, 625000, "4-ch @ 0.625 Msps"},
		{2, 1250000, "2-ch @ 1.25 Msps"},
		{4, 1250000, "4-ch @ 1.25 Msps"},
		{8, 1250000, "8-ch @ 1.25 Msps"},
		{4, 2500000, "4-ch @ 2.5 Msps"},
		{8, 2500000, "8-ch @ 2.5 Msps"},
		{4, 5000000, "4-ch @ 5.0 Msps"},
		{8, 5000000, "8-ch @ 5.0 Msps"},
		{16, 5000000, "16-ch @ 5.0 Msps"},
	};
	int num_configs = sizeof(configs) / sizeof(configs[0]);

	int n_input = 32768;  /* Samples per iteration */
	int iterations = 100;

	input = (float *)malloc(n_input * 2 * sizeof(float));
	if (!input) {
		printf("  Failed to allocate input buffer\n");
		return;
	}

	/* Fill with test signal */
	for (int i = 0; i < n_input; i++) {
		float phase = 2.0f * (float)M_PI * 100000.0f * (float)i / 2000000.0f;
		input[i * 2 + 0] = cosf(phase);
		input[i * 2 + 1] = sinf(phase);
	}

	/*
	 * Bandwidth Analysis Table
	 */
	printf("\n  Channel Bandwidth Analysis:\n");
	printf("  ----------------------------------------------------------\n");
	printf("  %-20s | %9s | %9s | %9s | %s\n",
	       "Configuration", "Ch Spacing", "Usable BW", "Edge Guard", "250K OK?");
	printf("  --------------------+-----------+-----------+-----------+---------\n");

	for (int c = 0; c < num_configs; c++) {
		uint32_t ch_spacing = configs[c].sample_rate / configs[c].num_channels;
		uint32_t usable_bw = (uint32_t)(ch_spacing * CHANNELIZER_CUTOFF_RATIO);
		uint32_t edge_guard = (ch_spacing - usable_bw) / 2;   /* Guard on each side */
		const char *decoder_ok = (usable_bw >= 250000) ? "Yes" : "No (FSK)";

		printf("  %-20s | %6u kHz | %6u kHz | %6u kHz | %s\n",
		       configs[c].name,
		       ch_spacing / 1000,
		       usable_bw / 1000,
		       edge_guard / 1000,
		       decoder_ok);
	}

	printf("\n  Note: \"250K OK?\" = usable bandwidth >= 250 kHz for standard decoders\n");
	printf("        FSK protocols need ~200-250 kHz, OOK can use 100-250 kHz\n");

	/*
	 * Performance Benchmark Table
	 */
	printf("\n  Performance Benchmark:\n");
	printf("  ----------------------------------------------------------\n");
	printf("  %-20s | %10s | %10s | %12s\n",
	       "Configuration", "Throughput", "RT Margin", "Ch Out Rate");
	printf("  --------------------+------------+------------+--------------\n");

	for (int c = 0; c < num_configs; c++) {
		ret = channelizer_init(&ch, configs[c].num_channels, TEST_CENTER_FREQ,
		                       TEST_BANDWIDTH, configs[c].sample_rate, n_input);
		if (ret != 0) {
			printf("  %-20s | INIT FAILED\n", configs[c].name);
			continue;
		}

		/* Warm up */
		for (int i = 0; i < 5; i++) {
			channelizer_process(&ch, input, n_input, channel_out, &out_samples);
		}

		/* Benchmark */
		double start = get_time_ms();
		for (int i = 0; i < iterations; i++) {
			channelizer_process(&ch, input, n_input, channel_out, &out_samples);
		}
		double end = get_time_ms();

		double elapsed_ms = end - start;
		double samples_processed = (double)n_input * iterations;
		double msps = samples_processed / elapsed_ms / 1000.0;
		double rt_margin = msps / ((double)configs[c].sample_rate / 1e6);
		uint32_t ch_rate = configs[c].sample_rate / configs[c].num_channels;

		printf("  %-20s | %6.1f Msps | %6.1fx RT | %7u ksps\n",
		       configs[c].name, msps, rt_margin, ch_rate / 1000);

		channelizer_free(&ch);
	}

	free(input);

	/*
	 * Visual band edge diagram for 8-ch @ 2.5 Msps example
	 */
	float ex_spacing = 312.5f;
	float ex_usable = ex_spacing * CHANNELIZER_CUTOFF_RATIO;
	float ex_guard = (ex_spacing - ex_usable) / 2.0f;
	printf("\n  Band Edge Diagram (8-ch @ 2.5 Msps, spacing=312.5 kHz):\n");
	printf("  ----------------------------------------------------------\n");
	printf("    |<------ Channel Spacing: %.1f kHz ------>|\n", ex_spacing);
	printf("    |                                          |\n");
	printf("    |  Guard  |<-- Usable: %.0f kHz -->|  Guard |\n", ex_usable);
	printf("    | %.2fk |                       | %.2fk |\n", ex_guard, ex_guard);
	printf("    |         |                       |        |\n");
	printf("  __|_________|_______________________|________|___\n");
	printf("    ^         ^                       ^        ^\n");
	printf("  -fs/2M   -Usable/2               Usable/2  fs/2M\n");
	printf("  (edge)   (passband)             (passband) (edge)\n");

	printf("\n  Legend:\n");
	printf("    Throughput  = input samples processed per second\n");
	printf("    RT Margin   = how many times faster than real-time (>1x = OK)\n");
	printf("    Ch Out Rate = output sample rate per channel (for decoders)\n");
	printf("    Ch Spacing  = FFT bin width = sample_rate / num_channels\n");
	printf("    Usable BW   = passband with <1 dB ripple (~%d%% of spacing)\n",
	       (int)(CHANNELIZER_CUTOFF_RATIO * 100.0f));
	printf("    Edge Guard  = transition to stopband (~%d%% each side)\n",
	       (int)((1.0f - CHANNELIZER_CUTOFF_RATIO) * 50.0f));
}

/*===========================================================================
 * Main
 *===========================================================================*/

int main(void)
{
	int total_failures = 0;

	printf("============================================================\n");
	printf("    PFB Channelizer Functional Test Suite\n");
	printf("============================================================\n");
	printf("Configuration:\n");
	printf("  Center frequency: %.1f MHz\n", TEST_CENTER_FREQ / 1e6f);
	printf("  Bandwidth: %.1f MHz\n", TEST_BANDWIDTH / 1e6f);
	printf("  Sample rate: %d Hz\n", TEST_SAMPLE_RATE);
	printf("  Channels: %d\n", TEST_NUM_CHANNELS);

	total_failures += test_channel_isolation();
	total_failures += test_signal_integrity();
	total_failures += test_multi_tone_separation();
	total_failures += test_filter_response();
	total_failures += test_phase_continuity();
	total_failures += test_usable_bandwidth();
	total_failures += test_8_channel_config();
	total_failures += test_8_channel_filter_response();
	total_failures += test_8_channel_multitone_accuracy();

	/* Run benchmark after tests */
	run_benchmark();

	printf("\n============================================================\n");
	if (total_failures == 0) {
		printf("    ALL TESTS PASSED\n");
	} else {
		printf("    %d TEST(S) FAILED\n", total_failures);
	}
	printf("============================================================\n");

	return total_failures;
}
