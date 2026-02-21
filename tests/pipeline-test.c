/** @file
    End-to-end pipeline test: channelizer -> resampler.

    Validates that signals survive the full wideband processing chain.
    This test was created because the original channelizer and resampler
    tests ran in isolation and did not catch a real-world failure where
    the resampler's anti-aliasing filter killed signals that the PFB
    channelizer passed cleanly.

    Real-world scenario that failed:
      -B 433.8M:2M:8  (8 channels @ 2.5 MSps, spacing=312.5 kHz)
      Signal at 433.92 MHz -> +120 kHz from Ch0 center (433.8 MHz)
      PFB passband: +/-140 kHz (90% of +/-156.25 kHz) -> signal passes
      Resampler 312.5k->250k: -5.2 dB at 120 kHz -> signal killed

    Fix: bypass resampler (target_rate = channel_rate), tested here.

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
#include "cf32_resampler.h"
#include "rtl_433.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*===========================================================================
 * Test Configuration - matches real HydraSDR wideband scenarios
 *===========================================================================*/

#define PIPELINE_NUM_CHANNELS  8
#define PIPELINE_SAMPLE_RATE   WIDEBAND_RATE_2M5  /* 2500000 Hz */
#define PIPELINE_CENTER_FREQ   433.9e6f           /* Real ISM band center */
#define PIPELINE_BANDWIDTH     2.0e6f
#define PIPELINE_INPUT_SAMPLES 16384
#define PIPELINE_CHANNEL_SPACING (PIPELINE_SAMPLE_RATE / PIPELINE_NUM_CHANNELS) /* 312500 Hz */
#define PIPELINE_CHANNEL_RATE  (PIPELINE_SAMPLE_RATE / (PIPELINE_NUM_CHANNELS / 2)) /* 625000 Hz (2x oversampled) */

/* Amplitude threshold: signal must be above this to be "detected" */
#define PIPELINE_DETECT_THRESHOLD_DB  -6.0f   /* -6 dB from reference */
#define PIPELINE_QUIET_THRESHOLD_DB  -30.0f   /* Below this = no signal */

/*===========================================================================
 * Test Utilities
 *===========================================================================*/

static int test_count = 0;
static int test_passed = 0;

#define TEST_ASSERT(cond, msg) do { \
	test_count++; \
	if (!(cond)) { \
		printf("  FAIL: %s\n", msg); \
	} else { \
		test_passed++; \
		printf("  PASS: %s\n", msg); \
	} \
} while (0)

static void generate_tone(float *buf, int n_samples, float freq_offset,
                           float sample_rate, float amplitude)
{
	for (int i = 0; i < n_samples; i++) {
		float phase = 2.0f * (float)M_PI * freq_offset * (float)i / sample_rate;
		buf[i * 2 + 0] = amplitude * cosf(phase);
		buf[i * 2 + 1] = amplitude * sinf(phase);
	}
}

static float compute_power_db(const float *buf, int n_samples)
{
	float power = 0.0f;
	for (int i = 0; i < n_samples; i++) {
		float I = buf[i * 2 + 0];
		float Q = buf[i * 2 + 1];
		power += I * I + Q * Q;
	}
	power /= (float)n_samples;
	if (power < 1e-20f)
		return -200.0f;
	return 10.0f * log10f(power);
}

/*===========================================================================
 * Test 1: Pipeline with passthrough (the fix: target_rate == channel_rate)
 *
 * Verifies that signals at various offsets from channel center survive
 * the full pipeline when resampler is in passthrough mode.
 *===========================================================================*/

