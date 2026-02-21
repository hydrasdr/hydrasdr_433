/*
 * Stockham FFT - SCALAR Backend (Auto-vectorized C99)
 *
 * Radix-4 with On-The-Fly (OTF) Twiddle Computation:
 * - Stores only W^k twiddles (3x less memory)
 * - Computes W^{2k} = W^k * W^k on-the-fly
 * - Computes W^{3k} = W^{2k} * W^k on-the-fly
 * - ~2x faster than Radix-2 due to better cache utilization
 *
 * This is the portable baseline implementation that works on all platforms.
 * It relies on compiler auto-vectorization with hints for SIMD optimization.
 *
 * License: MIT
 * Copyright (c) 2025-2026, Benjamin Vernoux <bvernoux@hydrasdr.com>
 */

#include "stockham_internal.h"
#include "compat_opt.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SFFT_ALIGN 64
#define SFFT_MIN_SIZE 2  /* Minimum FFT size for PFB channelizers */

/* ============================================================================
 * Internal Plan Structure (Radix-4 OTF)
 * ========================================================================= */

struct sfft_plan {
	const sfft_backend_vtable_t *vtable;	/* Backend that created this plan */

	size_t n;
	size_t log2n;
	size_t log4n;		/* Number of radix-4 stages */
	int has_radix2_stage;	/* Final radix-2 stage if log2n is odd */

	/* Only store W^k twiddles (not W^{2k}, W^{3k}) */
	float **tw_re;
	float **tw_im;

	/* R2C/C2R support: twiddles for N/2-point internal FFT */
	size_t half_log4n;		/* Number of radix-4 stages for N/2-point FFT */
	int half_has_radix2_stage;	/* Final radix-2 for N/2-point FFT */
	float **half_tw_re;		/* Twiddles for N/2-point FFT */
	float **half_tw_im;

	/* R2C/C2R support: post-processing twiddles W_N^k */
	float *r2c_tw_re;		/* cos(-2*pi*k/N) for k=0..N/2-1 */
	float *r2c_tw_im;		/* sin(-2*pi*k/N) for k=0..N/2-1 */

	/* R2C/C2R support: internal N/2-point FFT plan for reusing C2C code */
	struct sfft_plan *half_plan;	/* NULL if N < 2*SFFT_MIN_SIZE */

	/* Work buffers in SoA format */
	float *OPT_RESTRICT work_re;
	float *OPT_RESTRICT work_im;
	float *OPT_RESTRICT work2_re;
	float *OPT_RESTRICT work2_im;
};

/* ============================================================================
 * Memory Helpers
 * ========================================================================= */

static void *scalar_aligned_alloc(size_t size)
{
	return opt_aligned_alloc(size, SFFT_ALIGN);
}

static void scalar_aligned_free(void *ptr)
{
	opt_aligned_free(ptr);
}

/* ============================================================================
 * Utility Functions
 * ========================================================================= */

static OPT_INLINE int is_power_of_2(size_t n)
{
	return n > 0 && (n & (n - 1)) == 0;
}

static OPT_INLINE size_t log2_size(size_t n)
{
	size_t k = 0;
	while ((1ULL << k) < n)
		k++;
	return k;
}

/* ============================================================================
 * Twiddle Factor Computation (OTF - only W^k stored)
 * ========================================================================= */

static int compute_twiddles(sfft_plan_t *plan)
{
	const size_t n = plan->n;
	const size_t log4n = plan->log4n;
	const double neg_2pi_over_n = -2.0 * M_PI / (double)n;

	/* Handle n=2 case: no radix-4 stages, only radix-2 */
	if (log4n == 0) {
		plan->tw_re = NULL;
		plan->tw_im = NULL;
		return 0;
	}

	plan->tw_re = (float **)calloc(log4n, sizeof(float *));
	plan->tw_im = (float **)calloc(log4n, sizeof(float *));

	if (!plan->tw_re || !plan->tw_im) {
		free(plan->tw_re);
		free(plan->tw_im);
		plan->tw_re = NULL;
		plan->tw_im = NULL;
		return -1;
	}

	for (size_t s = 0; s < log4n; s++) {
		const size_t m = n >> (s * 2);
		const size_t quarter_m = m >> 2;
		const size_t stride = 1ULL << (s * 2);

		plan->tw_re[s] = (float *)scalar_aligned_alloc(quarter_m * sizeof(float));
		plan->tw_im[s] = (float *)scalar_aligned_alloc(quarter_m * sizeof(float));

		if (!plan->tw_re[s] || !plan->tw_im[s]) {
			/* Clean up all previously allocated stages */
			for (size_t i = 0; i <= s; i++) {
				scalar_aligned_free(plan->tw_re[i]);
				scalar_aligned_free(plan->tw_im[i]);
			}
			free(plan->tw_re);
			free(plan->tw_im);
			plan->tw_re = NULL;
			plan->tw_im = NULL;
			return -1;
		}

		/* Compute only W^k twiddles */
		for (size_t j = 0; j < quarter_m; j++) {
			double angle = neg_2pi_over_n * (double)(j * stride);
			plan->tw_re[s][j] = (float)cos(angle);
			plan->tw_im[s][j] = (float)sin(angle);
		}
	}
	return 0;
}

/* ============================================================================
 * R2C Twiddle Factor Computation
 *
 * Precomputes all twiddles needed for R2C/C2R transforms:
 * 1. Twiddles for the internal N/2-point FFT
 * 2. Post-processing twiddles W_N^k for k=0..N/2-1
 * ========================================================================= */

