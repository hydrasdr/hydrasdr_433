/*
 * hydrasdr-lfft - Light FFT Library for Small Sizes
 *
 * Optimized for small FFT sizes (2-32 points) used in PFB channelizers.
 * Uses portable scalar Stockham algorithm with compiler auto-vectorization.
 *
 * Features:
 * - Stockham algorithm (cache-friendly, no bit-reversal)
 * - Radix-4 with radix-2 cleanup for any power-of-2 size
 * - On-the-fly twiddle factor computation (reduced memory)
 * - BSD/MIT license compatible (no GPL dependencies)
 *
 * License: MIT
 * Copyright (c) 2025-2026, Benjamin Vernoux <bvernoux@hydrasdr.com>
 */

#ifndef HYDRASDR_LFFT_H
#define HYDRASDR_LFFT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Version Information
 * ========================================================================= */

#define HYDRASDR_LFFT_VERSION_MAJOR 1
#define HYDRASDR_LFFT_VERSION_MINOR 0
#define HYDRASDR_LFFT_VERSION_PATCH 0
#define HYDRASDR_LFFT_VERSION_STRING "1.0.0"

/* Minimum supported FFT size (power of 2) */
#define HLFFT_MIN_SIZE 2

/* ============================================================================
 * Error Codes
 * ========================================================================= */

typedef enum {
	HLFFT_OK = 0,			/* Success */
	HLFFT_ERROR_INVALID_ARG = -1,	/* Invalid argument */
	HLFFT_ERROR_INVALID_SIZE = -2,	/* FFT size not supported */
	HLFFT_ERROR_NO_MEMORY = -3,	/* Memory allocation failed */
	HLFFT_ERROR_NOT_IMPL = -8,	/* Feature not implemented */
} hlfft_error_t;

/* ============================================================================
 * Complex Number Type
 * ========================================================================= */

typedef struct {
	float re;
	float im;
} hlfft_complex_t;

/* ============================================================================
 * FFT Plan (opaque handle)
 * ========================================================================= */

typedef struct hlfft_plan hlfft_plan_t;

/* ============================================================================
 * Library Initialization
 * ========================================================================= */

/**
 * Initialize hydrasdr-lfft library
 *
 * @return HLFFT_OK on success, error code on failure
 */
int hlfft_init(void);

/**
 * Shutdown hydrasdr-lfft library
 */
void hlfft_shutdown(void);

/* ============================================================================
 * FFT Plan Management
 * ========================================================================= */

/**
 * Create FFT plan for given size
 *
 * @param n       FFT size (must be power of 2, >= 2)
 * @param config  Configuration (ignored, pass NULL)
 * @return        FFT plan or NULL on error
 *
 * The plan pre-computes twiddle factors and allocates work buffers.
 */
hlfft_plan_t *hlfft_plan_create(size_t n, const void *config);

/**
 * Destroy FFT plan and free resources
 *
 * @param plan  FFT plan to destroy (can be NULL)
 */
void hlfft_plan_destroy(hlfft_plan_t *plan);

/**
 * Get FFT size from plan
 *
 * @param plan  FFT plan
 * @return FFT size (0 if plan is NULL)
 */
size_t hlfft_plan_size(const hlfft_plan_t *plan);

/* ============================================================================
 * FFT Execution
 * ========================================================================= */

/**
 * Execute forward FFT (time domain -> frequency domain)
 *
 * @param plan   FFT plan
 * @param input  Input samples (N complex values, interleaved I/Q)
 * @param output Output spectrum (N complex values)
 * @return HLFFT_OK on success
 *
 * Input format: Array of N hlfft_complex_t values in interleaved (AoS) format.
 * Memory layout: [re0, im0, re1, im1, ..., re(N-1), im(N-1)]
 *
 * Compatible with SDR I/Q data streams. For raw float* data in
 * interleaved format, cast directly: (hlfft_complex_t*)float_ptr
 */
int hlfft_forward(const hlfft_plan_t *plan,
		  const hlfft_complex_t *input,
		  hlfft_complex_t *output);

/**
 * Execute inverse FFT (frequency domain -> time domain)
 *
 * @param plan   FFT plan
 * @param input  Input spectrum (N complex values)
 * @param output Output samples (N complex values, interleaved I/Q)
 * @return HLFFT_OK on success
 *
 * Note: Output is NOT normalized (multiply by 1/N if needed)
 */
int hlfft_inverse(const hlfft_plan_t *plan,
		  const hlfft_complex_t *input,
		  hlfft_complex_t *output);

/**
 * Execute forward FFT with split real/imag arrays (SoA format)
 *
 * @param plan   FFT plan
 * @param in_re  Input real parts (N floats)
 * @param in_im  Input imaginary parts (N floats)
 * @param out_re Output real parts (N floats)
 * @param out_im Output imaginary parts (N floats)
 * @return HLFFT_OK on success
 *
 * Avoids AoS<->SoA conversion overhead when caller already has split format.
 * Uses zero-copy ping-pong: result lands directly in output buffers.
 */
int hlfft_forward_soa(const hlfft_plan_t *plan,
		      const float *in_re, const float *in_im,
		      float *out_re, float *out_im);

/* ============================================================================
 * Utility Functions
 * ========================================================================= */

/**
 * Check if size is valid for FFT
 *
 * @param n  FFT size to check
 * @return Non-zero if size is power of 2 and >= HLFFT_MIN_SIZE
 */
int hlfft_size_valid(size_t n);

/**
 * Get error message string
 *
 * @param error  Error code
 * @return Static string with error description
 */
const char *hlfft_error_string(int error);

/**
 * Get library version string
 *
 * @return Version string (e.g., "1.0.0")
 */
const char *hlfft_version(void);

/**
 * Get build info string (compiler, SIMD, optimization flags)
 *
 * @return Static string, e.g. "FFT: Release GCC 15.2.0 AVX-512 FMA fast-math"
 */
const char *hlfft_build_info(void);

/* ============================================================================
 * Memory Allocation Helpers
 * ========================================================================= */

/**
 * Allocate aligned memory for FFT buffers
 *
 * @param size  Size in bytes
 * @return      Aligned pointer (64-byte alignment)
 */
void *hlfft_aligned_alloc(size_t size);

/**
 * Free aligned memory
 */
void hlfft_aligned_free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif /* HYDRASDR_LFFT_H */
