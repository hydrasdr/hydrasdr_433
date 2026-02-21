/*
 * hydrasdr-lfft - Light FFT Library for Small Sizes
 *
 * Simplified implementation for small FFT sizes (2-32 points)
 * used in PFB channelizers.
 *
 * License: MIT
 * Copyright (c) 2025-2026, Benjamin Vernoux <bvernoux@hydrasdr.com>
 */

#include "hydrasdr_lfft.h"
#include "stockham_internal.h"
#include "build_info.h"

#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Global State
 * ========================================================================= */

static int g_initialized = 0;

/* ============================================================================
 * FFT Plan Structure
 * ========================================================================= */

struct hlfft_plan {
	size_t n;		/* FFT size */
	sfft_plan_t *stockham;	/* Stockham FFT plan */
};

/* ============================================================================
 * Library Initialization
 * ========================================================================= */

int hlfft_init(void)
{
	if (g_initialized)
		return HLFFT_OK;

	g_initialized = 1;
	return HLFFT_OK;
}

void hlfft_shutdown(void)
{
	g_initialized = 0;
}

/* ============================================================================
 * FFT Plan Management
 * ========================================================================= */

hlfft_plan_t *hlfft_plan_create(size_t n, const void *config)
{
	hlfft_plan_t *plan;
	(void)config;  /* Unused */

	if (!hlfft_size_valid(n))
		return NULL;

	if (!g_initialized)
		hlfft_init();

	plan = (hlfft_plan_t *)calloc(1, sizeof(hlfft_plan_t));
	if (!plan)
		return NULL;

	plan->n = n;
	plan->stockham = sfft_backend_scalar.plan_create(n);

	if (!plan->stockham) {
		free(plan);
		return NULL;
	}

	return plan;
}

void hlfft_plan_destroy(hlfft_plan_t *plan)
{
	if (!plan)
		return;

	if (plan->stockham)
		sfft_backend_scalar.plan_destroy(plan->stockham);

	free(plan);
}

size_t hlfft_plan_size(const hlfft_plan_t *plan)
{
	return plan ? plan->n : 0;
}

/* ============================================================================
 * FFT Execution
 * ========================================================================= */

int hlfft_forward(const hlfft_plan_t *plan,
		  const hlfft_complex_t *input,
		  hlfft_complex_t *output)
{
	if (!plan || !input || !output)
		return HLFFT_ERROR_INVALID_ARG;

	/* sfft_complex_t has same layout as hlfft_complex_t */
	sfft_backend_scalar.forward(plan->stockham,
				    (const sfft_complex_t *)input,
				    (sfft_complex_t *)output);

	return HLFFT_OK;
}

int hlfft_inverse(const hlfft_plan_t *plan,
		  const hlfft_complex_t *input,
		  hlfft_complex_t *output)
{
	if (!plan || !input || !output)
		return HLFFT_ERROR_INVALID_ARG;

	sfft_backend_scalar.inverse(plan->stockham,
				    (const sfft_complex_t *)input,
				    (sfft_complex_t *)output);

	return HLFFT_OK;
}

int hlfft_forward_soa(const hlfft_plan_t *plan,
		      const float *in_re, const float *in_im,
		      float *out_re, float *out_im)
{
	if (!plan || !in_re || !in_im || !out_re || !out_im)
		return HLFFT_ERROR_INVALID_ARG;

	sfft_backend_scalar.forward_split(plan->stockham,
					  in_re, in_im, out_re, out_im);

	return HLFFT_OK;
}

/* ============================================================================
 * Memory Allocation
 * ========================================================================= */

void *hlfft_aligned_alloc(size_t size)
{
	return sfft_backend_scalar.aligned_alloc(size);
}

void hlfft_aligned_free(void *ptr)
{
	sfft_backend_scalar.aligned_free(ptr);
}

/* ============================================================================
 * Utility Functions
 * ========================================================================= */

int hlfft_size_valid(size_t n)
{
	/* Power of 2 check and minimum size */
	return n >= HLFFT_MIN_SIZE && (n & (n - 1)) == 0;
}

const char *hlfft_error_string(int error)
{
	switch (error) {
	case HLFFT_OK:                  return "Success";
	case HLFFT_ERROR_INVALID_ARG:   return "Invalid argument";
	case HLFFT_ERROR_INVALID_SIZE:  return "Invalid FFT size";
	case HLFFT_ERROR_NO_MEMORY:     return "Memory allocation failed";
	case HLFFT_ERROR_NOT_IMPL:      return "Feature not implemented";
	default:                        return "Unknown error";
	}
}

const char *hlfft_version(void)
{
	return HYDRASDR_LFFT_VERSION_STRING;
}

const char *hlfft_build_info(void)
{
	return "FFT: " BUILD_INFO_STR;
}
