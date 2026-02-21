/** @file
    Channelizer hot-path profiler with correctness validation.

    1. Correctness tests (DC routing, isolation, output count, tone routing)
    2. Hot-path component timing breakdown:
       - Full channelizer_process throughput
       - Isolated polyphase dot products (AoS vs SoA)
       - Isolated FFT (M-point complex)
       - Overhead estimate (commutator + phase correction + store)

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
#include "hydrasdr_lfft.h"
#include "fft_kernels.h"
#include "rtl_433.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Volatile sink to prevent dead-code elimination of benchmark results */
static volatile float g_sink;

static int g_pass;
static int g_fail;

/*===========================================================================
 * Timer - use QPC on Windows for sub-us resolution
 *===========================================================================*/

#ifdef _WIN32
#include <windows.h>
static double get_time_sec(void)
{
	LARGE_INTEGER freq, cnt;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&cnt);
	return (double)cnt.QuadPart / (double)freq.QuadPart;
}
#else
static double get_time_sec(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}
#endif

/*===========================================================================
 * Helpers
 *===========================================================================*/

static float compute_power(const float *buf, int n)
{
	if (n <= 0)
		return 0.0f;
	float pwr = 0.0f;
	for (int i = 0; i < n; i++) {
		float re = buf[i * 2 + 0];
		float im = buf[i * 2 + 1];
		pwr += re * re + im * im;
	}
	return pwr / (float)n;
}

static float power_to_db(float p)
{
	return (p > 1e-20f) ? 10.0f * log10f(p) : -200.0f;
}

static void check(int cond, const char *msg)
{
	if (cond) {
		g_pass++;
	} else {
		printf("    FAIL: %s\n", msg);
		g_fail++;
	}
}

/*===========================================================================
 * Dot product benchmarks (noinline to prevent DCE)
 *===========================================================================*/

#if defined(__GNUC__) || defined(__clang__)
#define BENCH_NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#define BENCH_NOINLINE __declspec(noinline)
#else
#define BENCH_NOINLINE
#endif

static BENCH_NOINLINE void dotprod_interleaved_bench(const float *win_iq,
		int start, int mask, const float *coeff, int len,
		float *out_i, float *out_q)
{
	float sum_i = 0.0f, sum_q = 0.0f;
	int alloc = mask + 1;
	int seg1 = alloc - start;

	if (seg1 >= len) {
		const float *w = win_iq + start * 2;
		for (int i = 0; i < len; i++) {
			float c = coeff[i];
			sum_i += w[i * 2 + 0] * c;
			sum_q += w[i * 2 + 1] * c;
		}
	} else {
		const float *w1 = win_iq + start * 2;
		for (int i = 0; i < seg1; i++) {
			float c = coeff[i];
			sum_i += w1[i * 2 + 0] * c;
			sum_q += w1[i * 2 + 1] * c;
		}
		const float *w2 = win_iq;
		for (int i = 0; i < len - seg1; i++) {
			float c = coeff[seg1 + i];
			sum_i += w2[i * 2 + 0] * c;
			sum_q += w2[i * 2 + 1] * c;
		}
	}
	*out_i = sum_i;
	*out_q = sum_q;
}

static BENCH_NOINLINE void dotprod_soa_bench(const float *win_i,
		const float *win_q, const float *coeff, int len,
		float *out_i, float *out_q)
{
	float sum_i = 0.0f, sum_q = 0.0f;

	for (int i = 0; i < len; i++) {
		float c = coeff[i];
		sum_i += win_i[i] * c;
		sum_q += win_q[i] * c;
	}
	*out_i = sum_i;
	*out_q = sum_q;
}

/*===========================================================================
 * Correctness Tests
 *===========================================================================*/