static int test_pipeline_passthrough(void)
{
	channelizer_t ch;
	cf32_resampler_t res;
	float *input;
	float *channel_out[CHANNELIZER_MAX_CHANNELS];
	int out_samples;
	int ret;
	int failures = 0;

	printf("\n=== Pipeline Test 1: Channelizer -> Passthrough ===\n");
	printf("  Config: 8-ch @ 2.5 MSps, center=433.9 MHz (2x oversampled)\n");
	printf("  Channel spacing: %.1f kHz, passband: +/-%.1f kHz\n",
	       (float)PIPELINE_CHANNEL_SPACING / 1000.0f,
	       (float)PIPELINE_CHANNEL_SPACING * CHANNELIZER_CUTOFF_RATIO / 2000.0f);
	printf("  Resampler: passthrough (%d -> %d Hz)\n\n",
	       PIPELINE_CHANNEL_RATE, PIPELINE_CHANNEL_RATE);

	/* Init resampler in passthrough mode (same rate in and out) */
	ret = cf32_resampler_init(&res, PIPELINE_CHANNEL_RATE, PIPELINE_CHANNEL_RATE, PIPELINE_INPUT_SAMPLES);
	TEST_ASSERT(ret == 0, "Passthrough resampler init");
	TEST_ASSERT(res.initialized == 0, "Passthrough mode confirmed (no filtering)");

	input = (float *)calloc(PIPELINE_INPUT_SAMPLES * 2, sizeof(float));
	if (!input) {
		printf("  FAIL: allocation\n");
		cf32_resampler_free(&res);
		return 1;
	}

	/*
	 * Test signals at offsets matching real-world scenarios.
	 * All offsets are from channel 0 center (433.9 MHz).
	 */
	struct {
		float offset_khz;    /* kHz from channel center */
		const char *desc;
	} test_signals[] = {
		{  0.0f,   "DC (channel center)"                        },
		{ 20.0f,   "+20 kHz (433.92 MHz telecommand)"           },
		{ 50.0f,   "+50 kHz (typical OOK signal)"               },
		{100.0f,   "+100 kHz (near passband edge)"              },
		{120.0f,   "+120 kHz (original failing frequency)"      },
		{130.0f,   "+130 kHz (93% of passband edge)"            },
		{-20.0f,   "-20 kHz (negative offset)"                  },
		{-120.0f,  "-120 kHz (negative edge)"                   },
	};
	int num_signals = sizeof(test_signals) / sizeof(test_signals[0]);

	float half_usable = (float)PIPELINE_CHANNEL_SPACING * CHANNELIZER_CUTOFF_RATIO / 2.0f;

	/* Get reference power at DC */
	ret = channelizer_init(&ch, PIPELINE_NUM_CHANNELS, PIPELINE_CENTER_FREQ,
	                       PIPELINE_BANDWIDTH, PIPELINE_SAMPLE_RATE, PIPELINE_INPUT_SAMPLES);
	if (ret != 0) {
		printf("  FAIL: channelizer init\n");
		free(input);
		cf32_resampler_free(&res);
		return 1;
	}

	memset(input, 0, PIPELINE_INPUT_SAMPLES * 2 * sizeof(float));
	generate_tone(input, PIPELINE_INPUT_SAMPLES, 0.0f, (float)PIPELINE_SAMPLE_RATE, 1.0f);
	channelizer_process(&ch, input, PIPELINE_INPUT_SAMPLES, channel_out, &out_samples);
	float ref_db = compute_power_db(channel_out[0], out_samples);
	channelizer_free(&ch);

	printf("  Reference power (DC): %.1f dB\n", ref_db);
	printf("  Usable passband: +/-%.1f kHz\n\n", half_usable / 1000.0f);

	printf("  %-45s | Power (dB) | vs Ref  | Status\n", "Signal");
	printf("  ---------------------------------------------+------------+---------+--------\n");

	for (int i = 0; i < num_signals; i++) {
		float offset_hz = test_signals[i].offset_khz * 1000.0f;

		/* Fresh channelizer for each test */
		ret = channelizer_init(&ch, PIPELINE_NUM_CHANNELS, PIPELINE_CENTER_FREQ,
		                       PIPELINE_BANDWIDTH, PIPELINE_SAMPLE_RATE, PIPELINE_INPUT_SAMPLES);
		if (ret != 0) {
			printf("  FAIL: channelizer init for offset %.0f kHz\n",
			       test_signals[i].offset_khz);
			failures++;
			continue;
		}

		/* Generate signal at offset from center (which maps to Ch0) */
		memset(input, 0, PIPELINE_INPUT_SAMPLES * 2 * sizeof(float));
		generate_tone(input, PIPELINE_INPUT_SAMPLES, offset_hz,
		              (float)PIPELINE_SAMPLE_RATE, 1.0f);

		/* Stage 1: Channelizer */
		channelizer_process(&ch, input, PIPELINE_INPUT_SAMPLES,
		                    channel_out, &out_samples);

		/* Stage 2: Resampler (passthrough - just returns same data) */
		float *resampled_out;
		int resampled_count;

		if (res.initialized) {
			resampled_count = cf32_resampler_process(&res, channel_out[0],
			                                         out_samples,
			                                         &resampled_out,
			                                         PIPELINE_INPUT_SAMPLES);
		} else {
			/* Passthrough mode: no resampling */
			resampled_out = channel_out[0];
			resampled_count = out_samples;
		}

		float power_db = compute_power_db(resampled_out, resampled_count);
		float delta = power_db - ref_db;

		/* Determine expected result */
		int within_passband = (fabsf(offset_hz) <= half_usable);
		const char *status;
		char msg[128];

		if (within_passband) {
			/* Signal within usable BW must survive with < 6 dB loss */
			if (delta > PIPELINE_DETECT_THRESHOLD_DB) {
				status = "OK";
			} else {
				status = "KILLED!";
				failures++;
			}
			snprintf(msg, sizeof(msg), "Signal at %+.0f kHz survives pipeline (%.1f dB)",
			         test_signals[i].offset_khz, delta);
			test_count++;
			if (delta > PIPELINE_DETECT_THRESHOLD_DB)
				test_passed++;
			else
				printf("  FAIL: %s\n", msg);
		} else {
			/* Signal outside passband - expected to be attenuated */
			status = (delta < -3.0f) ? "OK (filtered)" : "LEAK";
		}

		printf("  %-45s | %7.1f dB | %+5.1f dB | %s\n",
		       test_signals[i].desc, power_db, delta, status);

		channelizer_free(&ch);
	}

	free(input);
	cf32_resampler_free(&res);

	return failures;
}