static int compute_r2c_twiddles(sfft_plan_t *plan)
{
	const size_t n = plan->n;
	const size_t half_n = n >> 1;
	const size_t half_log2n = plan->log2n - 1;
	const double neg_2pi_over_n = -2.0 * M_PI / (double)n;
	const double neg_4pi_over_n = -4.0 * M_PI / (double)n; /* -2pi / (n/2) */

	plan->half_log4n = half_log2n / 2;
	plan->half_has_radix2_stage = (half_log2n % 2) != 0;

	/* Allocate twiddles for N/2-point FFT */
	if (plan->half_log4n > 0) {
		plan->half_tw_re = (float **)calloc(plan->half_log4n, sizeof(float *));
		plan->half_tw_im = (float **)calloc(plan->half_log4n, sizeof(float *));

		if (!plan->half_tw_re || !plan->half_tw_im)
			return -1;

		for (size_t s = 0; s < plan->half_log4n; s++) {
			const size_t m = half_n >> (s * 2);
			const size_t quarter_m = m >> 2;
			const size_t stride = 1ULL << (s * 2);

			plan->half_tw_re[s] = (float *)scalar_aligned_alloc(quarter_m * sizeof(float));
			plan->half_tw_im[s] = (float *)scalar_aligned_alloc(quarter_m * sizeof(float));

			if (!plan->half_tw_re[s] || !plan->half_tw_im[s])
				return -1;

			/* Compute W^k twiddles for N/2-point FFT */
			for (size_t j = 0; j < quarter_m; j++) {
				double angle = neg_4pi_over_n * (double)(j * stride);
				plan->half_tw_re[s][j] = (float)cos(angle);
				plan->half_tw_im[s][j] = (float)sin(angle);
			}
		}
	} else {
		plan->half_tw_re = NULL;
		plan->half_tw_im = NULL;
	}

	/* Allocate R2C post-processing twiddles W_N^k for k=0..N/2-1 */
	plan->r2c_tw_re = (float *)scalar_aligned_alloc(half_n * sizeof(float));
	plan->r2c_tw_im = (float *)scalar_aligned_alloc(half_n * sizeof(float));

	if (!plan->r2c_tw_re || !plan->r2c_tw_im)
		return -1;

	for (size_t k = 0; k < half_n; k++) {
		double angle = neg_2pi_over_n * (double)k;
		plan->r2c_tw_re[k] = (float)cos(angle);
		plan->r2c_tw_im[k] = (float)sin(angle);
	}

	return 0;
}

/* ============================================================================
 * Plan Management
 * ========================================================================= */

/* Forward declaration for cleanup in error paths */
static void scalar_plan_destroy(sfft_plan_t *plan);

static sfft_plan_t *scalar_plan_create(size_t n)
{
	if (!is_power_of_2(n) || n < SFFT_MIN_SIZE)
		return NULL;

	sfft_plan_t *plan = (sfft_plan_t *)calloc(1, sizeof(sfft_plan_t));
	if (OPT_UNLIKELY(!plan))
		return NULL;

	plan->vtable = &sfft_backend_scalar;
	plan->n = n;
	plan->log2n = log2_size(n);
	plan->log4n = plan->log2n / 2;
	plan->has_radix2_stage = (plan->log2n % 2) != 0;

	/* Allocate SoA work buffers */
	plan->work_re = (float *)scalar_aligned_alloc(n * sizeof(float));
	plan->work_im = (float *)scalar_aligned_alloc(n * sizeof(float));
	plan->work2_re = (float *)scalar_aligned_alloc(n * sizeof(float));
	plan->work2_im = (float *)scalar_aligned_alloc(n * sizeof(float));

	if (OPT_UNLIKELY(!plan->work_re || !plan->work_im ||
			 !plan->work2_re || !plan->work2_im)) {
		scalar_aligned_free(plan->work_re);
		scalar_aligned_free(plan->work_im);
		scalar_aligned_free(plan->work2_re);
		scalar_aligned_free(plan->work2_im);
		free(plan);
		return NULL;
	}

	if (compute_twiddles(plan) != 0) {
		scalar_aligned_free(plan->work_re);
		scalar_aligned_free(plan->work_im);
		scalar_aligned_free(plan->work2_re);
		scalar_aligned_free(plan->work2_im);
		if (plan->tw_re) {
			for (size_t s = 0; s < plan->log4n; s++)
				scalar_aligned_free(plan->tw_re[s]);
			free(plan->tw_re);
		}
		if (plan->tw_im) {
			for (size_t s = 0; s < plan->log4n; s++)
				scalar_aligned_free(plan->tw_im[s]);
			free(plan->tw_im);
		}
		free(plan);
		return NULL;
	}

	if (compute_r2c_twiddles(plan) != 0) {
		scalar_plan_destroy(plan);
		return NULL;
	}

	return plan;
}

static void scalar_plan_destroy(sfft_plan_t *plan)
{
	if (!plan)
		return;

	/* Free C2C twiddles */
	if (plan->tw_re) {
		for (size_t s = 0; s < plan->log4n; s++)
			scalar_aligned_free(plan->tw_re[s]);
		free(plan->tw_re);
	}
	if (plan->tw_im) {
		for (size_t s = 0; s < plan->log4n; s++)
			scalar_aligned_free(plan->tw_im[s]);
		free(plan->tw_im);
	}

	/* Free R2C twiddles for N/2-point FFT */
	if (plan->half_tw_re) {
		for (size_t s = 0; s < plan->half_log4n; s++)
			scalar_aligned_free(plan->half_tw_re[s]);
		free(plan->half_tw_re);
	}
	if (plan->half_tw_im) {
		for (size_t s = 0; s < plan->half_log4n; s++)
			scalar_aligned_free(plan->half_tw_im[s]);
		free(plan->half_tw_im);
	}

	/* Free R2C post-processing twiddles */
	scalar_aligned_free(plan->r2c_tw_re);
	scalar_aligned_free(plan->r2c_tw_im);

	scalar_aligned_free(plan->work_re);
	scalar_aligned_free(plan->work_im);
	scalar_aligned_free(plan->work2_re);
	scalar_aligned_free(plan->work2_im);
	free(plan);
}

static size_t scalar_plan_size(const sfft_plan_t *plan)
{
	return plan ? plan->n : 0;
}

