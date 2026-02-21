/*
 * hydrasdr-lfft Test Suite
 *
 * Comprehensive correctness and benchmark tests for small FFT sizes.
 *
 * License: MIT
 * Copyright (c) 2025-2026, Benjamin Vernoux <bvernoux@hydrasdr.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "hydrasdr_lfft.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ============================================================================
 * Test Configuration Constants
 * ========================================================================= */

/* Benchmark parameters */
#define BENCHMARK_ITERATIONS    1000000     /* Number of FFT iterations for benchmark */
#define WARMUP_ITERATIONS       1000        /* Warmup iterations before timing */

/* Tolerance thresholds */
#define TOLERANCE               1e-5f       /* Default numerical tolerance */

/* Random number generation */
#define RAND_RANGE              2000        /* Random value range (-1000 to +1000) */
#define RAND_OFFSET             1000        /* Offset to center range at zero */
#define RAND_SCALE              1000.0f     /* Scale factor for float conversion */

/* Random seeds for reproducibility */
#define RAND_SEED_VS_REF        42          /* Seed for vs-reference test */
#define RAND_SEED_ROUNDTRIP     123         /* Seed for roundtrip test */
#define RAND_SEED_PARSEVAL      456         /* Seed for Parseval test */
#define RAND_SEED_LINEARITY     789         /* Seed for linearity test */
#define RAND_SEED_TIMESHIFT     111         /* Seed for time-shift test */
#define RAND_SEED_SYMMETRY      222         /* Seed for real-symmetry test */

/* Linearity test coefficients */
#define LINEARITY_COEFF_A       2.5f        /* First linear coefficient */
#define LINEARITY_COEFF_B       (-1.3f)     /* Second linear coefficient */

/* Time shift parameters */
#define MIN_SIZE_TIME_SHIFT     4           /* Minimum FFT size for time shift test */
#define TIME_SHIFT_AMOUNT       1           /* Number of samples to shift */

/* Test data initialization */
#define TEST_DATA_MOD_RE        7           /* Modulus for real part init */
#define TEST_DATA_MOD_IM        5           /* Modulus for imaginary part init */

/* Unit conversions */
#define NS_PER_SEC              1e9         /* Nanoseconds per second */
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

static hlfft_complex_t *alloc_complex(size_t n)
{
	size_t size = n * sizeof(hlfft_complex_t);
	hlfft_complex_t *ptr = (hlfft_complex_t *)hlfft_aligned_alloc(size);
	if (ptr)
		memset(ptr, 0, size);
	return ptr;
}

static void free_complex(hlfft_complex_t *ptr)
{
	hlfft_aligned_free(ptr);
}

static float complex_mag(hlfft_complex_t c)
{
	return sqrtf(c.re * c.re + c.im * c.im);
}

static float complex_mag_sq(hlfft_complex_t c)
{
	return c.re * c.re + c.im * c.im;
}

static float max_abs_error(const hlfft_complex_t *a, const hlfft_complex_t *b, size_t n)
{
	float max_err = 0.0f;
	for (size_t i = 0; i < n; i++) {
		float err = complex_mag((hlfft_complex_t){a[i].re - b[i].re, a[i].im - b[i].im});
		if (err > max_err)
			max_err = err;
	}
	return max_err;
}

/* Reference O(N^2) DFT for verification */
static void reference_dft(const hlfft_complex_t *in, hlfft_complex_t *out, size_t n, int inverse)
{
	float sign = inverse ? 1.0f : -1.0f;
	for (size_t k = 0; k < n; k++) {
		out[k].re = 0.0f;
		out[k].im = 0.0f;
		for (size_t j = 0; j < n; j++) {
			float angle = sign * 2.0f * (float)M_PI * (float)k * (float)j / (float)n;
			float c = cosf(angle);
			float s = sinf(angle);
			out[k].re += in[j].re * c - in[j].im * s;
			out[k].im += in[j].re * s + in[j].im * c;
		}
	}
}

/* ============================================================================
 * Correctness Tests
 * ========================================================================= */

/*
 * Test 1: DC Input
 * Input: [1, 1, 1, ..., 1] (all ones)
 * Expected: X[0] = N, X[k] = 0 for k > 0
 */