static void run_correctness(int num_channels, uint32_t sample_rate, int n_input)
{
	channelizer_t ch;
	int M = num_channels;
	int ret;

	printf("\n  Correctness (%d-ch @ %.1f MSps, %d samples):\n",
	       M, (float)sample_rate / 1e6f, n_input);

	/* Test 1: Initialization */
	ret = channelizer_init(&ch, M, 868.5e6f, 2.0e6f, sample_rate, n_input);
	check(ret == 0, "init should succeed");
	if (ret != 0)
		return;
	check(ch.num_channels == M, "num_channels matches");
	check(ch.decimation_factor == M / 2, "decimation_factor = M/2");
	check(ch.channel_rate == sample_rate / (uint32_t)ch.decimation_factor,
	      "channel_rate = fs/D");
	printf("    Init: M=%d D=%d rate=%u taps=%d  OK\n",
	       ch.num_channels, ch.decimation_factor,
	       ch.channel_rate, ch.window_len);

	/* Test 2: Output count */
	float *input = (float *)calloc((size_t)n_input * 2, sizeof(float));
	if (!input) {
		printf("    FAIL: alloc\n");
		g_fail++;
		channelizer_free(&ch);
		return;
	}

	/* DC tone */
	for (int i = 0; i < n_input; i++) {
		input[i * 2 + 0] = 1.0f;
		input[i * 2 + 1] = 0.0f;
	}

	float *ch_out[CHANNELIZER_MAX_CHANNELS];
	int out_samples;
	ret = channelizer_process(&ch, input, n_input, ch_out, &out_samples);
	check(ret == 0, "process returns 0");

	int expected_out = n_input / ch.decimation_factor;
	check(out_samples == expected_out, "output count = n/D");
	printf("    Output: %d in -> %d out (expected %d)  %s\n",
	       n_input, out_samples, expected_out,
	       out_samples == expected_out ? "OK" : "FAIL");

	/* Test 3: DC tone routing - Ch0 should have max power */
	float powers[CHANNELIZER_MAX_CHANNELS];
	float total_pwr = 0.0f;
	int max_ch = 0;
	for (int i = 0; i < M; i++) {
		powers[i] = compute_power(ch_out[i], out_samples);
		total_pwr += powers[i];
		if (powers[i] > powers[max_ch])
			max_ch = i;
	}
	check(max_ch == 0, "DC tone in channel 0");
	float dc_ratio = (total_pwr > 0) ? powers[0] / total_pwr : 0.0f;
	check(dc_ratio > 0.90f, "DC power >90% in Ch0");
	printf("    DC routing: Ch%d has max, ratio=%.1f%%  %s\n",
	       max_ch, dc_ratio * 100.0f,
	       (max_ch == 0 && dc_ratio > 0.90f) ? "OK" : "FAIL");

	/* Test 4: Channel 1 tone routing */
	channelizer_free(&ch);
	ret = channelizer_init(&ch, M, 868.5e6f, 2.0e6f, sample_rate, n_input);
	if (ret != 0) {
		g_fail++;
		free(input);
		return;
	}

	float spacing = ch.channel_spacing;
	for (int i = 0; i < n_input; i++) {
		float phase = 2.0f * (float)M_PI * spacing * (float)i
		              / (float)sample_rate;
		input[i * 2 + 0] = cosf(phase);
		input[i * 2 + 1] = sinf(phase);
	}

	ret = channelizer_process(&ch, input, n_input, ch_out, &out_samples);
	check(ret == 0, "process Ch1 tone");

	max_ch = 0;
	for (int i = 0; i < M; i++) {
		powers[i] = compute_power(ch_out[i], out_samples);
		if (powers[i] > powers[max_ch])
			max_ch = i;
	}
	check(max_ch == 1, "Ch1 tone routed to Ch1");
	printf("    Ch1 routing: max in Ch%d  %s\n",
	       max_ch, max_ch == 1 ? "OK" : "FAIL");

	/* Test 5: Channel isolation (DC tone, check non-adjacent) */
	channelizer_free(&ch);
	ret = channelizer_init(&ch, M, 868.5e6f, 2.0e6f, sample_rate, n_input);
	if (ret != 0) {
		g_fail++;
		free(input);
		return;
	}

	for (int i = 0; i < n_input; i++) {
		input[i * 2 + 0] = 1.0f;
		input[i * 2 + 1] = 0.0f;
	}
	ret = channelizer_process(&ch, input, n_input, ch_out, &out_samples);
	check(ret == 0, "process isolation");

	for (int i = 0; i < M; i++)
		powers[i] = compute_power(ch_out[i], out_samples);

	float dc_db = power_to_db(powers[0]);
	int iso_ok = 1;
	for (int i = 2; i < M - 1; i++) {
		float iso = dc_db - power_to_db(powers[i]);
		if (iso < 25.0f)
			iso_ok = 0;
	}
	check(iso_ok, "non-adjacent isolation >25 dB");
	printf("    Isolation: non-adjacent >25 dB  %s\n",
	       iso_ok ? "OK" : "FAIL");

	/* Test 6: Negative frequency routing */
	channelizer_free(&ch);
	ret = channelizer_init(&ch, M, 868.5e6f, 2.0e6f, sample_rate, n_input);
	if (ret != 0) {
		g_fail++;
		free(input);
		return;
	}

	int neg_ch = M - 1;
	float neg_offset = -spacing;
	for (int i = 0; i < n_input; i++) {
		float phase = 2.0f * (float)M_PI * neg_offset * (float)i
		              / (float)sample_rate;
		input[i * 2 + 0] = cosf(phase);
		input[i * 2 + 1] = sinf(phase);
	}
	ret = channelizer_process(&ch, input, n_input, ch_out, &out_samples);
	check(ret == 0, "process neg freq");

	max_ch = 0;
	for (int i = 0; i < M; i++) {
		powers[i] = compute_power(ch_out[i], out_samples);
		if (powers[i] > powers[max_ch])
			max_ch = i;
	}
	check(max_ch == neg_ch, "negative freq in last channel");
	printf("    Neg freq: max in Ch%d (expected Ch%d)  %s\n",
	       max_ch, neg_ch, max_ch == neg_ch ? "OK" : "FAIL");

	free(input);
	channelizer_free(&ch);
}