/* ============================================================================
 * Radix-4 Kernel with OTF Twiddle Computation
 *
 * Optimizations applied:
 * 1. Alignment hints for 64-byte aligned buffers
 * 2. Software prefetching for next block's data
 * 3. Explicit 4x loop unrolling for better ILP
 * 4. Pre-computed stride offsets outside inner loop
 * ========================================================================= */

/*
 * Radix-4 butterfly macro for a single element
 * Reduces code duplication in unrolled loop
 */
#define RADIX4_BUTTERFLY_OTF(j_idx)					\
do {									\
	const float a0r = pa0_re[j_idx], a0i = pa0_im[j_idx];		\
	const float a1r = pa1_re[j_idx], a1i = pa1_im[j_idx];		\
	const float a2r = pa2_re[j_idx], a2i = pa2_im[j_idx];		\
	const float a3r = pa3_re[j_idx], a3i = pa3_im[j_idx];		\
									\
	const float w1r = tw_re[j_idx], w1i = tw_im[j_idx];		\
									\
	/* Compute W2 = W1 * W1 on-the-fly */				\
	const float w2r = OPT_FMA_F32(w1r, w1r, -(w1i * w1i));		\
	const float w2i = 2.0f * w1r * w1i;				\
									\
	/* Compute W3 = W2 * W1 on-the-fly */				\
	const float w3r = OPT_FMA_F32(w2r, w1r, -(w2i * w1i));		\
	const float w3i = OPT_FMA_F32(w2r, w1i, w2i * w1r);		\
									\
	/* Radix-4 butterfly */						\
	const float t0r = a0r + a2r, t0i = a0i + a2i;			\
	const float t1r = a0r - a2r, t1i = a0i - a2i;			\
	const float t2r = a1r + a3r, t2i = a1i + a3i;			\
	const float t3r = a1r - a3r, t3i = a1i - a3i;			\
									\
	/* X0 = t0 + t2 (no twiddle) */					\
	pd0_re[j_idx] = t0r + t2r;					\
	pd0_im[j_idx] = t0i + t2i;					\
									\
	/* X1 = (t1 - j*t3) * W1 */					\
	const float u1r = t1r + t3i, u1i = t1i - t3r;			\
	pd1_re[j_idx] = OPT_FMA_F32(u1r, w1r, -(u1i * w1i));		\
	pd1_im[j_idx] = OPT_FMA_F32(u1r, w1i, u1i * w1r);		\
									\
	/* X2 = (t0 - t2) * W2 */					\
	const float u2r = t0r - t2r, u2i = t0i - t2i;			\
	pd2_re[j_idx] = OPT_FMA_F32(u2r, w2r, -(u2i * w2i));		\
	pd2_im[j_idx] = OPT_FMA_F32(u2r, w2i, u2i * w2r);		\
									\
	/* X3 = (t1 + j*t3) * W3 */					\
	const float u3r = t1r - t3i, u3i = t1i + t3r;			\
	pd3_re[j_idx] = OPT_FMA_F32(u3r, w3r, -(u3i * w3i));		\
	pd3_im[j_idx] = OPT_FMA_F32(u3r, w3i, u3i * w3r);		\
} while (0)

static OPT_HOT void stockham_radix4_otf_scalar(
	const float *OPT_RESTRICT src_re,
	const float *OPT_RESTRICT src_im,
	float *OPT_RESTRICT dst_re,
	float *OPT_RESTRICT dst_im,
	const float *OPT_RESTRICT tw_re,
	const float *OPT_RESTRICT tw_im,
	size_t n,
	size_t stage)
{
	const size_t quarter_n = n >> 2;
	const size_t m = n >> (stage * 2);
	const size_t quarter_m = m >> 2;
	const size_t num_blocks = 1ULL << (stage * 2);

	/* Alignment assumptions help compiler with vectorization */
	src_re = OPT_ASSUME_ALIGNED(src_re, SFFT_ALIGN);
	src_im = OPT_ASSUME_ALIGNED(src_im, SFFT_ALIGN);
	dst_re = OPT_ASSUME_ALIGNED(dst_re, SFFT_ALIGN);
	dst_im = OPT_ASSUME_ALIGNED(dst_im, SFFT_ALIGN);
	tw_re = OPT_ASSUME_ALIGNED(tw_re, SFFT_ALIGN);
	tw_im = OPT_ASSUME_ALIGNED(tw_im, SFFT_ALIGN);

	for (size_t b = 0; b < num_blocks; b++) {
		/* Pre-compute source base offset once per block */
		const size_t src_base = b * m;
		const size_t dst_base = b * quarter_m;

		const float *OPT_RESTRICT pa0_re = src_re + src_base;
		const float *OPT_RESTRICT pa0_im = src_im + src_base;
		const float *OPT_RESTRICT pa1_re = pa0_re + quarter_m;
		const float *OPT_RESTRICT pa1_im = pa0_im + quarter_m;
		const float *OPT_RESTRICT pa2_re = pa1_re + quarter_m;
		const float *OPT_RESTRICT pa2_im = pa1_im + quarter_m;
		const float *OPT_RESTRICT pa3_re = pa2_re + quarter_m;
		const float *OPT_RESTRICT pa3_im = pa2_im + quarter_m;

		float *OPT_RESTRICT pd0_re = dst_re + dst_base;
		float *OPT_RESTRICT pd0_im = dst_im + dst_base;
		float *OPT_RESTRICT pd1_re = pd0_re + quarter_n;
		float *OPT_RESTRICT pd1_im = pd0_im + quarter_n;
		float *OPT_RESTRICT pd2_re = pd1_re + quarter_n;
		float *OPT_RESTRICT pd2_im = pd1_im + quarter_n;
		float *OPT_RESTRICT pd3_re = pd2_re + quarter_n;
		float *OPT_RESTRICT pd3_im = pd2_im + quarter_n;

		/* Prefetch next block's source data into L1 cache */
		if (b + 1 < num_blocks) {
			const size_t next_base = (b + 1) * m;
			OPT_PREFETCH_READ(src_re + next_base);
			OPT_PREFETCH_READ(src_im + next_base);
			OPT_PREFETCH_READ(src_re + next_base + quarter_m);
			OPT_PREFETCH_READ(src_im + next_base + quarter_m);
		}

		/*
		 * Process loop with 4x unrolling when quarter_m >= 4
		 * This improves ILP by giving the compiler more independent
		 * operations to schedule.
		 */
		size_t j = 0;

		/* Unrolled portion: 4 elements per iteration */
		if (quarter_m >= 4) {
			const size_t unroll_limit = quarter_m & ~(size_t)3;

			OPT_PRAGMA_VECTORIZE
			for (; j < unroll_limit; j += 4) {
				RADIX4_BUTTERFLY_OTF(j);
				RADIX4_BUTTERFLY_OTF(j + 1);
				RADIX4_BUTTERFLY_OTF(j + 2);
				RADIX4_BUTTERFLY_OTF(j + 3);
			}
		}

		/* Remainder: handle remaining 0-3 elements */
		OPT_PRAGMA_VECTORIZE
		for (; j < quarter_m; j++) {
			RADIX4_BUTTERFLY_OTF(j);
		}
	}
}