static int test_dc_input(size_t n)
{
	hlfft_plan_t *plan = hlfft_plan_create(n, NULL);
	TEST_ASSERT(plan != NULL, "plan creation");

	hlfft_complex_t *in = alloc_complex(n);
	hlfft_complex_t *out = alloc_complex(n);
	TEST_ASSERT(in && out, "memory allocation");

	/* All ones input */
	for (size_t i = 0; i < n; i++) {
		in[i].re = 1.0f;
		in[i].im = 0.0f;
	}

	hlfft_forward(plan, in, out);

	/* Check X[0] = N */
	float err0 = fabsf(out[0].re - (float)n) + fabsf(out[0].im);
	TEST_ASSERT(err0 < TOLERANCE, "X[0] should equal N");

	/* Check X[k] = 0 for k > 0 */
	for (size_t k = 1; k < n; k++) {
		float mag = complex_mag(out[k]);
		TEST_ASSERT(mag < TOLERANCE, "X[k] should be zero for k > 0");
	}

	free_complex(in);
	free_complex(out);
	hlfft_plan_destroy(plan);
	return 1;
}

/*
 * Test 2: Impulse Response
 * Input: [1, 0, 0, ..., 0] (delta function)
 * Expected: X[k] = 1 for all k (flat spectrum)
 */
static int test_impulse(size_t n)
{
	hlfft_plan_t *plan = hlfft_plan_create(n, NULL);
	TEST_ASSERT(plan != NULL, "plan creation");

	hlfft_complex_t *in = alloc_complex(n);
	hlfft_complex_t *out = alloc_complex(n);
	TEST_ASSERT(in && out, "memory allocation");

	/* Impulse at t=0 */
	in[0].re = 1.0f;

	hlfft_forward(plan, in, out);

	/* All bins should be 1 */
	for (size_t k = 0; k < n; k++) {
		float err = fabsf(out[k].re - 1.0f) + fabsf(out[k].im);
		TEST_ASSERT(err < TOLERANCE, "all bins should equal 1");
	}

	free_complex(in);
	free_complex(out);
	hlfft_plan_destroy(plan);
	return 1;
}

/*
 * Test 3: Single Tone (all bins)
 * Input: exp(j * 2*pi*k*n/N) for each bin k
 * Expected: Energy only in bin k
 */
static int test_single_tones(size_t n)
{
	hlfft_plan_t *plan = hlfft_plan_create(n, NULL);
	TEST_ASSERT(plan != NULL, "plan creation");

	hlfft_complex_t *in = alloc_complex(n);
	hlfft_complex_t *out = alloc_complex(n);
	TEST_ASSERT(in && out, "memory allocation");

	/* Test each frequency bin */
	for (size_t target_bin = 0; target_bin < n; target_bin++) {
		/* Generate complex exponential at frequency target_bin */
		for (size_t i = 0; i < n; i++) {
			float phase = 2.0f * (float)M_PI * (float)target_bin * (float)i / (float)n;
			in[i].re = cosf(phase);
			in[i].im = sinf(phase);
		}

		hlfft_forward(plan, in, out);

		/* Check energy is only in target bin */
		for (size_t k = 0; k < n; k++) {
			float expected_mag = (k == target_bin) ? (float)n : 0.0f;
			float actual_mag = complex_mag(out[k]);
			float err = fabsf(actual_mag - expected_mag);
			if (err > TOLERANCE * (float)n) {
				if (g_verbose)
					printf("    bin %zu: expected %.2f, got %.2f\n",
					       k, expected_mag, actual_mag);
				TEST_ASSERT(0, "single tone energy mismatch");
			}
		}
	}

	free_complex(in);
	free_complex(out);
	hlfft_plan_destroy(plan);
	return 1;
}

/*
 * Test 4: Comparison with Reference DFT
 * Use random input and compare with O(N^2) DFT
 */