/*===========================================================================
 * Test 2: Pipeline with resampler (old broken path, documents the issue)
 *
 * Shows that the resampler 312.5k->250k attenuates signals near the
 * channel edge. This test DOCUMENTS the known attenuation, not a failure.
 *===========================================================================*/

static int test_pipeline_with_resampler(void)
{
	channelizer_t ch;
	cf32_resampler_t res;
	float *input;
	float *channel_out[CHANNELIZER_MAX_CHANNELS];
	int out_samples;
	int ret;
	int failures = 0;

	printf("\n\n=== Pipeline Test 2: Channelizer -> Resampler 625k->250k ===\n");
	printf("  Documents the known issue: resampler attenuates near-edge signals\n");
	printf("  This is why the fix bypasses the resampler (passthrough mode)\n\n");

	ret = cf32_resampler_init(&res, PIPELINE_CHANNEL_RATE, DEFAULT_SAMPLE_RATE,
	                          PIPELINE_INPUT_SAMPLES);
	if (ret != 0) {
		printf("  FAIL: resampler init 312500->250000\n");
		return 1;
	}

	input = (float *)calloc(PIPELINE_INPUT_SAMPLES * 2, sizeof(float));
	if (!input) {
		cf32_resampler_free(&res);
		return 1;
	}

	printf("  Resampler: %d -> 250000 Hz (L=%d, M=%d)\n", PIPELINE_CHANNEL_RATE,
	       res.up_factor, res.down_factor);
	printf("  Output Nyquist: %.0f Hz\n\n", (float)DEFAULT_SAMPLE_RATE / 2.0f);

	struct {
		float offset_khz;
		float max_attenuation_db;  /* Max acceptable attenuation */
		const char *desc;
	} test_signals[] = {
		{  0.0f,   1.0f,  "DC (channel center)"                  },
		{ 20.0f,   1.0f,  "+20 kHz (433.92 MHz)"                 },
		{ 50.0f,   2.0f,  "+50 kHz (typical OOK)"                },
		{100.0f,   6.0f,  "+100 kHz (approaching Nyquist)"       },
		{120.0f,  99.0f,  "+120 kHz (original failing freq)"     },
	};
	int num_signals = sizeof(test_signals) / sizeof(test_signals[0]);

	/* Get reference: DC through channelizer only (no resampler) */
	ret = channelizer_init(&ch, PIPELINE_NUM_CHANNELS, PIPELINE_CENTER_FREQ,
	                       PIPELINE_BANDWIDTH, PIPELINE_SAMPLE_RATE, PIPELINE_INPUT_SAMPLES);
	if (ret != 0) {
		free(input);
		cf32_resampler_free(&res);
		return 1;
	}
	memset(input, 0, PIPELINE_INPUT_SAMPLES * 2 * sizeof(float));
	generate_tone(input, PIPELINE_INPUT_SAMPLES, 0.0f, (float)PIPELINE_SAMPLE_RATE, 1.0f);
	channelizer_process(&ch, input, PIPELINE_INPUT_SAMPLES, channel_out, &out_samples);
	float ref_db = compute_power_db(channel_out[0], out_samples);
	channelizer_free(&ch);

	printf("  %-40s | Chan dB | Resamp dB | Atten  | Note\n", "Signal");
	printf("  ----------------------------------------+--------+-----------+--------+---------\n");

	for (int i = 0; i < num_signals; i++) {
		float offset_hz = test_signals[i].offset_khz * 1000.0f;

		/* Fresh channelizer and resampler state */
		ret = channelizer_init(&ch, PIPELINE_NUM_CHANNELS, PIPELINE_CENTER_FREQ,
		                       PIPELINE_BANDWIDTH, PIPELINE_SAMPLE_RATE, PIPELINE_INPUT_SAMPLES);
		if (ret != 0) {
			failures++;
			continue;
		}
		cf32_resampler_free(&res);
		cf32_resampler_init(&res, PIPELINE_CHANNEL_RATE, DEFAULT_SAMPLE_RATE,
		                    PIPELINE_INPUT_SAMPLES);

		memset(input, 0, PIPELINE_INPUT_SAMPLES * 2 * sizeof(float));
		generate_tone(input, PIPELINE_INPUT_SAMPLES, offset_hz,
		              (float)PIPELINE_SAMPLE_RATE, 1.0f);

		/* Stage 1: Channelizer */
		channelizer_process(&ch, input, PIPELINE_INPUT_SAMPLES,
		                    channel_out, &out_samples);
		float chan_db = compute_power_db(channel_out[0], out_samples);

		/* Stage 2: Resampler */
		float *resampled_out;
		int resampled_count;
		resampled_count = cf32_resampler_process(&res, channel_out[0],
		                                         out_samples,
		                                         &resampled_out,
		                                         PIPELINE_INPUT_SAMPLES);

		float resamp_db = compute_power_db(resampled_out, resampled_count);
		float atten = chan_db - resamp_db;

		const char *note = "";
		if (atten > 3.0f)
			note = "<-- LOSSY";

		printf("  %-40s | %5.1f   | %6.1f    | %5.1f  | %s\n",
		       test_signals[i].desc, chan_db, resamp_db, atten, note);

		channelizer_free(&ch);
	}

	/* Verify the specific failure case: +120 kHz is attenuated by resampler */
	cf32_resampler_free(&res);
	cf32_resampler_init(&res, PIPELINE_CHANNEL_RATE, DEFAULT_SAMPLE_RATE,
	                    PIPELINE_INPUT_SAMPLES);
	ret = channelizer_init(&ch, PIPELINE_NUM_CHANNELS, PIPELINE_CENTER_FREQ,
	                       PIPELINE_BANDWIDTH, PIPELINE_SAMPLE_RATE, PIPELINE_INPUT_SAMPLES);
	if (ret == 0) {
		memset(input, 0, PIPELINE_INPUT_SAMPLES * 2 * sizeof(float));
		generate_tone(input, PIPELINE_INPUT_SAMPLES, 120000.0f,
		              (float)PIPELINE_SAMPLE_RATE, 1.0f);
		channelizer_process(&ch, input, PIPELINE_INPUT_SAMPLES,
		                    channel_out, &out_samples);
		float chan_db_120 = compute_power_db(channel_out[0], out_samples);

		float *resampled_out;
		int resampled_count = cf32_resampler_process(&res, channel_out[0],
		                                              out_samples,
		                                              &resampled_out,
		                                              PIPELINE_INPUT_SAMPLES);
		float resamp_db_120 = compute_power_db(resampled_out, resampled_count);
		float atten_120 = chan_db_120 - resamp_db_120;

		printf("\n  Verification: +120 kHz resampler attenuation = %.1f dB\n", atten_120);
		TEST_ASSERT(atten_120 > 2.0f,
			    "Resampler attenuates +120 kHz signal (documents known issue)");

		channelizer_free(&ch);
	}

	free(input);
	cf32_resampler_free(&res);

	return failures;
}