#undef RADIX4_BUTTERFLY_OTF

/* ============================================================================
 * Final Radix-2 Stage (when log2n is odd)
 *
 * Optimizations applied:
 * 1. Alignment hints
 * 2. 4x loop unrolling for better ILP
 * 3. Prefetching for large arrays
 * ========================================================================= */

static OPT_HOT void stockham_radix2_last_scalar(
	const float *OPT_RESTRICT src_re,
	const float *OPT_RESTRICT src_im,
	float *OPT_RESTRICT dst_re,
	float *OPT_RESTRICT dst_im,
	size_t n)
{
	const size_t half_n = n >> 1;

	/* Alignment assumptions */
	src_re = OPT_ASSUME_ALIGNED(src_re, SFFT_ALIGN);
	src_im = OPT_ASSUME_ALIGNED(src_im, SFFT_ALIGN);
	dst_re = OPT_ASSUME_ALIGNED(dst_re, SFFT_ALIGN);
	dst_im = OPT_ASSUME_ALIGNED(dst_im, SFFT_ALIGN);

	/* Prefetch destination for write */
	OPT_PREFETCH_WRITE(dst_re);
	OPT_PREFETCH_WRITE(dst_im);
	OPT_PREFETCH_WRITE(dst_re + half_n);
	OPT_PREFETCH_WRITE(dst_im + half_n);

	size_t b = 0;

	/* Unrolled loop: process 4 pairs per iteration */
	if (half_n >= 4) {
		const size_t unroll_limit = half_n & ~(size_t)3;

		OPT_PRAGMA_VECTORIZE
		for (; b < unroll_limit; b += 4) {
			/* Prefetch ahead for large arrays */
			if (b + 16 < half_n) {
				OPT_PREFETCH_READ(src_re + (b + 16) * 2);
				OPT_PREFETCH_READ(src_im + (b + 16) * 2);
			}

			/* Element 0 */
			{
				const size_t src_idx = b * 2;
				const float ar = src_re[src_idx];
				const float ai = src_im[src_idx];
				const float br = src_re[src_idx + 1];
				const float bi = src_im[src_idx + 1];
				dst_re[b] = ar + br;
				dst_im[b] = ai + bi;
				dst_re[b + half_n] = ar - br;
				dst_im[b + half_n] = ai - bi;
			}

			/* Element 1 */
			{
				const size_t src_idx = (b + 1) * 2;
				const float ar = src_re[src_idx];
				const float ai = src_im[src_idx];
				const float br = src_re[src_idx + 1];
				const float bi = src_im[src_idx + 1];
				dst_re[b + 1] = ar + br;
				dst_im[b + 1] = ai + bi;
				dst_re[b + 1 + half_n] = ar - br;
				dst_im[b + 1 + half_n] = ai - bi;
			}

			/* Element 2 */
			{
				const size_t src_idx = (b + 2) * 2;
				const float ar = src_re[src_idx];
				const float ai = src_im[src_idx];
				const float br = src_re[src_idx + 1];
				const float bi = src_im[src_idx + 1];
				dst_re[b + 2] = ar + br;
				dst_im[b + 2] = ai + bi;
				dst_re[b + 2 + half_n] = ar - br;
				dst_im[b + 2 + half_n] = ai - bi;
			}

			/* Element 3 */
			{
				const size_t src_idx = (b + 3) * 2;
				const float ar = src_re[src_idx];
				const float ai = src_im[src_idx];
				const float br = src_re[src_idx + 1];
				const float bi = src_im[src_idx + 1];
				dst_re[b + 3] = ar + br;
				dst_im[b + 3] = ai + bi;
				dst_re[b + 3 + half_n] = ar - br;
				dst_im[b + 3 + half_n] = ai - bi;
			}
		}
	}

	/* Handle remainder */
	OPT_PRAGMA_VECTORIZE
	for (; b < half_n; b++) {
		const size_t src_idx = b * 2;
		const float ar = src_re[src_idx];
		const float ai = src_im[src_idx];
		const float br = src_re[src_idx + 1];
		const float bi = src_im[src_idx + 1];

		dst_re[b] = ar + br;
		dst_im[b] = ai + bi;
		dst_re[b + half_n] = ar - br;
		dst_im[b + half_n] = ai - bi;
	}
}

/* ============================================================================
 * Format Conversion
 *
 * Optimizations applied:
 * 1. Alignment hints
 * 2. 4x loop unrolling
 * 3. Prefetching for large arrays
 * ========================================================================= */

