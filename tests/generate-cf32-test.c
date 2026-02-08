/*
 * Generate CF32 test signal for hydrasdr_433 testing
 * Creates a file with OOK-modulated signal that should be detected
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int main(int argc, char *argv[])
{
	const char *filename = "test_signal_250k.cf32";
	uint32_t sample_rate = 250000;
	float carrier_freq = 10000.0f;  /* 10 kHz offset from center */
	float duration = 2.0f;  /* 2 seconds */

	if (argc > 1) {
		filename = argv[1];
	}

	size_t num_samples = (size_t)(sample_rate * duration);
	float *buf = (float *)malloc(num_samples * 2 * sizeof(float));
	if (!buf) {
		fprintf(stderr, "Failed to allocate buffer\n");
		return 1;
	}

	printf("Generating CF32 test signal:\n");
	printf("  File: %s\n", filename);
	printf("  Sample rate: %u Hz\n", sample_rate);
	printf("  Duration: %.1f seconds\n", duration);
	printf("  Samples: %zu\n", num_samples);

	/* Generate OOK-like signal pattern */
	/* Pattern: carrier on/off with varying pulse widths */
	float t = 0.0f;
	float dt = 1.0f / sample_rate;

	/* Simple OOK pattern: 500us pulses with 500us gaps, repeated */
	float pulse_width = 0.0005f;  /* 500 microseconds */
	float gap_width = 0.0005f;
	float pattern_period = pulse_width + gap_width;

	/* Add some noise floor */
	float noise_level = 0.02f;
	float signal_level = 0.7f;

	for (size_t i = 0; i < num_samples; i++) {
		float phase = 2.0f * (float)M_PI * carrier_freq * t;

		/* Determine if we're in a pulse or gap */
		float pattern_pos = fmodf(t, pattern_period);
		int in_pulse = (pattern_pos < pulse_width);

		/* Add some packet structure: bursts of pulses with longer gaps */
		float burst_period = 0.1f;  /* 100ms between packet bursts */
		float burst_duration = 0.02f;  /* 20ms of pulses per burst */
		float burst_pos = fmodf(t, burst_period);
		int in_burst = (burst_pos < burst_duration);

		float amplitude = noise_level;
		if (in_burst && in_pulse) {
			amplitude = signal_level;
		}

		/* Generate I/Q with some phase noise */
		float noise_i = ((float)rand() / RAND_MAX - 0.5f) * noise_level;
		float noise_q = ((float)rand() / RAND_MAX - 0.5f) * noise_level;

		buf[i * 2 + 0] = amplitude * cosf(phase) + noise_i;  /* I */
		buf[i * 2 + 1] = amplitude * sinf(phase) + noise_q;  /* Q */

		t += dt;
	}

	/* Write to file */
	FILE *fp = fopen(filename, "wb");
	if (!fp) {
		fprintf(stderr, "Failed to open %s for writing\n", filename);
		free(buf);
		return 1;
	}

	size_t written = fwrite(buf, sizeof(float) * 2, num_samples, fp);
	fclose(fp);
	free(buf);

	if (written != num_samples) {
		fprintf(stderr, "Failed to write all samples\n");
		return 1;
	}

	printf("  Written: %zu samples (%.2f MB)\n", written,
	       (float)(written * 2 * sizeof(float)) / (1024 * 1024));
	printf("Done!\n");

	return 0;
}
