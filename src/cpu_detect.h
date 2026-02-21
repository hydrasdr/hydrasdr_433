/** @file
    Runtime CPU ISA level detection.

    Header-only, static inline. Detects the best available SIMD ISA:
      - x86-64: SSE2/AVX2/AVX-512 via CPUID
      - AArch64: NEON (mandatory) / SVE (via getauxval on Linux)
      - ARMv7: NEON (compile-time detection)
      - Other: baseline

    Supports GCC/Clang (__builtin_cpu_supports) and MSVC (__cpuid).

    Copyright (C) 2026 Benjamin Vernoux <bvernoux@hydrasdr.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef CPU_DETECT_H_
#define CPU_DETECT_H_

enum cpu_isa_level {
    CPU_ISA_BASELINE = 0,  /* Portable baseline (SSE2 on x86-64, scalar elsewhere) */
    CPU_ISA_AVX2     = 1,  /* x86: AVX2 + FMA */
    CPU_ISA_AVX512   = 2,  /* x86: AVX-512F + VL + FMA */
    CPU_ISA_NEON     = 3,  /* ARM: NEON (mandatory on AArch64) */
    CPU_ISA_SVE      = 4,  /* ARM: SVE (optional on AArch64) */
};

/* x86/x86-64 detection */
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)

#if defined(__GNUC__) || defined(__clang__)

/*
 * GCC/Clang: __builtin_cpu_supports() handles CPUID + XCR0 OS checks
 * internally. Must call __builtin_cpu_init() first (idempotent).
 */
static inline enum cpu_isa_level cpu_detect_isa(void)
{
    __builtin_cpu_init();

    if (__builtin_cpu_supports("avx512f") &&
        __builtin_cpu_supports("avx512vl") &&
        __builtin_cpu_supports("fma"))
        return CPU_ISA_AVX512;

    if (__builtin_cpu_supports("avx2") &&
        __builtin_cpu_supports("fma"))
        return CPU_ISA_AVX2;

    return CPU_ISA_BASELINE;
}

#elif defined(_MSC_VER)

#include <intrin.h>

/*
 * MSVC: manual CPUID + XGETBV checks.
 * XCR0 bits: 1=SSE state, 2=YMM state, 5=opmask, 6=ZMM_Hi256, 7=Hi16_ZMM
 */
static inline enum cpu_isa_level cpu_detect_isa(void)
{
    int info[4];
    unsigned long long xcr0;

    /* Check basic CPUID support */
    __cpuid(info, 0);
    if (info[0] < 7)
        return CPU_ISA_BASELINE;

    /* ECX from CPUID(1): bit 12=FMA, bit 27=OSXSAVE, bit 28=AVX */
    __cpuid(info, 1);
    if (!(info[2] & (1 << 27)))  /* OSXSAVE not set */
        return CPU_ISA_BASELINE;

    xcr0 = _xgetbv(0);

    /* Check OS saved YMM state (bits 1:2) */
    if ((xcr0 & 0x06) != 0x06)
        return CPU_ISA_BASELINE;

    int has_fma = (info[2] >> 12) & 1;

    /* CPUID(7,0): EBX bit 5=AVX2, bit 16=AVX512F; ECX bit 6=AVX512VL */
    __cpuidex(info, 7, 0);
    int has_avx2 = (info[1] >> 5) & 1;
    int has_avx512f = (info[1] >> 16) & 1;
    int has_avx512vl = (info[1] >> 31) & 1;

    /* AVX-512: need OS support for opmask+ZMM (XCR0 bits 5:7) */
    if (has_avx512f && has_avx512vl && has_fma &&
        (xcr0 & 0xE0) == 0xE0)
        return CPU_ISA_AVX512;

    if (has_avx2 && has_fma)
        return CPU_ISA_AVX2;

    return CPU_ISA_BASELINE;
}

#else
/* Unknown x86 compiler */
static inline enum cpu_isa_level cpu_detect_isa(void)
{
    return CPU_ISA_BASELINE;
}
#endif

#elif defined(__aarch64__) || defined(_M_ARM64)
/* AArch64: NEON is mandatory, SVE is optional.
 * Runtime SVE detection needed for portable binaries — cpu_detect.h
 * is included by channelizer.c which is NOT compiled with SVE flags,
 * so __ARM_FEATURE_SVE is not set here. */

#if defined(__linux__)
#include <sys/auxv.h>
#ifndef HWCAP_SVE
#define HWCAP_SVE (1 << 22)
#endif
#endif

static inline enum cpu_isa_level cpu_detect_isa(void)
{
#if defined(__linux__)
    unsigned long hwcaps = getauxval(AT_HWCAP);
    if (hwcaps & HWCAP_SVE)
        return CPU_ISA_SVE;
#endif
    /* NEON is mandatory on AArch64 — always available */
    return CPU_ISA_NEON;
}

#elif defined(__arm__) || defined(_M_ARM)
/* ARMv7 (32-bit): NEON is optional */
static inline enum cpu_isa_level cpu_detect_isa(void)
{
#if defined(__ARM_NEON)
    return CPU_ISA_NEON;
#else
    return CPU_ISA_BASELINE;
#endif
}

#else
/* Non-x86, non-ARM architecture (RISC-V, etc.): use baseline */
static inline enum cpu_isa_level cpu_detect_isa(void)
{
    return CPU_ISA_BASELINE;
}
#endif

#endif /* CPU_DETECT_H_ */
