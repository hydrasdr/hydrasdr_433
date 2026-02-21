/*
 * Stockham FFT - Internal Types and Declarations
 *
 * Internal header for hydrasdr-lfft scalar FFT implementation.
 * Not for external use - include hydrasdr_lfft.h instead.
 *
 * License: MIT
 * Copyright (c) 2025-2026, Benjamin Vernoux <bvernoux@hydrasdr.com>
 */

#ifndef STOCKHAM_INTERNAL_H
#define STOCKHAM_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Complex Number Type (internal)
 * ========================================================================= */

typedef struct {
	float re;
	float im;
} sfft_complex_t;

/* ============================================================================
 * FFT Plan (opaque)
 * ========================================================================= */

typedef struct sfft_plan sfft_plan_t;

/* ============================================================================
 * Backend Virtual Table (simplified - single scalar backend)
 * ========================================================================= */

typedef enum {
	SFFT_BACKEND_SCALAR = 0,
	SFFT_BACKEND_COUNT
} sfft_backend_id_t;

typedef struct sfft_backend_vtable {
	sfft_backend_id_t id;
	const char *name;

	sfft_plan_t *(*plan_create)(size_t n);
	void (*plan_destroy)(sfft_plan_t *plan);
	size_t (*plan_size)(const sfft_plan_t *plan);

	void (*forward)(const sfft_plan_t *plan,
			const sfft_complex_t *restrict input,
			sfft_complex_t *restrict output);
	void (*inverse)(const sfft_plan_t *plan,
			const sfft_complex_t *restrict input,
			sfft_complex_t *restrict output);

	void (*forward_split)(const sfft_plan_t *plan,
			      const float *restrict in_re,
			      const float *restrict in_im,
			      float *restrict out_re,
			      float *restrict out_im);
	void (*inverse_split)(const sfft_plan_t *plan,
			      const float *restrict in_re,
			      const float *restrict in_im,
			      float *restrict out_re,
			      float *restrict out_im);

	void *(*aligned_alloc)(size_t size);
	void (*aligned_free)(void *ptr);
} sfft_backend_vtable_t;

/* Scalar backend vtable */
extern const sfft_backend_vtable_t sfft_backend_scalar;

/* ============================================================================
 * Public API (internal names used by hydrasdr_lfft.c)
 * ========================================================================= */

sfft_plan_t *sfft_plan_create(size_t n);
void sfft_plan_destroy(sfft_plan_t *plan);
size_t sfft_plan_size(const sfft_plan_t *plan);

void sfft_forward(const sfft_plan_t *plan,
		  const sfft_complex_t *restrict input,
		  sfft_complex_t *restrict output);
void sfft_inverse(const sfft_plan_t *plan,
		  const sfft_complex_t *restrict input,
		  sfft_complex_t *restrict output);

void *sfft_aligned_alloc(size_t size);
void sfft_aligned_free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif /* STOCKHAM_INTERNAL_H */