/*===========================================================================
 * Test 3: Real-world frequency scenarios
 *
 * Test with the exact frequencies from real HydraSDR usage.
 *===========================================================================*/

static int test_realworld_frequencies(void)
{
	channelizer_t ch;
	float *input;
	float *channel_out[CHANNELIZER_MAX_CHANNELS];
	int out_samples;
	int ret;
	int failures = 0;

	printf("\n\n=== Pipeline Test 3: Real-World Frequency Scenarios ===\n\n");

	input = (float *)calloc(PIPELINE_INPUT_SAMPLES * 2, sizeof(float));
	if (!input)
		return 1;

	/*
	 * Real ISM band scenarios with -B center:2M:8
	 */
	struct {
		float center_mhz;      /* -B center frequency */
		float signal_mhz;      /* Actual signal frequency */
		int expected_channel;   /* Which channel should capture it */
		const char *desc;
	} scenarios[] = {
		/* -B 433.9M:2M:8 -> channels spaced 312.5 kHz
		 * Ch0: 433.900 MHz (DC), Ch1: 434.2125, Ch7: 433.5875 (-312.5k)
		 * Ch5: 432.9625 (-937.5k), Ch6: 433.275 (-625k) */
		{433.900f, 433.920f, 0, "Telecommand at 433.92 MHz (+20 kHz from Ch0)"},
		{433.900f, 433.800f, 0, "Signal at 433.80 MHz (Ch0, -100 kHz)"},
		{433.900f, 434.200f, 1, "Signal at 434.20 MHz (Ch1, -12.5 kHz)"},
		{433.900f, 433.100f, 5, "Signal at 433.10 MHz (Ch5, +137.5 kHz)"},

		/* -B 868.5M:2M:8 -> 868 MHz ISM band */
		{868.500f, 868.500f, 0, "Signal at 868.50 MHz (Ch0 center)"},
		{868.500f, 868.300f, 7, "Signal at 868.30 MHz (Ch7, -200 kHz)"},
	};
	int num_scenarios = sizeof(scenarios) / sizeof(scenarios[0]);

	float half_usable = (float)PIPELINE_CHANNEL_SPACING * CHANNELIZER_CUTOFF_RATIO / 2.0f;

	printf("  Config: 8 channels @ 2.5 MSps, spacing=312.5 kHz (2x oversampled)\n");
	printf("  Usable passband per channel: +/-%.1f kHz\n\n", half_usable / 1000.0f);

	printf("  %-55s | Chan | Power dB | Detect?\n", "Scenario");
	printf("  -------------------------------------------------------+------+----------+--------\n");

	for (int s = 0; s < num_scenarios; s++) {
		float center_hz = scenarios[s].center_mhz * 1e6f;
		float signal_hz = scenarios[s].signal_mhz * 1e6f;
		float offset_hz = signal_hz - center_hz;

		ret = channelizer_init(&ch, PIPELINE_NUM_CHANNELS, center_hz,
		                       PIPELINE_BANDWIDTH, PIPELINE_SAMPLE_RATE,
		                       PIPELINE_INPUT_SAMPLES);
		if (ret != 0) {
			printf("  FAIL: channelizer init for scenario %d\n", s);
			failures++;
			continue;
		}

		memset(input, 0, PIPELINE_INPUT_SAMPLES * 2 * sizeof(float));
		generate_tone(input, PIPELINE_INPUT_SAMPLES, offset_hz,
		              (float)PIPELINE_SAMPLE_RATE, 1.0f);

		channelizer_process(&ch, input, PIPELINE_INPUT_SAMPLES,
		                    channel_out, &out_samples);

		/* Find which channel has the most power (passthrough mode) */
		int best_chan = -1;
		float best_db = -200.0f;
		for (int c = 0; c < PIPELINE_NUM_CHANNELS; c++) {
			float db = compute_power_db(channel_out[c], out_samples);
			if (db > best_db) {
				best_db = db;
				best_chan = c;
			}
		}

		/* Signal at expected channel */
		float expected_db = compute_power_db(
			channel_out[scenarios[s].expected_channel], out_samples);

		int detected = (expected_db > -10.0f);
		int correct_chan = (best_chan == scenarios[s].expected_channel);
		const char *detect_str;
		char msg[128];

		if (detected && correct_chan) {
			detect_str = "YES";
		} else if (detected) {
			detect_str = "WRONG CH";
			failures++;
		} else {
			detect_str = "NO!";
			failures++;
		}

		printf("  %-55s | Ch%-2d | %6.1f   | %s\n",
		       scenarios[s].desc, best_chan, expected_db, detect_str);

		snprintf(msg, sizeof(msg), "%.3f MHz signal detected in Ch%d",
		         scenarios[s].signal_mhz, scenarios[s].expected_channel);
		TEST_ASSERT(detected && correct_chan, msg);

		channelizer_free(&ch);
	}

	free(input);
	return failures;
}