static OPT_HOT void aos_to_soa(
	const sfft_complex_t *OPT_RESTRICT aos,
	float *OPT_RESTRICT re,
	float *OPT_RESTRICT im,
	size_t n)
{
	re = OPT_ASSUME_ALIGNED(re, SFFT_ALIGN);
	im = OPT_ASSUME_ALIGNED(im, SFFT_ALIGN);

	size_t i = 0;

	/* Unrolled loop: 4 elements per iteration */
	if (n >= 4) {
		const size_t unroll_limit = n & ~(size_t)3;

		OPT_PRAGMA_VECTORIZE
		for (; i < unroll_limit; i += 4) {
			/* Prefetch ahead */
			if (i + 32 < n) {
				OPT_PREFETCH_READ(&aos[i + 32]);
			}

			re[i]     = aos[i].re;
			im[i]     = aos[i].im;
			re[i + 1] = aos[i + 1].re;
			im[i + 1] = aos[i + 1].im;
			re[i + 2] = aos[i + 2].re;
			im[i + 2] = aos[i + 2].im;
			re[i + 3] = aos[i + 3].re;
			im[i + 3] = aos[i + 3].im;
		}
	}

	/* Remainder */
	for (; i < n; i++) {
		re[i] = aos[i].re;
		im[i] = aos[i].im;
	}
}

static OPT_HOT void soa_to_aos(
	const float *OPT_RESTRICT re,
	const float *OPT_RESTRICT im,
	sfft_complex_t *OPT_RESTRICT aos,
	size_t n)
{
	re = OPT_ASSUME_ALIGNED(re, SFFT_ALIGN);
	im = OPT_ASSUME_ALIGNED(im, SFFT_ALIGN);

	size_t i = 0;

	/* Unrolled loop: 4 elements per iteration */
	if (n >= 4) {
		const size_t unroll_limit = n & ~(size_t)3;

		OPT_PRAGMA_VECTORIZE
		for (; i < unroll_limit; i += 4) {
			/* Prefetch ahead */
			if (i + 32 < n) {
				OPT_PREFETCH_READ(re + i + 32);
				OPT_PREFETCH_READ(im + i + 32);
			}

			aos[i].re     = re[i];
			aos[i].im     = im[i];
			aos[i + 1].re = re[i + 1];
			aos[i + 1].im = im[i + 1];
			aos[i + 2].re = re[i + 2];
			aos[i + 2].im = im[i + 2];
			aos[i + 3].re = re[i + 3];
			aos[i + 3].im = im[i + 3];
		}
	}

	/* Remainder */
	for (; i < n; i++) {
		aos[i].re = re[i];
		aos[i].im = im[i];
	}
}

/* ============================================================================
 * FFT Execution
 * ========================================================================= */

static void scalar_forward(const sfft_plan_t *plan,
			   const sfft_complex_t *OPT_RESTRICT input,
			   sfft_complex_t *OPT_RESTRICT output)
{
	if (OPT_UNLIKELY(!plan || !input || !output))
		return;

	const size_t n = plan->n;
	const size_t log4n = plan->log4n;

	/* Convert input AoS -> SoA */
	aos_to_soa(input, plan->work_re, plan->work_im, n);

	/* Ping-pong between work buffers */
	const float *src_re = plan->work_re;
	const float *src_im = plan->work_im;
	float *dst_re = plan->work2_re;
	float *dst_im = plan->work2_im;

	/* Radix-4 stages with OTF twiddle computation */
	for (size_t s = 0; s < log4n; s++) {
		stockham_radix4_otf_scalar(src_re, src_im, dst_re, dst_im,
					   plan->tw_re[s], plan->tw_im[s], n, s);

		/* Swap buffers */
		const float *tmp;
		tmp = src_re; src_re = dst_re; dst_re = (float *)tmp;
		tmp = src_im; src_im = dst_im; dst_im = (float *)tmp;
	}

	/* Final radix-2 stage if needed */
	if (plan->has_radix2_stage) {
		stockham_radix2_last_scalar(src_re, src_im, dst_re, dst_im, n);
		src_re = dst_re;
		src_im = dst_im;
	}

	/* Convert output SoA -> AoS */
	soa_to_aos(src_re, src_im, output, n);
}

static void scalar_inverse(const sfft_plan_t *plan,
			   const sfft_complex_t *OPT_RESTRICT input,
			   sfft_complex_t *OPT_RESTRICT output)
{
	if (OPT_UNLIKELY(!plan || !input || !output))
		return;

	const size_t n = plan->n;
	const size_t log4n = plan->log4n;

	/* Convert input AoS -> SoA with conjugation */
	OPT_PRAGMA_VECTORIZE
	for (size_t i = 0; i < n; i++) {
		plan->work_re[i] = input[i].re;
		plan->work_im[i] = -input[i].im;
	}

	/* Ping-pong between work buffers */
	const float *src_re = plan->work_re;
	const float *src_im = plan->work_im;
	float *dst_re = plan->work2_re;
	float *dst_im = plan->work2_im;

	/* Radix-4 stages with OTF twiddle computation */
	for (size_t s = 0; s < log4n; s++) {
		stockham_radix4_otf_scalar(src_re, src_im, dst_re, dst_im,
					   plan->tw_re[s], plan->tw_im[s], n, s);

		const float *tmp;
		tmp = src_re; src_re = dst_re; dst_re = (float *)tmp;
		tmp = src_im; src_im = dst_im; dst_im = (float *)tmp;
	}

	/* Final radix-2 stage if needed */
	if (plan->has_radix2_stage) {
		stockham_radix2_last_scalar(src_re, src_im, dst_re, dst_im, n);
		src_re = dst_re;
		src_im = dst_im;
	}

	/* Convert output SoA -> AoS with conjugation */
	OPT_PRAGMA_VECTORIZE
	for (size_t i = 0; i < n; i++) {
		output[i].re = src_re[i];
		output[i].im = -src_im[i];
	}
}

static void scalar_forward_interleaved(const sfft_plan_t *plan,
				       const float *OPT_RESTRICT input,
				       sfft_complex_t *OPT_RESTRICT output)
{
	if (OPT_UNLIKELY(!plan || !input || !output))
		return;

	scalar_forward(plan, (const sfft_complex_t *)input, output);
}