static int test_vs_reference(size_t n)
{
	hlfft_plan_t *plan = hlfft_plan_create(n, NULL);
	TEST_ASSERT(plan != NULL, "plan creation");

	hlfft_complex_t *in = alloc_complex(n);
	hlfft_complex_t *out_fft = alloc_complex(n);
	hlfft_complex_t *out_dft = alloc_complex(n);
	TEST_ASSERT(in && out_fft && out_dft, "memory allocation");

	/* Random input with fixed seed for reproducibility */
	srand(RAND_SEED_VS_REF + (unsigned)n);
	for (size_t i = 0; i < n; i++) {
		in[i].re = (float)(rand() % RAND_RANGE - RAND_OFFSET) / RAND_SCALE;
		in[i].im = (float)(rand() % RAND_RANGE - RAND_OFFSET) / RAND_SCALE;
	}

	hlfft_forward(plan, in, out_fft);
	reference_dft(in, out_dft, n, 0);

	float max_err = max_abs_error(out_fft, out_dft, n);
	if (g_verbose)
		printf("    max error vs reference: %.2e\n", max_err);

	/* Allow larger tolerance for bigger FFTs due to accumulated error */
	float tol = TOLERANCE * (float)n;
	TEST_ASSERT(max_err < tol, "should match reference DFT");

	free_complex(in);
	free_complex(out_fft);
	free_complex(out_dft);
	hlfft_plan_destroy(plan);
	return 1;
}

/*
 * Test 5: Forward-Inverse Roundtrip
 * x -> FFT -> IFFT -> x (with 1/N normalization)
 */
static int test_roundtrip(size_t n)
{
	hlfft_plan_t *plan = hlfft_plan_create(n, NULL);
	TEST_ASSERT(plan != NULL, "plan creation");

	hlfft_complex_t *in = alloc_complex(n);
	hlfft_complex_t *freq = alloc_complex(n);
	hlfft_complex_t *out = alloc_complex(n);
	TEST_ASSERT(in && freq && out, "memory allocation");

	/* Random input */
	srand(RAND_SEED_ROUNDTRIP + (unsigned)n);
	for (size_t i = 0; i < n; i++) {
		in[i].re = (float)(rand() % RAND_RANGE - RAND_OFFSET) / RAND_SCALE;
		in[i].im = (float)(rand() % RAND_RANGE - RAND_OFFSET) / RAND_SCALE;
	}

	hlfft_forward(plan, in, freq);
	hlfft_inverse(plan, freq, out);

	/* Normalize and compare */
	float scale = 1.0f / (float)n;
	for (size_t i = 0; i < n; i++) {
		out[i].re *= scale;
		out[i].im *= scale;
	}

	float max_err = max_abs_error(in, out, n);
	if (g_verbose)
		printf("    roundtrip max error: %.2e\n", max_err);

	TEST_ASSERT(max_err < TOLERANCE, "roundtrip should recover original");

	free_complex(in);
	free_complex(freq);
	free_complex(out);
	hlfft_plan_destroy(plan);
	return 1;
}

/*
 * Test 6: Parseval's Theorem (Energy Conservation)
 * sum(|x[n]|^2) = (1/N) * sum(|X[k]|^2)
 */
static int test_parseval(size_t n)
{
	hlfft_plan_t *plan = hlfft_plan_create(n, NULL);
	TEST_ASSERT(plan != NULL, "plan creation");

	hlfft_complex_t *in = alloc_complex(n);
	hlfft_complex_t *out = alloc_complex(n);
	TEST_ASSERT(in && out, "memory allocation");

	/* Random input */
	srand(RAND_SEED_PARSEVAL + (unsigned)n);
	for (size_t i = 0; i < n; i++) {
		in[i].re = (float)(rand() % RAND_RANGE - RAND_OFFSET) / RAND_SCALE;
		in[i].im = (float)(rand() % RAND_RANGE - RAND_OFFSET) / RAND_SCALE;
	}

	hlfft_forward(plan, in, out);

	/* Time domain energy */
	float time_energy = 0.0f;
	for (size_t i = 0; i < n; i++)
		time_energy += complex_mag_sq(in[i]);

	/* Frequency domain energy (divided by N) */
	float freq_energy = 0.0f;
	for (size_t k = 0; k < n; k++)
		freq_energy += complex_mag_sq(out[k]);
	freq_energy /= (float)n;

	float rel_err = fabsf(time_energy - freq_energy) / time_energy;
	if (g_verbose)
		printf("    time energy: %.4f, freq energy: %.4f, rel err: %.2e\n",
		       time_energy, freq_energy, rel_err);

	TEST_ASSERT(rel_err < TOLERANCE, "energy should be conserved");

	free_complex(in);
	free_complex(out);
	hlfft_plan_destroy(plan);
	return 1;
}