/*===========================================================================
 * Test 4: Passband edge signal preservation (the critical test)
 *
 * Sweep from 0 to 150 kHz in 10 kHz steps and verify that signals
 * within the usable bandwidth survive the full pipeline with
 * passthrough resampler.
 *===========================================================================*/

static int test_passband_edge_sweep(void)
{
	channelizer_t ch;
	float *input;
	float *channel_out[CHANNELIZER_MAX_CHANNELS];
	int out_samples;
	int ret;
	int failures = 0;

	printf("\n\n=== Pipeline Test 4: Passband Edge Sweep (full pipeline) ===\n\n");

	input = (float *)calloc(PIPELINE_INPUT_SAMPLES * 2, sizeof(float));
	if (!input)
		return 1;

	float channel_spacing = (float)PIPELINE_CHANNEL_SPACING;
	float half_usable = channel_spacing * CHANNELIZER_CUTOFF_RATIO / 2.0f;

	printf("  Channel spacing: %.1f kHz\n", channel_spacing / 1000.0f);
	printf("  Usable passband: +/-%.1f kHz (%.0f%% cutoff)\n",
	       half_usable / 1000.0f, CHANNELIZER_CUTOFF_RATIO * 100.0f);
	printf("  Passthrough resampler (no sample rate conversion)\n\n");

	printf("  Offset (kHz) | PFB Out (dB) | Status\n");
	printf("  -------------+--------------+---------------------------\n");

	/* Get reference at DC */
	ret = channelizer_init(&ch, PIPELINE_NUM_CHANNELS, PIPELINE_CENTER_FREQ,
	                       PIPELINE_BANDWIDTH, PIPELINE_SAMPLE_RATE, PIPELINE_INPUT_SAMPLES);
	if (ret != 0) {
		free(input);
		return 1;
	}
	memset(input, 0, PIPELINE_INPUT_SAMPLES * 2 * sizeof(float));
	generate_tone(input, PIPELINE_INPUT_SAMPLES, 0.0f, (float)PIPELINE_SAMPLE_RATE, 1.0f);
	channelizer_process(&ch, input, PIPELINE_INPUT_SAMPLES, channel_out, &out_samples);
	float ref_db = compute_power_db(channel_out[0], out_samples);
	channelizer_free(&ch);

	/* Sweep from 0 to 155 kHz in 10 kHz steps */
	for (float offset_khz = 0.0f; offset_khz <= 155.0f; offset_khz += 10.0f) {
		float offset_hz = offset_khz * 1000.0f;

		ret = channelizer_init(&ch, PIPELINE_NUM_CHANNELS, PIPELINE_CENTER_FREQ,
		                       PIPELINE_BANDWIDTH, PIPELINE_SAMPLE_RATE,
		                       PIPELINE_INPUT_SAMPLES);
		if (ret != 0) {
			failures++;
			continue;
		}

		memset(input, 0, PIPELINE_INPUT_SAMPLES * 2 * sizeof(float));
		generate_tone(input, PIPELINE_INPUT_SAMPLES, offset_hz,
		              (float)PIPELINE_SAMPLE_RATE, 1.0f);
		channelizer_process(&ch, input, PIPELINE_INPUT_SAMPLES,
		                    channel_out, &out_samples);

		float power_db = compute_power_db(channel_out[0], out_samples);
		float delta = power_db - ref_db;

		const char *status;
		int within_passband = (offset_hz <= half_usable);
		char msg[128];

		if (within_passband) {
			if (delta > -3.0f) {
				status = "passband OK";
			} else {
				status = "PASSBAND ATTENUATION!";
				failures++;
				snprintf(msg, sizeof(msg),
				         "+%.0f kHz within passband but attenuated %.1f dB",
				         offset_khz, -delta);
				printf("  FAIL: %s\n", msg);
			}
		} else if (offset_hz <= channel_spacing / 2.0f) {
			status = "transition band";
		} else {
			status = "stopband (expected)";
		}

		/* Mark the passband edge */
		const char *marker = "";
		if (fabsf(offset_hz - half_usable) < 5000.0f)
			marker = " <-- passband edge";

		printf("  %+7.0f kHz   | %+6.1f dB    | %s%s\n",
		       offset_khz, delta, status, marker);

		channelizer_free(&ch);
	}

	/* Explicit assertion for the critical frequencies */
	struct {
		float offset_khz;
		const char *desc;
	} critical[] = {
		{ 20.0f, "+20 kHz (telecommand)" },
		{100.0f, "+100 kHz (typical)"    },
		{120.0f, "+120 kHz (original bug)"},
	};

	printf("\n  Critical frequency assertions:\n");
	for (int i = 0; i < 3; i++) {
		float offset_hz = critical[i].offset_khz * 1000.0f;

		ret = channelizer_init(&ch, PIPELINE_NUM_CHANNELS, PIPELINE_CENTER_FREQ,
		                       PIPELINE_BANDWIDTH, PIPELINE_SAMPLE_RATE,
		                       PIPELINE_INPUT_SAMPLES);
		if (ret != 0) {
			failures++;
			continue;
		}

		memset(input, 0, PIPELINE_INPUT_SAMPLES * 2 * sizeof(float));
		generate_tone(input, PIPELINE_INPUT_SAMPLES, offset_hz,
		              (float)PIPELINE_SAMPLE_RATE, 1.0f);
		channelizer_process(&ch, input, PIPELINE_INPUT_SAMPLES,
		                    channel_out, &out_samples);

		float power_db = compute_power_db(channel_out[0], out_samples);
		float delta = power_db - ref_db;

		char msg[128];
		snprintf(msg, sizeof(msg), "%s survives pipeline (%.1f dB loss)",
		         critical[i].desc, -delta);
		TEST_ASSERT(delta > PIPELINE_DETECT_THRESHOLD_DB, msg);

		channelizer_free(&ch);
	}

	free(input);
	return failures;
}

/*===========================================================================
 * Main
 *===========================================================================*/

int main(void)
{
	int total_failures = 0;

	printf("============================================================\n");
	printf("    End-to-End Pipeline Test: Channelizer + Resampler\n");
	printf("============================================================\n");
	printf("  Validates signals survive the full wideband processing chain.\n");
	printf("  Tests the fix: bypass resampler (passthrough mode) so that\n");
	printf("  near-edge signals are not attenuated.\n");

	total_failures += test_pipeline_passthrough();
	total_failures += test_pipeline_with_resampler();
	total_failures += test_realworld_frequencies();
	total_failures += test_passband_edge_sweep();

	printf("\n============================================================\n");
	printf("  Test assertions: %d/%d passed\n", test_passed, test_count);
	if (total_failures == 0 && test_passed == test_count) {
		printf("  ALL PIPELINE TESTS PASSED\n");
	} else {
		printf("  PIPELINE TESTS FAILED (%d failures)\n", total_failures);
	}
	printf("============================================================\n");

	return (test_passed == test_count && total_failures == 0) ? 0 : 1;
}