/* ============================================================================
 * Real-to-Complex (R2C) and Complex-to-Real (C2R) Transforms
 *
 * R2C Forward: N reals -> N/2+1 complex
 *   1. Pack N reals into N/2 complex: z[k] = x[2k] + j*x[2k+1]
 *   2. Perform N/2-point complex FFT -> Z[k]
 *   3. Post-process: X[k] = 0.5 * (Z[k] + conj(Z[N/2-k])) -
 *                    0.5j * W^k * (Z[k] - conj(Z[N/2-k]))
 *
 * C2R Inverse: N/2+1 complex -> N reals
 *   1. Pre-process: Form Z[k] from X[k] and X[N-k]
 *   2. Perform N/2-point inverse complex FFT -> z[k]
 *   3. Unpack: x[2k] = z[k].re, x[2k+1] = z[k].im
 * ========================================================================= */

static void scalar_forward_r2c(const sfft_plan_t *plan,
			       const float *OPT_RESTRICT input,
			       sfft_complex_t *OPT_RESTRICT output)
{
	if (OPT_UNLIKELY(!plan || !input || !output))
		return;

	const size_t n = plan->n;
	const size_t half_n = n >> 1;

	/*
	 * Step 1: Pack N reals into N/2 complex (SoA format)
	 * z[k] = x[2k] + j * x[2k+1]
	 */
	float *z_re = plan->work_re;
	float *z_im = plan->work_im;

	OPT_PRAGMA_VECTORIZE
	for (size_t k = 0; k < half_n; k++) {
		z_re[k] = input[2 * k];
		z_im[k] = input[2 * k + 1];
	}

	/*
	 * Step 2: Perform N/2-point complex FFT using precomputed twiddles
	 * Reuse the optimized radix-4 kernel
	 */
	const float *src_re = z_re;
	const float *src_im = z_im;
	float *dst_re = plan->work2_re;
	float *dst_im = plan->work2_im;

	/* Radix-4 stages using optimized kernel with half_n twiddles */
	for (size_t s = 0; s < plan->half_log4n; s++) {
		stockham_radix4_otf_scalar(src_re, src_im, dst_re, dst_im,
					   plan->half_tw_re[s], plan->half_tw_im[s],
					   half_n, s);

		/* Swap buffers */
		const float *tmp;
		tmp = src_re; src_re = dst_re; dst_re = (float *)tmp;
		tmp = src_im; src_im = dst_im; dst_im = (float *)tmp;
	}

	/* Final radix-2 stage if needed */
	if (plan->half_has_radix2_stage) {
		stockham_radix2_last_scalar(src_re, src_im, dst_re, dst_im, half_n);
		src_re = dst_re;
		src_im = dst_im;
	}

	/* Now src_re/src_im contain Z[k], the N/2-point FFT result */

	/*
	 * Step 3: Post-process to get N-point R2C result using precomputed twiddles
	 *
	 * For k = 0..N/2:
	 *   X[k] = 0.5 * (Z[k] + conj(Z[N/2-k])) -
	 *          0.5j * W_N^k * (Z[k] - conj(Z[N/2-k]))
	 *
	 * where W_N^k = e^(-j*2*pi*k/N) = r2c_tw_re[k] + j*r2c_tw_im[k]
	 *
	 * Special cases:
	 *   X[0] = Z[0].re + Z[0].im (DC)
	 *   X[N/2] = Z[0].re - Z[0].im (Nyquist)
	 */

	/* DC bin */
	output[0].re = src_re[0] + src_im[0];
	output[0].im = 0.0f;

	/* Nyquist bin */
	output[half_n].re = src_re[0] - src_im[0];
	output[half_n].im = 0.0f;

	/* Remaining bins using precomputed twiddles */
	const float *r2c_tw_re = plan->r2c_tw_re;
	const float *r2c_tw_im = plan->r2c_tw_im;

	OPT_PRAGMA_VECTORIZE
	for (size_t k = 1; k < half_n; k++) {
		const size_t conj_k = half_n - k;

		/* Z[k] and Z[N/2-k] */
		const float zk_re = src_re[k];
		const float zk_im = src_im[k];
		const float zc_re = src_re[conj_k];
		const float zc_im = -src_im[conj_k]; /* conj */

		/* A = 0.5 * (Z[k] + conj(Z[N/2-k])) */
		const float a_re = 0.5f * (zk_re + zc_re);
		const float a_im = 0.5f * (zk_im + zc_im);

		/* B = 0.5 * (Z[k] - conj(Z[N/2-k])) */
		const float b_re = 0.5f * (zk_re - zc_re);
		const float b_im = 0.5f * (zk_im - zc_im);

		/* W_N^k from precomputed table */
		const float w_re = r2c_tw_re[k];
		const float w_im = r2c_tw_im[k];

		/* -j * W * B = -j * (w_re + j*w_im) * (b_re + j*b_im) */
		/* = -j * (w_re*b_re - w_im*b_im + j*(w_re*b_im + w_im*b_re)) */
		/* = (w_re*b_im + w_im*b_re) - j*(w_re*b_re - w_im*b_im) */
		const float jw_b_re = w_re * b_im + w_im * b_re;
		const float jw_b_im = -(w_re * b_re - w_im * b_im);

		/* X[k] = A - j*W*B */
		output[k].re = a_re + jw_b_re;
		output[k].im = a_im + jw_b_im;
	}
}