/*
 * Test 7: Linearity
 * FFT(a*x + b*y) = a*FFT(x) + b*FFT(y)
 */
static int test_linearity(size_t n)
{
	hlfft_plan_t *plan = hlfft_plan_create(n, NULL);
	TEST_ASSERT(plan != NULL, "plan creation");

	hlfft_complex_t *x = alloc_complex(n);
	hlfft_complex_t *y = alloc_complex(n);
	hlfft_complex_t *ax_plus_by = alloc_complex(n);
	hlfft_complex_t *fft_x = alloc_complex(n);
	hlfft_complex_t *fft_y = alloc_complex(n);
	hlfft_complex_t *fft_sum = alloc_complex(n);
	hlfft_complex_t *expected = alloc_complex(n);
	TEST_ASSERT(x && y && ax_plus_by && fft_x && fft_y && fft_sum && expected,
		    "memory allocation");

	/* Random inputs */
	srand(RAND_SEED_LINEARITY + (unsigned)n);
	for (size_t i = 0; i < n; i++) {
		x[i].re = (float)(rand() % RAND_RANGE - RAND_OFFSET) / RAND_SCALE;
		x[i].im = (float)(rand() % RAND_RANGE - RAND_OFFSET) / RAND_SCALE;
		y[i].re = (float)(rand() % RAND_RANGE - RAND_OFFSET) / RAND_SCALE;
		y[i].im = (float)(rand() % RAND_RANGE - RAND_OFFSET) / RAND_SCALE;
	}

	/* Coefficients */
	float a = LINEARITY_COEFF_A, b = LINEARITY_COEFF_B;

	/* Compute a*x + b*y */
	for (size_t i = 0; i < n; i++) {
		ax_plus_by[i].re = a * x[i].re + b * y[i].re;
		ax_plus_by[i].im = a * x[i].im + b * y[i].im;
	}

	/* FFT(a*x + b*y) */
	hlfft_forward(plan, ax_plus_by, fft_sum);

	/* a*FFT(x) + b*FFT(y) */
	hlfft_forward(plan, x, fft_x);
	hlfft_forward(plan, y, fft_y);
	for (size_t k = 0; k < n; k++) {
		expected[k].re = a * fft_x[k].re + b * fft_y[k].re;
		expected[k].im = a * fft_x[k].im + b * fft_y[k].im;
	}

	float max_err = max_abs_error(fft_sum, expected, n);
	if (g_verbose)
		printf("    linearity max error: %.2e\n", max_err);

	TEST_ASSERT(max_err < TOLERANCE * (float)n, "linearity should hold");

	free_complex(x);
	free_complex(y);
	free_complex(ax_plus_by);
	free_complex(fft_x);
	free_complex(fft_y);
	free_complex(fft_sum);
	free_complex(expected);
	hlfft_plan_destroy(plan);
	return 1;
}

/*
 * Test 8: Time Shift Property
 * Shifting input by m samples multiplies FFT by exp(-j*2*pi*k*m/N)
 */