/*===========================================================================
 * Performance Profiling
 *===========================================================================*/

struct profile_config {
	int num_channels;
	uint32_t sample_rate;
	int input_samples;
	const char *name;
};

static void run_profile(const struct profile_config *cfg)
{
	channelizer_t ch;
	int ret;

	printf("\n  %-28s (M=%d, D=%d)\n", cfg->name,
	       cfg->num_channels, cfg->num_channels / 2);
	printf("  ------------------------------------------------------------\n");

	ret = channelizer_init(&ch, cfg->num_channels, 868.5e6f, 2.0e6f,
	                       cfg->sample_rate, cfg->input_samples);
	if (ret != 0) {
		printf("  INIT FAILED\n");
		return;
	}

	int M = ch.num_channels;
	int D = ch.decimation_factor;
	int p = ch.window_len;
	int n = cfg->input_samples;
	int out_per_call = n / D;

	printf("  Filter: %d taps/branch, %d branches, %d total taps\n",
	       p, M, p * M);
	printf("  Per call: %d input -> %d output (%d FFTs)\n",
	       n, out_per_call, out_per_call);

	float *input = (float *)calloc((size_t)n * 2, sizeof(float));
	if (!input) {
		fprintf(stderr, "  ALLOC FAILED\n");
		channelizer_free(&ch);
		return;
	}

	for (int i = 0; i < n; i++) {
		float phase = 2.0f * (float)M_PI * 100000.0f * (float)i
		              / (float)cfg->sample_rate;
		input[i * 2 + 0] = cosf(phase);
		input[i * 2 + 1] = sinf(phase);
	}

	float *channel_out_ptrs[CHANNELIZER_MAX_CHANNELS];
	int out_samples;
	int iters = 2000;

	/* Warmup */
	for (int i = 0; i < 200; i++)
		channelizer_process(&ch, input, n, channel_out_ptrs, &out_samples);

	/* Benchmark 1: Full channelizer_process */
	double t0 = get_time_sec();
	for (int i = 0; i < iters; i++)
		channelizer_process(&ch, input, n, channel_out_ptrs, &out_samples);
	double t_total = get_time_sec() - t0;

	double total_samples = (double)n * iters;
	double msps = total_samples / t_total / 1e6;
	double ns_per_sample = t_total / total_samples * 1e9;
	double ns_per_fft = t_total / ((double)out_per_call * iters) * 1e9;

	printf("\n  Full channelizer_process:\n");
	printf("    Throughput:      %6.1f MSps (%.1fx RT @ %u Hz)\n",
	       msps, msps * 1e6 / (double)cfg->sample_rate, cfg->sample_rate);
	printf("    Per input sample: %5.1f ns\n", ns_per_sample);
	printf("    Per FFT cycle:    %5.0f ns (%d FFTs/call)\n",
	       ns_per_fft, out_per_call);

	/* Benchmark 2: Isolated dot products (AoS interleaved) */
	int w_alloc = 64;
	int w_mask = w_alloc - 1;
	float *win_buf = (float *)calloc((size_t)w_alloc * 2, sizeof(float));
	float *coeffs = (float *)calloc(p, sizeof(float));
	float acc_i = 0.0f, acc_q = 0.0f;
	float tmp_i, tmp_q;

	if (!win_buf || !coeffs) {
		fprintf(stderr, "  ALLOC FAILED\n");
		free(win_buf);
		free(coeffs);
		free(input);
		channelizer_free(&ch);
		return;
	}

	for (int i = 0; i < w_alloc * 2; i++)
		win_buf[i] = (float)(i % 17) * 0.01f;
	for (int i = 0; i < p; i++)
		coeffs[i] = 1.0f / (float)p;

	long dotprod_calls = (long)M * iters * out_per_call;

	t0 = get_time_sec();
	for (int i = 0; i < iters; i++) {
		for (int o = 0; o < out_per_call; o++) {
			for (int b = 0; b < M; b++) {
				dotprod_interleaved_bench(win_buf,
				                          (o * 3 + b) & w_mask,
				                          w_mask, coeffs, p,
				                          &tmp_i, &tmp_q);
				acc_i += tmp_i;
				acc_q += tmp_q;
			}
		}
	}
	double t_dotprod_aos = get_time_sec() - t0;
	g_sink = acc_i + acc_q;

	double ns_dotprod_per_fft = t_dotprod_aos / ((double)out_per_call * iters) * 1e9;
	double ns_dotprod_per_sample = t_dotprod_aos / total_samples * 1e9;

	printf("\n  Isolated dot products (AoS interleaved, current):\n");
	printf("    Total calls:     %ld (M=%d x %d FFTs x %d iters)\n",
	       dotprod_calls, M, out_per_call, iters);
	printf("    Per input sample: %5.1f ns (%.0f%% of total)\n",
	       ns_dotprod_per_sample,
	       ns_dotprod_per_sample / ns_per_sample * 100.0);
	printf("    Per FFT cycle:    %5.0f ns\n", ns_dotprod_per_fft);

	/* Benchmark 3: Isolated dot products (SoA) */
	float *win_i = (float *)calloc(w_alloc, sizeof(float));
	float *win_q = (float *)calloc(w_alloc, sizeof(float));

	if (!win_i || !win_q) {
		fprintf(stderr, "  ALLOC FAILED\n");
		free(win_i);
		free(win_q);
		free(win_buf);
		free(coeffs);
		free(input);
		channelizer_free(&ch);
		return;
	}

	for (int i = 0; i < w_alloc; i++) {
		win_i[i] = win_buf[i * 2 + 0];
		win_q[i] = win_buf[i * 2 + 1];
	}

	acc_i = 0.0f;
	acc_q = 0.0f;

	t0 = get_time_sec();
	for (int i = 0; i < iters; i++) {
		for (int o = 0; o < out_per_call; o++) {
			for (int b = 0; b < M; b++) {
				dotprod_soa_bench(win_i, win_q, coeffs, p,
				                  &tmp_i, &tmp_q);
				acc_i += tmp_i;
				acc_q += tmp_q;
			}
		}
	}
	double t_dotprod_soa = get_time_sec() - t0;
	g_sink = acc_i + acc_q;

	double ns_dotprod_soa_per_fft = t_dotprod_soa / ((double)out_per_call * iters) * 1e9;
	double ns_dotprod_soa_per_sample = t_dotprod_soa / total_samples * 1e9;
	double soa_speedup = t_dotprod_aos / t_dotprod_soa;

	printf("\n  Isolated dot products (SoA split I/Q, proposed):\n");
	printf("    Per input sample: %5.1f ns (%.0f%% of total)\n",
	       ns_dotprod_soa_per_sample,
	       ns_dotprod_soa_per_sample / ns_per_sample * 100.0);
	printf("    Per FFT cycle:    %5.0f ns\n", ns_dotprod_soa_per_fft);
	printf("    Speedup vs AoS:   %.2fx\n", soa_speedup);

	/* Benchmark 4: Isolated FFT */
	hlfft_complex_t *fft_in = (hlfft_complex_t *)hlfft_aligned_alloc(
		(size_t)M * sizeof(hlfft_complex_t));
	hlfft_complex_t *fft_out = (hlfft_complex_t *)hlfft_aligned_alloc(
		(size_t)M * sizeof(hlfft_complex_t));
	hlfft_plan_t *fft_plan = hlfft_plan_create(M, NULL);

	if (!fft_in || !fft_out || !fft_plan) {
		fprintf(stderr, "  ALLOC FAILED\n");
		hlfft_plan_destroy(fft_plan);
		hlfft_aligned_free(fft_in);
		hlfft_aligned_free(fft_out);
		free(win_i);
		free(win_q);
		free(win_buf);
		free(coeffs);
		free(input);
		channelizer_free(&ch);
		return;
	}

	for (int i = 0; i < M; i++) {
		fft_in[i].re = (float)i * 0.1f;
		fft_in[i].im = (float)i * 0.05f;
	}

	long fft_calls = (long)iters * out_per_call;

	t0 = get_time_sec();
	for (int i = 0; i < iters; i++) {
		for (int o = 0; o < out_per_call; o++)
			hlfft_forward(fft_plan, fft_in, fft_out);
	}
	double t_fft = get_time_sec() - t0;
	g_sink = fft_out[0].re + fft_out[0].im;

	double ns_fft_per_call = t_fft / (double)fft_calls * 1e9;
	double ns_fft_per_sample = t_fft / total_samples * 1e9;

	printf("\n  Isolated FFT (%d-point complex):\n", M);
	printf("    Total calls:     %ld\n", fft_calls);
	printf("    Per FFT call:     %5.0f ns\n", ns_fft_per_call);
	printf("    Per input sample: %5.1f ns (%.0f%% of total)\n",
	       ns_fft_per_sample, ns_fft_per_sample / ns_per_sample * 100.0);

	/* Benchmark 5: Overhead estimate */
	double ns_overhead = ns_per_sample - ns_dotprod_per_sample - ns_fft_per_sample;

	printf("\n  Estimated overhead (commutator + phase + store):\n");
	printf("    Per input sample: %5.1f ns (%.0f%% of total)\n",
	       ns_overhead, ns_overhead / ns_per_sample * 100.0);

	/* Projected with SoA */
	double ns_projected = ns_dotprod_soa_per_sample + ns_fft_per_sample + ns_overhead;
	double projected_msps = 1e9 / ns_projected / 1e6;

	printf("\n  Projected with SoA dot products:\n");
	printf("    Per input sample: %5.1f ns (was %.1f ns)\n",
	       ns_projected, ns_per_sample);
	printf("    Throughput:      %6.1f MSps (%.1fx RT)\n",
	       projected_msps,
	       projected_msps * 1e6 / (double)cfg->sample_rate);
	printf("    Overall speedup:  %.2fx\n", ns_per_sample / ns_projected);

	/* Cleanup */
	hlfft_plan_destroy(fft_plan);
	hlfft_aligned_free(fft_in);
	hlfft_aligned_free(fft_out);
	free(win_i);
	free(win_q);
	free(win_buf);
	free(coeffs);
	free(input);
	channelizer_free(&ch);
}