static void scalar_inverse_c2r(const sfft_plan_t *plan,
			       const sfft_complex_t *OPT_RESTRICT input,
			       float *OPT_RESTRICT output)
{
	if (OPT_UNLIKELY(!plan || !input || !output))
		return;

	const size_t n = plan->n;
	const size_t half_n = n >> 1;

	/*
	 * Step 1: Pre-process to form Z[k] from X[k] using precomputed twiddles
	 *
	 * Z[k] = A[k] + W_N^{-k} * B[k]
	 *
	 * where:
	 *   A[k] = 0.5 * (X[k] + conj(X[N/2-k]))
	 *   B[k] = j * 0.5 * (X[k] - conj(X[N/2-k]))
	 *   W_N^{-k} = conj(W_N^k) = r2c_tw_re[k] - j*r2c_tw_im[k]
	 *
	 * Special cases:
	 *   Z[0].re = 0.5 * (X[0].re + X[N/2].re)
	 *   Z[0].im = 0.5 * (X[0].re - X[N/2].re)
	 */

	float *z_re = plan->work_re;
	float *z_im = plan->work_im;

	/* DC and Nyquist combined */
	z_re[0] = 0.5f * (input[0].re + input[half_n].re);
	z_im[0] = 0.5f * (input[0].re - input[half_n].re);

	/* Remaining bins using precomputed twiddles */
	const float *r2c_tw_re = plan->r2c_tw_re;
	const float *r2c_tw_im = plan->r2c_tw_im;

	OPT_PRAGMA_VECTORIZE
	for (size_t k = 1; k < half_n; k++) {
		const size_t conj_k = half_n - k;

		const float xk_re = input[k].re;
		const float xk_im = input[k].im;
		const float xc_re = input[conj_k].re;
		const float xc_im = -input[conj_k].im; /* conj */

		/* A = 0.5 * (X[k] + conj(X[N/2-k])) */
		const float a_re = 0.5f * (xk_re + xc_re);
		const float a_im = 0.5f * (xk_im + xc_im);

		/* B = 0.5 * (X[k] - conj(X[N/2-k])) */
		const float b_re = 0.5f * (xk_re - xc_re);
		const float b_im = 0.5f * (xk_im - xc_im);

		/* W_N^{-k} = conj(W_N^k) from precomputed table */
		const float w_re = r2c_tw_re[k];
		const float w_im = -r2c_tw_im[k]; /* conjugate for inverse */

		/*
		 * Compute j * W^{-k} * B (NOT W^{-k} * jB, which is different!)
		 *
		 * First: W^{-k} * B = (w_re - j*w_im) * (b_re + j*b_im)
		 *                   = (w_re*b_re + w_im*b_im) + j*(w_re*b_im - w_im*b_re)
		 * Using code variables where w_im = -original_w_im:
		 *                   = (w_re*b_re - w_im*b_im) + j*(w_re*b_im + w_im*b_re)
		 *
		 * Then: j * (W^{-k} * B) = -Im(...) + j*Re(...)
		 */
		const float wB_re = w_re * b_re - w_im * b_im;
		const float wB_im = w_re * b_im + w_im * b_re;
		const float jwB_re = -wB_im;
		const float jwB_im = wB_re;

		/* Z[k] = A + j * W^{-k} * B */
		z_re[k] = a_re + jwB_re;
		z_im[k] = a_im + jwB_im;
	}

	/*
	 * Step 2: Perform N/2-point inverse complex FFT on Z
	 * (conjugate -> forward FFT -> conjugate)
	 */

	/* Conjugate Z */
	OPT_PRAGMA_VECTORIZE
	for (size_t k = 0; k < half_n; k++) {
		z_im[k] = -z_im[k];
	}

	/*
	 * Run N/2-point FFT using optimized kernel with precomputed twiddles
	 * (Same as forward R2C but starting with Z data)
	 */
	const float *src_re = z_re;
	const float *src_im = z_im;
	float *dst_re = plan->work2_re;
	float *dst_im = plan->work2_im;

	/* Radix-4 stages using optimized kernel */
	for (size_t s = 0; s < plan->half_log4n; s++) {
		stockham_radix4_otf_scalar(src_re, src_im, dst_re, dst_im,
					   plan->half_tw_re[s], plan->half_tw_im[s],
					   half_n, s);

		/* Swap buffers */
		const float *tmp;
		tmp = src_re; src_re = dst_re; dst_re = (float *)tmp;
		tmp = src_im; src_im = dst_im; dst_im = (float *)tmp;
	}

	/* Final radix-2 stage if needed */
	if (plan->half_has_radix2_stage) {
		stockham_radix2_last_scalar(src_re, src_im, dst_re, dst_im, half_n);
		src_re = dst_re;
		src_im = dst_im;
	}

	/*
	 * Step 3: Conjugate and unpack to get real output
	 *
	 * The conj->FFT->conj approach gives (N/2)*IFFT instead of N*IFFT,
	 * so we multiply by 2 to match standard unnormalized IFFT scaling.
	 *
	 * x[2k] = 2 * z[k].re, x[2k+1] = 2 * z[k].im (after negating imaginary)
	 */
	OPT_PRAGMA_VECTORIZE
	for (size_t k = 0; k < half_n; k++) {
		output[2 * k] = 2.0f * src_re[k];
		output[2 * k + 1] = -2.0f * src_im[k]; /* Conjugate and scale */
	}
}

/* ============================================================================
 * Split-Format FFT (No AoS↔SoA Conversion)
 *
 * These functions work directly with separate real/imag arrays,
 * avoiding conversion overhead. This is PFFFT's approach.
 * ========================================================================= */