static int test_time_shift(size_t n)
{
	if (n < MIN_SIZE_TIME_SHIFT) return 1;  /* Skip for very small N */

	hlfft_plan_t *plan = hlfft_plan_create(n, NULL);
	TEST_ASSERT(plan != NULL, "plan creation");

	hlfft_complex_t *x = alloc_complex(n);
	hlfft_complex_t *x_shifted = alloc_complex(n);
	hlfft_complex_t *fft_x = alloc_complex(n);
	hlfft_complex_t *fft_shifted = alloc_complex(n);
	hlfft_complex_t *expected = alloc_complex(n);
	TEST_ASSERT(x && x_shifted && fft_x && fft_shifted && expected,
		    "memory allocation");

	/* Random input */
	srand(RAND_SEED_TIMESHIFT + (unsigned)n);
	for (size_t i = 0; i < n; i++) {
		x[i].re = (float)(rand() % RAND_RANGE - RAND_OFFSET) / RAND_SCALE;
		x[i].im = (float)(rand() % RAND_RANGE - RAND_OFFSET) / RAND_SCALE;
	}

	/* Shift by m samples (circular) */
	size_t m = TIME_SHIFT_AMOUNT;
	for (size_t i = 0; i < n; i++)
		x_shifted[i] = x[(i + n - m) % n];

	hlfft_forward(plan, x, fft_x);
	hlfft_forward(plan, x_shifted, fft_shifted);

	/* Expected: fft_x[k] * exp(-j*2*pi*k*m/N) */
	for (size_t k = 0; k < n; k++) {
		float angle = -2.0f * (float)M_PI * (float)k * (float)m / (float)n;
		float c = cosf(angle);
		float s = sinf(angle);
		expected[k].re = fft_x[k].re * c - fft_x[k].im * s;
		expected[k].im = fft_x[k].re * s + fft_x[k].im * c;
	}

	float max_err = max_abs_error(fft_shifted, expected, n);
	if (g_verbose)
		printf("    time shift max error: %.2e\n", max_err);

	TEST_ASSERT(max_err < TOLERANCE * (float)n, "time shift property should hold");

	free_complex(x);
	free_complex(x_shifted);
	free_complex(fft_x);
	free_complex(fft_shifted);
	free_complex(expected);
	hlfft_plan_destroy(plan);
	return 1;
}

/*
 * Test 9: Conjugate Symmetry for Real Input
 * For real x[n]: X[N-k] = conj(X[k])
 */
static int test_real_symmetry(size_t n)
{
	hlfft_plan_t *plan = hlfft_plan_create(n, NULL);
	TEST_ASSERT(plan != NULL, "plan creation");

	hlfft_complex_t *in = alloc_complex(n);
	hlfft_complex_t *out = alloc_complex(n);
	TEST_ASSERT(in && out, "memory allocation");

	/* Real-valued input (imaginary = 0) */
	srand(RAND_SEED_SYMMETRY + (unsigned)n);
	for (size_t i = 0; i < n; i++) {
		in[i].re = (float)(rand() % RAND_RANGE - RAND_OFFSET) / RAND_SCALE;
		in[i].im = 0.0f;
	}

	hlfft_forward(plan, in, out);

	/* Check X[N-k] = conj(X[k]) */
	for (size_t k = 1; k < n / 2; k++) {
		size_t conj_k = n - k;
		float err_re = fabsf(out[k].re - out[conj_k].re);
		float err_im = fabsf(out[k].im + out[conj_k].im);  /* Should be negated */
		float err = err_re + err_im;
		if (err > TOLERANCE * (float)n) {
			if (g_verbose)
				printf("    k=%zu: X[k]=(%.4f,%.4f) X[N-k]=(%.4f,%.4f)\n",
				       k, out[k].re, out[k].im, out[conj_k].re, out[conj_k].im);
			TEST_ASSERT(0, "conjugate symmetry should hold for real input");
		}
	}

	free_complex(in);
	free_complex(out);
	hlfft_plan_destroy(plan);
	return 1;
}

/* ============================================================================
 * Benchmark
 * ========================================================================= */

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

static void run_benchmark(size_t n, int iterations)
{
	hlfft_plan_t *plan = hlfft_plan_create(n, NULL);
	if (!plan) {
		printf("  N=%-2zu  ERROR: plan creation failed\n", n);
		return;
	}

	hlfft_complex_t *in = alloc_complex(n);
	hlfft_complex_t *out = alloc_complex(n);
	if (!in || !out) {
		printf("  N=%-2zu  ERROR: memory allocation failed\n", n);
		hlfft_plan_destroy(plan);
		return;
	}

	/* Initialize with test data */
	for (size_t i = 0; i < n; i++) {
		in[i].re = (float)(i % TEST_DATA_MOD_RE) / (float)TEST_DATA_MOD_RE;
		in[i].im = (float)(i % TEST_DATA_MOD_IM) / (float)TEST_DATA_MOD_IM;
	}

	/* Warmup */
	for (int i = 0; i < WARMUP_ITERATIONS; i++) {
		hlfft_forward(plan, in, out);
		hlfft_inverse(plan, out, in);
	}

	/* Benchmark forward FFT */
	double start = get_time_sec();
	for (int i = 0; i < iterations; i++)
		hlfft_forward(plan, in, out);
	double fwd_time = get_time_sec() - start;

	/* Benchmark inverse FFT */
	start = get_time_sec();
	for (int i = 0; i < iterations; i++)
		hlfft_inverse(plan, out, in);
	double inv_time = get_time_sec() - start;

	/* Calculate metrics */
	double fwd_ns = (fwd_time * NS_PER_SEC) / iterations;
	double inv_ns = (inv_time * NS_PER_SEC) / iterations;
	double fwd_mfft = iterations / fwd_time / MSPS_DIVISOR;
	double inv_mfft = iterations / inv_time / MSPS_DIVISOR;

	/* For SDR context: samples processed per second */
	double fwd_msps = (n * iterations) / fwd_time / MSPS_DIVISOR;

	printf("  N=%-2zu  fwd: %6.1f ns (%5.1f M-FFT/s, %5.1f MSps)  "
	       "inv: %6.1f ns (%5.1f M-FFT/s)\n",
	       n, fwd_ns, fwd_mfft, fwd_msps, inv_ns, inv_mfft);

	free_complex(in);
	free_complex(out);
	hlfft_plan_destroy(plan);
}