/*===========================================================================
 * FFT Kernel Verification + Benchmark
 *===========================================================================*/

/**
 * Verify specialized FFT kernel against hlfft_forward_soa reference.
 * Returns max absolute error across all bins.
 */
static float verify_fft_kernel(int N,
			       void (*kernel)(const float *, const float *,
					      float *, float *))
{
	float in_re[16], in_im[16];
	float out_re[16], out_im[16];
	float ref_re[16], ref_im[16];
	float max_err = 0.0f;

	/* Deterministic pseudo-random input */
	for (int i = 0; i < N; i++) {
		in_re[i] = sinf((float)i * 1.7f + 0.3f);
		in_im[i] = cosf((float)i * 2.3f - 0.7f);
	}

	/* Reference: hlfft_forward_soa */
	hlfft_plan_t *plan = hlfft_plan_create((size_t)N, NULL);
	if (!plan) {
		printf("    FAIL: could not create FFT plan for N=%d\n", N);
		g_fail++;
		return 1e10f;
	}
	hlfft_forward_soa(plan, in_re, in_im, ref_re, ref_im);

	/* Specialized kernel */
	kernel(in_re, in_im, out_re, out_im);

	/* Compare */
	for (int i = 0; i < N; i++) {
		float err_re = fabsf(out_re[i] - ref_re[i]);
		float err_im = fabsf(out_im[i] - ref_im[i]);
		if (err_re > max_err) max_err = err_re;
		if (err_im > max_err) max_err = err_im;
	}

	hlfft_plan_destroy(plan);
	return max_err;
}

