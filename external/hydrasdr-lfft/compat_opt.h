/*
 * Portable Optimization Compatibility Header (Simplified)
 *
 * Cross-platform macros for performance optimization hints.
 * Simplified version for scalar FFT backend only.
 *
 * License: MIT
 * Copyright (c) 2025-2026, Benjamin Vernoux <bvernoux@hydrasdr.com>
 */

#ifndef COMPAT_OPT_H
#define COMPAT_OPT_H

#include <stddef.h>
#include <stdlib.h>

/* ========================================================================
 * COMPILER DETECTION
 * ======================================================================== */

#if defined(_MSC_VER)
	#define COMPILER_MSVC 1
#elif defined(__clang__)
	#define COMPILER_CLANG 1
#elif defined(__GNUC__)
	#define COMPILER_GCC 1
#endif

/* ========================================================================
 * RESTRICT KEYWORD (Pointer Aliasing Hint)
 * ======================================================================== */

#if defined(COMPILER_MSVC)
	#define OPT_RESTRICT __restrict
#elif defined(COMPILER_GCC) || defined(COMPILER_CLANG)
	#define OPT_RESTRICT __restrict__
#else
	#define OPT_RESTRICT
#endif

/* ========================================================================
 * INLINE HINTS
 * ======================================================================== */

#if defined(COMPILER_MSVC)
	#define OPT_INLINE __forceinline
#elif defined(COMPILER_GCC) || defined(COMPILER_CLANG)
	#define OPT_INLINE inline __attribute__((always_inline))
#else
	#define OPT_INLINE inline
#endif

/* ========================================================================
 * BRANCH PREDICTION HINTS
 * ======================================================================== */

#if defined(COMPILER_GCC) || defined(COMPILER_CLANG)
	#define OPT_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
	#define OPT_UNLIKELY(x) (x)
#endif

/* ========================================================================
 * HOT FUNCTION ATTRIBUTE
 * ======================================================================== */

#if defined(COMPILER_GCC) || defined(COMPILER_CLANG)
	#define OPT_HOT __attribute__((hot))
#else
	#define OPT_HOT
#endif

/* ========================================================================
 * SOFTWARE PREFETCH
 * ======================================================================== */

#if defined(COMPILER_GCC) || defined(COMPILER_CLANG)
	#define OPT_PREFETCH_READ(addr)  __builtin_prefetch((const void *)(addr), 0, 3)
	#define OPT_PREFETCH_WRITE(addr) __builtin_prefetch((const void *)(addr), 1, 3)
#elif defined(COMPILER_MSVC)
	#include <intrin.h>
	#define OPT_PREFETCH_READ(addr)  _mm_prefetch((const char *)(addr), _MM_HINT_T0)
	#define OPT_PREFETCH_WRITE(addr) _mm_prefetch((const char *)(addr), _MM_HINT_T0)
#else
	#define OPT_PREFETCH_READ(addr)  ((void)0)
	#define OPT_PREFETCH_WRITE(addr) ((void)0)
#endif

/* ========================================================================
 * ALIGNMENT HINTS
 * ======================================================================== */

#if defined(COMPILER_GCC) || defined(COMPILER_CLANG)
	#define OPT_ASSUME_ALIGNED(ptr, alignment) \
		__builtin_assume_aligned((ptr), (alignment))
#elif defined(COMPILER_MSVC)
	#define OPT_ASSUME_ALIGNED(ptr, alignment) \
		(__assume(((uintptr_t)(ptr) & ((alignment) - 1)) == 0), (ptr))
#else
	#define OPT_ASSUME_ALIGNED(ptr, alignment) (ptr)
#endif

/* ========================================================================
 * VECTORIZATION PRAGMAS
 * ======================================================================== */

#if defined(COMPILER_CLANG)
	#define OPT_PRAGMA_VECTORIZE \
		_Pragma("clang loop vectorize(enable) interleave(enable)")
#elif defined(COMPILER_GCC)
	#define OPT_PRAGMA_VECTORIZE \
		_Pragma("GCC ivdep")
#elif defined(COMPILER_MSVC)
	#define OPT_PRAGMA_VECTORIZE \
		__pragma(loop(ivdep))
#else
	#define OPT_PRAGMA_VECTORIZE
#endif

/* ========================================================================
 * FUSED MULTIPLY-ADD
 * ======================================================================== */

#if defined(COMPILER_GCC) || defined(COMPILER_CLANG)
	#define OPT_FMA_F32(a, b, c) __builtin_fmaf((a), (b), (c))
#elif defined(COMPILER_MSVC)
	#include <math.h>
	#define OPT_FMA_F32(a, b, c) fmaf((a), (b), (c))
#else
	#define OPT_FMA_F32(a, b, c) ((a) * (b) + (c))
#endif

/* ========================================================================
 * ALIGNED MEMORY ALLOCATION
 * ======================================================================== */

static OPT_INLINE void *opt_aligned_alloc(size_t size, size_t alignment)
{
	void *ptr = NULL;

	if (alignment < sizeof(void *))
		alignment = sizeof(void *);

#if defined(COMPILER_MSVC)
	ptr = _aligned_malloc(size, alignment);
#elif defined(__MINGW32__) || defined(__MINGW64__)
	ptr = __mingw_aligned_malloc(size, alignment);
#elif defined(_POSIX_VERSION) && _POSIX_VERSION >= 200112L
	if (posix_memalign(&ptr, alignment, size) != 0)
		ptr = NULL;
#else
	/* Fallback: manual alignment */
	void *raw = malloc(size + alignment + sizeof(void *));
	if (raw) {
		uintptr_t aligned = ((uintptr_t)raw + alignment + sizeof(void *)) & ~(alignment - 1);
		((void **)aligned)[-1] = raw;
		ptr = (void *)aligned;
	}
#endif

	return ptr;
}

static OPT_INLINE void opt_aligned_free(void *ptr)
{
	if (!ptr)
		return;

#if defined(COMPILER_MSVC)
	_aligned_free(ptr);
#elif defined(__MINGW32__) || defined(__MINGW64__)
	__mingw_aligned_free(ptr);
#elif defined(_POSIX_VERSION) && _POSIX_VERSION >= 200112L
	free(ptr);
#else
	free(((void **)ptr)[-1]);
#endif
}

#endif /* COMPAT_OPT_H */