/* ============================================================================
 * Main
 * ========================================================================= */

static void run_correctness_tests(void)
{
	printf("\n");
	printf("=============================================================\n");
	printf("                    CORRECTNESS TESTS\n");
	printf("=============================================================\n");

	size_t sizes[] = {2, 4, 8, 16, 32};
	int num_sizes = sizeof(sizes) / sizeof(sizes[0]);

	for (int i = 0; i < num_sizes; i++) {
		size_t n = sizes[i];
		char name[64];

		printf("\n--- FFT Size N=%zu ---\n", n);

		snprintf(name, sizeof(name), "DC input (N=%zu)", n);
		test_result(name, test_dc_input(n));

		snprintf(name, sizeof(name), "Impulse response (N=%zu)", n);
		test_result(name, test_impulse(n));

		snprintf(name, sizeof(name), "Single tones all bins (N=%zu)", n);
		test_result(name, test_single_tones(n));

		snprintf(name, sizeof(name), "vs Reference DFT (N=%zu)", n);
		test_result(name, test_vs_reference(n));

		snprintf(name, sizeof(name), "Forward-Inverse roundtrip (N=%zu)", n);
		test_result(name, test_roundtrip(n));

		snprintf(name, sizeof(name), "Parseval energy conservation (N=%zu)", n);
		test_result(name, test_parseval(n));

		snprintf(name, sizeof(name), "Linearity (N=%zu)", n);
		test_result(name, test_linearity(n));

		snprintf(name, sizeof(name), "Time shift property (N=%zu)", n);
		test_result(name, test_time_shift(n));

		snprintf(name, sizeof(name), "Real input symmetry (N=%zu)", n);
		test_result(name, test_real_symmetry(n));
	}
}

static void run_benchmarks(void)
{
	printf("\n");
	printf("=============================================================\n");
	printf("                      BENCHMARKS\n");
	printf("=============================================================\n");
	printf("\n");
	printf("  Iterations: %d\n", BENCHMARK_ITERATIONS);
	printf("  MSps = Million samples/second (N * M-FFT/s)\n");
	printf("\n");
	printf("  Size  Forward FFT                              Inverse FFT\n");
	printf("  ----  -----------------------------------------  ------------------\n");

	size_t sizes[] = {2, 4, 8, 16, 32};
	int num_sizes = sizeof(sizes) / sizeof(sizes[0]);

	for (int i = 0; i < num_sizes; i++)
		run_benchmark(sizes[i], BENCHMARK_ITERATIONS);
}

int main(int argc, char *argv[])
{
	/* Check for verbose flag */
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0)
			g_verbose = 1;
	}

	printf("\n");
	printf("*************************************************************\n");
	printf("*           hydrasdr-lfft Test Suite v%s                *\n", hlfft_version());
	printf("*************************************************************\n");
	printf("  %s\n", hlfft_build_info());

	/* Initialize library */
	if (hlfft_init() != HLFFT_OK) {
		fprintf(stderr, "ERROR: Failed to initialize hydrasdr-lfft\n");
		return 1;
	}

	/* Run tests */
	run_correctness_tests();
	run_benchmarks();

	/* Summary */
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