static void run_kernel_verification(void)
{
	struct {
		int n;
		const char *name;
		void (*kernel)(const float *, const float *, float *, float *);
	} kernels[] = {
		{2,  "fft2_forward_soa",  fft2_forward_soa},
		{4,  "fft4_forward_soa",  fft4_forward_soa},
		{8,  "fft8_forward_soa",  fft8_forward_soa},
		{16, "fft16_forward_soa", fft16_forward_soa},
	};
	int n_kernels = sizeof(kernels) / sizeof(kernels[0]);

	printf("\n  FFT Kernel Correctness (vs hlfft_forward_soa reference):\n");

	for (int i = 0; i < n_kernels; i++) {
		float err = verify_fft_kernel(kernels[i].n, kernels[i].kernel);
		int ok = err < 1e-5f;
		check(ok, kernels[i].name);
		printf("    %-24s max_err=%.2e  %s\n",
		       kernels[i].name, err, ok ? "OK" : "FAIL");
	}
}

/**
 * Benchmark specialized FFT kernels vs generic hlfft_forward_soa.
 */
static void run_kernel_benchmark(void)
{
	struct {
		int n;
		const char *name;
		void (*kernel)(const float *, const float *, float *, float *);
	} kernels[] = {
		{2,  "fft2  specialized",  fft2_forward_soa},
		{4,  "fft4  specialized",  fft4_forward_soa},
		{8,  "fft8  specialized",  fft8_forward_soa},
		{16, "fft16 specialized",  fft16_forward_soa},
	};
	int n_kernels = sizeof(kernels) / sizeof(kernels[0]);

	printf("\n  FFT Kernel Benchmark (specialized vs generic library):\n");
	printf("  %-24s %8s %8s %8s\n", "Kernel", "Spec ns", "Lib ns", "Speedup");
	printf("  ------------------------------------------------------------\n");

	for (int ki = 0; ki < n_kernels; ki++) {
		int N = kernels[ki].n;
		float in_re[16], in_im[16];
		float out_re[16], out_im[16];
		double t0, t_spec, t_lib;
		int iters = 2000000;

		for (int i = 0; i < N; i++) {
			in_re[i] = sinf((float)i * 1.7f + 0.3f);
			in_im[i] = cosf((float)i * 2.3f - 0.7f);
		}

		hlfft_plan_t *plan = hlfft_plan_create((size_t)N, NULL);
		if (!plan)
			continue;

		/* Warmup */
		for (int i = 0; i < 10000; i++) {
			kernels[ki].kernel(in_re, in_im, out_re, out_im);
			hlfft_forward_soa(plan, in_re, in_im, out_re, out_im);
		}

		/* Bench specialized */
		t0 = get_time_sec();
		for (int i = 0; i < iters; i++)
			kernels[ki].kernel(in_re, in_im, out_re, out_im);
		t_spec = get_time_sec() - t0;
		g_sink = out_re[0] + out_im[0];

		/* Bench generic */
		t0 = get_time_sec();
		for (int i = 0; i < iters; i++)
			hlfft_forward_soa(plan, in_re, in_im, out_re, out_im);
		t_lib = get_time_sec() - t0;
		g_sink = out_re[0] + out_im[0];

		double ns_spec = t_spec / (double)iters * 1e9;
		double ns_lib  = t_lib  / (double)iters * 1e9;
		double speedup = t_lib / t_spec;

		printf("  %-24s %7.1f  %7.1f  %6.2fx\n",
		       kernels[ki].name, ns_spec, ns_lib, speedup);

		hlfft_plan_destroy(plan);
	}
}