void scalar_forward_split(const sfft_plan_t *plan,
				 const float *OPT_RESTRICT in_re,
				 const float *OPT_RESTRICT in_im,
				 float *OPT_RESTRICT out_re,
				 float *OPT_RESTRICT out_im)
{
	if (OPT_UNLIKELY(!plan || !in_re || !in_im || !out_re || !out_im))
		return;

	const size_t n = plan->n;
	const size_t log4n = plan->log4n;
	const size_t total_stages = log4n + (plan->has_radix2_stage ? 1 : 0);

	/*
	 * N=2 special case: only a radix-2 stage, no radix-4 stages.
	 * Read directly from input, write to output.
	 */
	if (OPT_UNLIKELY(log4n == 0)) {
		if (plan->has_radix2_stage)
			stockham_radix2_last_scalar(in_re, in_im,
						    out_re, out_im, n);
		return;
	}

	/*
	 * Use output buffer as one of the ping-pong buffers.
	 * Arrange so final result lands in output (no final copy needed).
	 */
	float *buf0_re, *buf0_im;	/* First destination */
	float *buf1_re, *buf1_im;	/* Second destination */

	if (total_stages & 1) {
		/* Odd stages: result in first destination = output */
		buf0_re = out_re; buf0_im = out_im;
		buf1_re = plan->work_re; buf1_im = plan->work_im;
	} else {
		/* Even stages: result in second destination = output */
		buf0_re = plan->work_re; buf0_im = plan->work_im;
		buf1_re = out_re; buf1_im = out_im;
	}

	/* Stage 0: read directly from input, write to buf0 */
	stockham_radix4_otf_scalar(in_re, in_im, buf0_re, buf0_im,
				   plan->tw_re[0], plan->tw_im[0], n, 0);

	/* Remaining radix-4 stages: ping-pong between buf0 and buf1 */
	const float *src_re = buf0_re, *src_im = buf0_im;
	float *dst_re = buf1_re, *dst_im = buf1_im;

	for (size_t s = 1; s < log4n; s++) {
		stockham_radix4_otf_scalar(src_re, src_im, dst_re, dst_im,
					   plan->tw_re[s], plan->tw_im[s], n, s);
		const float *tmp;
		tmp = src_re; src_re = dst_re; dst_re = (float *)tmp;
		tmp = src_im; src_im = dst_im; dst_im = (float *)tmp;
	}

	/* Final radix-2 stage if needed */
	if (plan->has_radix2_stage) {
		stockham_radix2_last_scalar(src_re, src_im, dst_re, dst_im, n);
	}
	/* Result is now in output buffer - no copy needed! */
}

void scalar_inverse_split(const sfft_plan_t *plan,
				 const float *OPT_RESTRICT in_re,
				 const float *OPT_RESTRICT in_im,
				 float *OPT_RESTRICT out_re,
				 float *OPT_RESTRICT out_im)
{
	if (OPT_UNLIKELY(!plan || !in_re || !in_im || !out_re || !out_im))
		return;

	const size_t n = plan->n;
	const size_t log4n = plan->log4n;
	const size_t total_stages = log4n + (plan->has_radix2_stage ? 1 : 0);

	/*
	 * N=2 special case: only a radix-2 stage, no radix-4 stages.
	 * Conjugate input into work, radix-2 into output, conjugate output.
	 */
	if (OPT_UNLIKELY(log4n == 0)) {
		if (plan->has_radix2_stage) {
			OPT_PRAGMA_VECTORIZE
			for (size_t i = 0; i < n; i++) {
				plan->work_re[i] = in_re[i];
				plan->work_im[i] = -in_im[i];
			}
			stockham_radix2_last_scalar(plan->work_re, plan->work_im,
						    out_re, out_im, n);
			OPT_PRAGMA_VECTORIZE
			for (size_t i = 0; i < n; i++)
				out_im[i] = -out_im[i];
		}
		return;
	}

	/*
	 * Inverse FFT: conj(FFT(conj(input)))
	 * Use output as one ping-pong buffer, conjugate in-place at end.
	 */
	float *conj_buf_re, *conj_buf_im;  /* Buffer for conjugated input */
	float *other_re, *other_im;	   /* Other ping-pong buffer */

	if (total_stages & 1) {
		/* Odd: result in original_dst = output */
		conj_buf_re = plan->work_re; conj_buf_im = plan->work_im;
		other_re = out_re; other_im = out_im;
	} else {
		/* Even: result in original_src = output */
		conj_buf_re = out_re; conj_buf_im = out_im;
		other_re = plan->work_re; other_im = plan->work_im;
	}

	/* Conjugate input into conj_buf */
	OPT_PRAGMA_VECTORIZE
	for (size_t i = 0; i < n; i++) {
		conj_buf_re[i] = in_re[i];
		conj_buf_im[i] = -in_im[i];
	}

	/* Stage 0: read from conj_buf, write to other */
	stockham_radix4_otf_scalar(conj_buf_re, conj_buf_im,
				   other_re, other_im,
				   plan->tw_re[0], plan->tw_im[0], n, 0);

	/* Remaining radix-4 stages: ping-pong */
	const float *src_re = other_re, *src_im = other_im;
	float *dst_re = conj_buf_re, *dst_im = conj_buf_im;

	for (size_t s = 1; s < log4n; s++) {
		stockham_radix4_otf_scalar(src_re, src_im, dst_re, dst_im,
					   plan->tw_re[s], plan->tw_im[s], n, s);
		const float *tmp;
		tmp = src_re; src_re = dst_re; dst_re = (float *)tmp;
		tmp = src_im; src_im = dst_im; dst_im = (float *)tmp;
	}

	/* Final radix-2 stage if needed */
	if (plan->has_radix2_stage) {
		stockham_radix2_last_scalar(src_re, src_im, dst_re, dst_im, n);
	}

	/* Result is in output buffer, conjugate in-place */
	OPT_PRAGMA_VECTORIZE
	for (size_t i = 0; i < n; i++) {
		out_im[i] = -out_im[i];
	}
}

/* ============================================================================
 * Backend VTable Export
 * ========================================================================= */

const sfft_backend_vtable_t sfft_backend_scalar = {
	.id = SFFT_BACKEND_SCALAR,
	.name = "Scalar Stockham Radix-4",

	.plan_create = scalar_plan_create,
	.plan_destroy = scalar_plan_destroy,
	.plan_size = scalar_plan_size,

	.forward = scalar_forward,
	.inverse = scalar_inverse,

	.forward_split = scalar_forward_split,
	.inverse_split = scalar_inverse_split,

	.aligned_alloc = scalar_aligned_alloc,
	.aligned_free = scalar_aligned_free,
};
