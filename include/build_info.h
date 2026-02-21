/** @file
    Build information macros for compile-time detection.

    Provides BUILD_INFO_STR macro that expands to a string literal
    describing build type, compiler, and SIMD capabilities.

    SIMD detection reflects the compilation flags of the translation
    unit that includes this header.  Hot-path files compiled with
    -march=native will report the full ISA (e.g. AVX-512 FMA),
    while files compiled without DSP flags report the baseline.

    Copyright (C) 2026 Benjamin Vernoux <bvernoux@hydrasdr.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_BUILD_INFO_H_
#define INCLUDE_BUILD_INFO_H_

/* Build type */
#ifdef NDEBUG
#define BUILD_TYPE_STR "Release"
#else
#define BUILD_TYPE_STR "Debug"
#endif

/* Compiler identification */
#if defined(__clang__)
#define BUILD_COMPILER_STR "Clang " __clang_version__
#elif defined(__GNUC__)
#define BUILD_COMPILER_STR "GCC " __VERSION__
#elif defined(_MSC_VER)
#define BUILD_CC_STR_(x) #x
#define BUILD_CC_STR(x) BUILD_CC_STR_(x)
/* _MSC_VER format: MMmm (1944 = VS2022 17.14, 1950 = VS2026) */
#define BUILD_COMPILER_STR "MSVC " BUILD_CC_STR(_MSC_VER)
#else
#define BUILD_COMPILER_STR "unknown"
#endif

/* SIMD capabilities (reflects the including file's compilation flags) */
#if defined(__AVX512F__)
#define BUILD_SIMD_STR " AVX-512"
#elif defined(__AVX2__)
#define BUILD_SIMD_STR " AVX2"
#elif defined(__AVX__)
#define BUILD_SIMD_STR " AVX"
#elif defined(__SSE4_2__)
#define BUILD_SIMD_STR " SSE4.2"
#elif defined(__SSE2__) || defined(_M_X64)
#define BUILD_SIMD_STR " SSE2"
#elif defined(__ARM_FEATURE_SVE)
#define BUILD_SIMD_STR " SVE"
#elif defined(__ARM_NEON)
#define BUILD_SIMD_STR " NEON"
#else
#define BUILD_SIMD_STR ""
#endif

#ifdef __FMA__
#define BUILD_FMA_STR " FMA"
#else
#define BUILD_FMA_STR ""
#endif

#ifdef __FAST_MATH__
#define BUILD_FMATH_STR " fast-math"
#else
#define BUILD_FMATH_STR ""
#endif

/** Complete build info string (build type + compiler + SIMD + math flags) */
#define BUILD_INFO_STR \
    BUILD_TYPE_STR " " BUILD_COMPILER_STR \
    BUILD_SIMD_STR BUILD_FMA_STR BUILD_FMATH_STR

#endif /* INCLUDE_BUILD_INFO_H_ */