/*===========================================================================
 * Main
 *===========================================================================*/

int main(void)
{
	printf("============================================================\n");
	printf("    PFB Channelizer Profiler + Correctness\n");
	printf("============================================================\n");
	printf("  Build: " BUILD_INFO_STR "\n");
	printf("  %s\n", channelizer_build_info());

	/*
	 * Part 1: Correctness tests
	 */
	printf("\n============================================================\n");
	printf("  CORRECTNESS VALIDATION\n");
	printf("============================================================\n");

	struct { int M; uint32_t fs; int n; } corr_cfgs[] = {
		{4,  2500000, 8192},
		{8,  2500000, 16384},
		{16, 5000000, 32768},
		{2,  2500000, 4096},
	};
	int n_corr = sizeof(corr_cfgs) / sizeof(corr_cfgs[0]);

	for (int i = 0; i < n_corr; i++)
		run_correctness(corr_cfgs[i].M, corr_cfgs[i].fs, corr_cfgs[i].n);

	printf("\n  Correctness: %d passed, %d failed\n", g_pass, g_fail);

	/*
	 * Part 1b: FFT kernel verification
	 */
	printf("\n============================================================\n");
	printf("  FFT KERNEL VERIFICATION\n");
	printf("============================================================\n");
	run_kernel_verification();
	printf("\n  Correctness (incl. kernels): %d passed, %d failed\n",
	       g_pass, g_fail);

	/*
	 * Part 2: Performance profiling
	 */
	printf("\n============================================================\n");
	printf("  PERFORMANCE PROFILING\n");
	printf("============================================================\n");

	struct profile_config prof_cfgs[] = {
		{8,  2500000, 32768, "8-ch @ 2.5 MSps"},
		{16, 5000000, 32768, "16-ch @ 5.0 MSps"},
		{4,  2500000, 32768, "4-ch @ 2.5 MSps"},
	};
	int n_prof = sizeof(prof_cfgs) / sizeof(prof_cfgs[0]);

	for (int i = 0; i < n_prof; i++)
		run_profile(&prof_cfgs[i]);

	/*
	 * Part 3: FFT kernel benchmark
	 */
	printf("\n============================================================\n");
	printf("  FFT KERNEL BENCHMARK\n");
	printf("============================================================\n");
	run_kernel_benchmark();

	printf("\n============================================================\n");
	printf("  SUMMARY\n");
	printf("============================================================\n");
	printf("\n  Correctness: %d passed, %d failed\n", g_pass, g_fail);

	if (g_fail > 0) {
		printf("  *** SOME TESTS FAILED ***\n\n");
		return 1;
	}

	printf("  All correctness tests PASSED\n\n");
	return 0;
}
