/** @file
    Filter characterization test for PFB channelizer.

    Measures and displays:
    - Full frequency response from DC to Nyquist
    - Passband flatness (should be ~0 dB)
    - Transition band rolloff
    - Stopband rejection (in dB)
    - Adjacent and non-adjacent channel isolation

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

#define TEST_CENTER_FREQ    868.5e6f
#define TEST_BANDWIDTH      2e6f
#define TEST_INPUT_SAMPLES  16384

static float *alloc_cf32(int n_samples)
{
	return (float *)calloc((size_t)n_samples * 2, sizeof(float));
}

static void generate_tone(float *buf, int n_samples, float freq_offset, float sample_rate)
{
	for (int i = 0; i < n_samples; i++) {
		float phase = 2.0f * (float)M_PI * freq_offset * (float)i / sample_rate;
		buf[i * 2 + 0] = cosf(phase);
		buf[i * 2 + 1] = sinf(phase);
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
	if (power < 1e-20f)
		return -200.0f;
	return 10.0f * log10f(power);
}

/**
 * Full filter characterization for given channel count and sample rate.
 */
static void characterize_filter(int num_channels, uint32_t sample_rate)
{
	channelizer_t ch;
	int n_input = TEST_INPUT_SAMPLES;

	printf("\n");
	printf("================================================================\n");
	printf("  FILTER CHARACTERIZATION: %d channels @ %.1f MSps\n",
	       num_channels, (float)sample_rate / 1e6f);
	printf("================================================================\n");

	if (channelizer_init(&ch, num_channels, TEST_CENTER_FREQ, TEST_BANDWIDTH,
	                     sample_rate, (size_t)n_input) != 0) {
		printf("  ERROR: Failed to initialize channelizer\n");
		return;
	}

	float spacing = ch.channel_spacing;
	float nyquist = spacing / 2.0f;

	printf("\n");
	printf("  Configuration:\n");
	printf("    Channel spacing:  %.1f kHz\n", spacing / 1000.0f);
	printf("    Channel Nyquist:  %.1f kHz\n", nyquist / 1000.0f);
	printf("    Taps per branch:  %d\n", ch.taps_per_branch);
	printf("    Total filter taps: %d\n", ch.total_taps);
	printf("\n");

	float *input = alloc_cf32(n_input);
	if (!input) {
		channelizer_free(&ch);
		return;
	}

	/*
	 * FULL FREQUENCY RESPONSE SWEEP (both sides around DC)
	 * Sweep a tone across Channel 0 (DC) and measure response.
	 * Shows passband flatness and transition/stopband rolloff.
	 */
	printf("  ================================================================\n");
	printf("  CHANNEL 0 (DC) FREQUENCY RESPONSE (both +/- sides)\n");
	printf("  ================================================================\n");
	printf("  Offset from DC     Power      Response (dB)\n");
	printf("  ----------------------------------------------------------------\n");

	float dc_power = 0.0f;
	float passband_3db_pos = 0.0f;
	float passband_3db_neg = 0.0f;
	float passband_6db = 0.0f;

	/* First sweep negative side from -200% to 0% of Nyquist */
	for (int pct = -200; pct <= 200; pct += 10) {
		float offset = (float)pct / 100.0f * nyquist;

		/* Reinitialize to clear state */
		channelizer_free(&ch);
		if (channelizer_init(&ch, num_channels, TEST_CENTER_FREQ, TEST_BANDWIDTH,
		                     sample_rate, (size_t)n_input) != 0) {
			free(input);
			return;
		}

		generate_tone(input, n_input, offset, (float)sample_rate);

		float *channel_out[CHANNELIZER_MAX_CHANNELS];
		int out_samples;
		channelizer_process(&ch, input, n_input, channel_out, &out_samples);

		float power = compute_power(channel_out[0], out_samples);

		/* Store DC power as reference */
		if (pct == 0)
			dc_power = power;

		/* Use dc_power as reference only after we've measured DC */
		float rel_db = (dc_power > 0.0f) ?
		               power_to_db(power) - power_to_db(dc_power) :
		               power_to_db(power);

		/* Track -3dB points on positive and negative sides.
		 * Since we sweep from negative to positive, track where we first
		 * exceed -3dB on the negative side, and first drop below -3dB on positive. */
		if (pct > 0 && passband_3db_pos == 0.0f && rel_db < -3.0f)
			passband_3db_pos = offset;
		if (pct < 0 && passband_3db_neg == 0.0f && rel_db >= -3.0f)
			passband_3db_neg = offset;  /* First point above -3dB from negative edge */
		if (passband_6db == 0.0f && rel_db < -6.0f && pct > 0)
			passband_6db = offset;

		/* Visual indicator centered on 0 dB */
		char bar[51];
		int bar_len = (int)((rel_db + 50.0f) / 50.0f * 50.0f);
		if (bar_len < 0) bar_len = 0;
		if (bar_len > 50) bar_len = 50;
		memset(bar, '#', (size_t)bar_len);
		bar[bar_len] = '\0';

		printf("  %+7.1f kHz (%+4d%%)  %.4f  %+6.1f dB  %s\n",
		       offset / 1000.0f, pct, power, rel_db, bar);
	}
	(void)passband_6db;  /* Suppress unused warning */

	printf("  ----------------------------------------------------------------\n");
	printf("  -3 dB point (positive): +%.1f kHz (+%.0f%% of Nyquist)\n",
	       passband_3db_pos / 1000.0f, 100.0f * passband_3db_pos / nyquist);
	printf("  -3 dB point (negative): %.1f kHz (%.0f%% of Nyquist)\n",
	       passband_3db_neg / 1000.0f, 100.0f * passband_3db_neg / nyquist);
	printf("  Usable BW:   %.1f to +%.1f kHz (%.0f%% of channel spacing)\n",
	       passband_3db_neg / 1000.0f, passband_3db_pos / 1000.0f,
	       100.0f * (passband_3db_pos - passband_3db_neg) / spacing);

	/*
	 * ADJACENT CHANNEL ISOLATION
	 * Inject tone at channel centers, measure leakage to adjacent channels.
	 */
	printf("\n");
	printf("  ================================================================\n");
	printf("  ADJACENT CHANNEL ISOLATION\n");
	printf("  ================================================================\n");
	printf("  Tone at center of each channel, measuring leakage to others.\n");
	printf("  ----------------------------------------------------------------\n");

	for (int target = 0; target < num_channels; target++) {
		float freq_offset;
		if (target == 0) {
			freq_offset = 0.0f;
		} else if (target <= num_channels / 2) {
			freq_offset = (float)target * spacing;
		} else {
			freq_offset = (float)(target - num_channels) * spacing;
		}

		channelizer_free(&ch);
		channelizer_init(&ch, num_channels, TEST_CENTER_FREQ, TEST_BANDWIDTH,
		                 sample_rate, (size_t)n_input);

		generate_tone(input, n_input, freq_offset, (float)sample_rate);

		float *channel_out[CHANNELIZER_MAX_CHANNELS];
		int out_samples;
		channelizer_process(&ch, input, n_input, channel_out, &out_samples);

		float target_power = compute_power(channel_out[target], out_samples);
		float target_db = power_to_db(target_power);

		printf("  Ch%d (%.0f kHz): power=%.4f (%.1f dB)\n",
		       target, freq_offset / 1000.0f, target_power, target_db);

		/* Measure isolation to all other channels */
		for (int other = 0; other < num_channels; other++) {
			if (other == target)
				continue;

			float other_power = compute_power(channel_out[other], out_samples);
			float isolation = target_db - power_to_db(other_power);

			const char *label = "";
			int diff = abs(other - target);
			if (diff == 1 || diff == num_channels - 1)
				label = " (adjacent)";
			else if (diff == num_channels / 2)
				label = " (Nyquist)";

			printf("    -> Ch%d: %.6f (%.1f dB isolation)%s\n",
			       other, other_power, isolation, label);
		}
		printf("\n");
	}

	/*
	 * INTER-CHANNEL RESPONSE (Tone between channel centers)
	 * Shows how the filter handles signals at channel boundaries.
	 */
	printf("  ================================================================\n");
	printf("  INTER-CHANNEL RESPONSE (tone between Ch0 and Ch1)\n");
	printf("  ================================================================\n");
	printf("  Offset         Ch0 Power    Ch1 Power    Notes\n");
	printf("  ----------------------------------------------------------------\n");

	for (int pct = 0; pct <= 100; pct += 10) {
		float offset = (float)pct / 100.0f * spacing;

		channelizer_free(&ch);
		channelizer_init(&ch, num_channels, TEST_CENTER_FREQ, TEST_BANDWIDTH,
		                 sample_rate, (size_t)n_input);

		generate_tone(input, n_input, offset, (float)sample_rate);

		float *channel_out[CHANNELIZER_MAX_CHANNELS];
		int out_samples;
		channelizer_process(&ch, input, n_input, channel_out, &out_samples);

		float p0 = compute_power(channel_out[0], out_samples);
		float p1 = compute_power(channel_out[1], out_samples);

		const char *note = "";
		if (pct == 0) note = "<-- Ch0 center";
		else if (pct == 50) note = "<-- Halfway (Nyquist)";
		else if (pct == 100) note = "<-- Ch1 center";

		printf("  %5.0f kHz (%3d%%)  %.4f       %.4f       %s\n",
		       offset / 1000.0f, pct, p0, p1, note);
	}

	printf("  ----------------------------------------------------------------\n");
	printf("  Note: Total power should be ~1.0 (energy conservation)\n");

	free(input);
	channelizer_free(&ch);
}

int main(void)
{
	printf("\n");
	printf("****************************************************************\n");
	printf("*        PFB CHANNELIZER FILTER CHARACTERIZATION               *\n");
	printf("****************************************************************\n");

	/* Test common configurations */
	characterize_filter(4, 2000000);
	characterize_filter(8, WIDEBAND_RATE_2M5);

	printf("\n");
	printf("================================================================\n");
	printf("  SUMMARY - KEY FILTER METRICS\n");
	printf("================================================================\n");
	printf("\n");
	printf("  Target specifications:\n");
	printf("    - Passband flatness:      0 dB ±0.5 dB within usable BW\n");
	printf("    - -3dB bandwidth:         ~%.0f%% of channel Nyquist\n",
	       CHANNELIZER_CUTOFF_RATIO * 100.0f);
	printf("    - Adjacent ch rejection:  >38 dB (for clean decoding)\n");
	printf("    - Non-adjacent rejection: >49 dB\n");
	printf("\n");
	printf("  Actual filter: m=%d (taps/branch=%d), cutoff=%.0f%% Nyquist\n",
	       24, 48, CHANNELIZER_CUTOFF_RATIO * 100.0f);
	printf("\n");

	return 0;
}
