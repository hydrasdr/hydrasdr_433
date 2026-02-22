/** @file
    Specialized FFT kernels for PFB channelizer hot path.

    Fully-unrolled Cooley-Tukey DIT kernels for N=2,4,8,16 with
    compile-time constant twiddle factors. No loops, no buffers,
    no function call overhead when inlined. SoA (split real/imag)
    interface matches channelizer's internal format.

    These replace the generic hlfft_forward_soa() call for small
    fixed sizes, eliminating ~4 layers of dispatch overhead per FFT.

    Copyright (C) 2025-2026 Benjamin Vernoux <bvernoux@hydrasdr.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef FFT_KERNELS_H_
#define FFT_KERNELS_H_

#include "compat_opt.h"

/* ========================================================================
 * Compile-time twiddle constants
 * ======================================================================== */

#define FFT_SQRT2_2  0.70710678118654752f  /* cos(pi/4) = sin(pi/4) */
#define FFT_COS_PI8  0.92387953251128676f  /* cos(pi/8) */
#define FFT_SIN_PI8  0.38268343236508978f  /* sin(pi/8) */

/* ========================================================================
 * 2-point FFT (DFT butterfly)
 *
 * X[0] = x[0] + x[1]
 * X[1] = x[0] - x[1]
 *
 * 4 adds, 0 multiplies.
 * ======================================================================== */

static OPT_INLINE void fft2_forward_soa(const float *OPT_RESTRICT in_re,
                     const float *OPT_RESTRICT in_im,
                     float *OPT_RESTRICT out_re,
                     float *OPT_RESTRICT out_im)
{
    float a_re = in_re[0], a_im = in_im[0];
    float b_re = in_re[1], b_im = in_im[1];

    out_re[0] = a_re + b_re;
    out_im[0] = a_im + b_im;
    out_re[1] = a_re - b_re;
    out_im[1] = a_im - b_im;
}

/* ========================================================================
 * 4-point FFT (Cooley-Tukey DIT radix-2)
 *
 * Stage 1: Two 2-pt DFTs on even/odd indices
 * Stage 2: Combine with W4 twiddles: W4^0=1, W4^1=-j
 *
 * All twiddles are trivial (swap+negate). 16 adds, 0 multiplies.
 * ======================================================================== */

static OPT_INLINE void fft4_forward_soa(const float *OPT_RESTRICT in_re,
                     const float *OPT_RESTRICT in_im,
                     float *OPT_RESTRICT out_re,
                     float *OPT_RESTRICT out_im)
{
    /* Stage 1: 2-pt DFTs on even indices {0,2} and odd indices {1,3} */
    float e0_re = in_re[0] + in_re[2];
    float e0_im = in_im[0] + in_im[2];
    float e1_re = in_re[0] - in_re[2];
    float e1_im = in_im[0] - in_im[2];

    float o0_re = in_re[1] + in_re[3];
    float o0_im = in_im[1] + in_im[3];
    float o1_re = in_re[1] - in_re[3];
    float o1_im = in_im[1] - in_im[3];

    /* Stage 2: Combine with twiddles
     * W4^0 = 1        -> o0 * 1 = (o0_re, o0_im)
     * W4^1 = -j       -> o1 * -j = (o1_im, -o1_re) */
    out_re[0] = e0_re + o0_re;
    out_im[0] = e0_im + o0_im;
    out_re[1] = e1_re + o1_im;
    out_im[1] = e1_im - o1_re;
    out_re[2] = e0_re - o0_re;
    out_im[2] = e0_im - o0_im;
    out_re[3] = e1_re - o1_im;
    out_im[3] = e1_im + o1_re;
}

/* ========================================================================
 * 8-point FFT (Cooley-Tukey DIT radix-2)
 *
 * Two inline 4-pt DFTs (even/odd indices) + combine with W8 twiddles.
 *   W8^0 = 1              (free)
 *   W8^1 = (S2, -S2)      where S2 = sqrt(2)/2
 *   W8^2 = -j             (free: swap+negate)
 *   W8^3 = (-S2, -S2)
 *
 * Optimized W8^1 multiply: W8^1 * z = S2*((zr+zi), (zi-zr))
 * Optimized W8^3 multiply: W8^3 * z = S2*((-zr+zi), (-zi-zr))
 *
 * 4 multiplies + 52 adds.
 * ======================================================================== */

static OPT_INLINE void fft8_forward_soa(const float *OPT_RESTRICT in_re,
                     const float *OPT_RESTRICT in_im,
                     float *OPT_RESTRICT out_re,
                     float *OPT_RESTRICT out_im)
{
    /* --- Inline 4-pt DFT on even indices {0,2,4,6} --- */
    float ee0_re = in_re[0] + in_re[4];
    float ee0_im = in_im[0] + in_im[4];
    float ee1_re = in_re[0] - in_re[4];
    float ee1_im = in_im[0] - in_im[4];

    float eo0_re = in_re[2] + in_re[6];
    float eo0_im = in_im[2] + in_im[6];
    float eo1_re = in_re[2] - in_re[6];
    float eo1_im = in_im[2] - in_im[6];

    float e0_re = ee0_re + eo0_re;
    float e0_im = ee0_im + eo0_im;
    float e1_re = ee1_re + eo1_im;
    float e1_im = ee1_im - eo1_re;
    float e2_re = ee0_re - eo0_re;
    float e2_im = ee0_im - eo0_im;
    float e3_re = ee1_re - eo1_im;
    float e3_im = ee1_im + eo1_re;

    /* --- Inline 4-pt DFT on odd indices {1,3,5,7} --- */
    float oe0_re = in_re[1] + in_re[5];
    float oe0_im = in_im[1] + in_im[5];
    float oe1_re = in_re[1] - in_re[5];
    float oe1_im = in_im[1] - in_im[5];

    float oo0_re = in_re[3] + in_re[7];
    float oo0_im = in_im[3] + in_im[7];
    float oo1_re = in_re[3] - in_re[7];
    float oo1_im = in_im[3] - in_im[7];

    float o0_re = oe0_re + oo0_re;
    float o0_im = oe0_im + oo0_im;
    float o1_re = oe1_re + oo1_im;
    float o1_im = oe1_im - oo1_re;
    float o2_re = oe0_re - oo0_re;
    float o2_im = oe0_im - oo0_im;
    float o3_re = oe1_re - oo1_im;
    float o3_im = oe1_im + oo1_re;

    /* --- Stage 3: Combine with W8 twiddles ---
     * k=0: W8^0=1       -> t = o0
     * k=1: W8^1*o1      -> t = S2*((o1r+o1i), (o1i-o1r))
     * k=2: W8^2=-j      -> t = (o2i, -o2r)
     * k=3: W8^3*o3      -> t = S2*((-o3r+o3i), (-o3i-o3r))
     */
    float t1_re = FFT_SQRT2_2 * (o1_re + o1_im);
    float t1_im = FFT_SQRT2_2 * (o1_im - o1_re);

    float t3_re = FFT_SQRT2_2 * (-o3_re + o3_im);
    float t3_im = FFT_SQRT2_2 * (-o3_im - o3_re);

    out_re[0] = e0_re + o0_re;
    out_im[0] = e0_im + o0_im;
    out_re[1] = e1_re + t1_re;
    out_im[1] = e1_im + t1_im;
    out_re[2] = e2_re + o2_im;
    out_im[2] = e2_im - o2_re;
    out_re[3] = e3_re + t3_re;
    out_im[3] = e3_im + t3_im;
    out_re[4] = e0_re - o0_re;
    out_im[4] = e0_im - o0_im;
    out_re[5] = e1_re - t1_re;
    out_im[5] = e1_im - t1_im;
    out_re[6] = e2_re - o2_im;
    out_im[6] = e2_im + o2_re;
    out_re[7] = e3_re - t3_re;
    out_im[7] = e3_im - t3_im;
}

/* ========================================================================
 * 16-point FFT (Radix-4 with staging buffer)
 *
 * Radix-4 decomposition eliminates the register pressure problem of
 * the radix-2 approach (~48 simultaneous live values → 83 stack spills).
 *
 * Stage 1: Four 4-pt DFTs on decimated-by-4 subsequences → staging buffer
 *   Group 0: {x[0], x[4], x[8], x[12]}   → t[0..3]
 *   Group 1: {x[1], x[5], x[9], x[13]}   → t[4..7]
 *   Group 2: {x[2], x[6], x[10], x[14]}  → t[8..11]
 *   Group 3: {x[3], x[7], x[11], x[15]}  → t[12..15]
 *
 * Stage 2: For each k=0..3, apply twiddles + radix-4 butterfly:
 *   a = t[k]    (no twiddle)     b = W16^k  * t[k+4]
 *   c = W16^2k * t[k+8]         d = W16^3k * t[k+12]
 *   p = a+c, q = b+d, r = a-c, s = b-d
 *   out[k]=p+q, out[k+4]=r-js, out[k+8]=p-q, out[k+12]=r+js
 *
 * Peak register pressure: ~16 (fits in 16 XMM registers, zero spills).
 * 24 multiplies + 144 adds.
 * ======================================================================== */

static OPT_HOT void fft16_forward_soa(const float *OPT_RESTRICT in_re,
                       const float *OPT_RESTRICT in_im,
                       float *OPT_RESTRICT out_re,
                       float *OPT_RESTRICT out_im)
{
    /* Staging buffer: 128 bytes, stays in L1 cache */
    float tr[16], ti[16];

    /* ---- Stage 1: Four 4-pt DFTs ---- */

    /* Group 0: {x[0], x[4], x[8], x[12]} → t[0..3] */
    {
        float e0r = in_re[0] + in_re[8],  e0i = in_im[0] + in_im[8];
        float e1r = in_re[0] - in_re[8],  e1i = in_im[0] - in_im[8];
        float o0r = in_re[4] + in_re[12], o0i = in_im[4] + in_im[12];
        float o1r = in_re[4] - in_re[12], o1i = in_im[4] - in_im[12];

        tr[0] = e0r + o0r; ti[0] = e0i + o0i;
        tr[1] = e1r + o1i; ti[1] = e1i - o1r;
        tr[2] = e0r - o0r; ti[2] = e0i - o0i;
        tr[3] = e1r - o1i; ti[3] = e1i + o1r;
    }

    /* Group 1: {x[1], x[5], x[9], x[13]} → t[4..7] */
    {
        float e0r = in_re[1] + in_re[9],  e0i = in_im[1] + in_im[9];
        float e1r = in_re[1] - in_re[9],  e1i = in_im[1] - in_im[9];
        float o0r = in_re[5] + in_re[13], o0i = in_im[5] + in_im[13];
        float o1r = in_re[5] - in_re[13], o1i = in_im[5] - in_im[13];

        tr[4] = e0r + o0r; ti[4] = e0i + o0i;
        tr[5] = e1r + o1i; ti[5] = e1i - o1r;
        tr[6] = e0r - o0r; ti[6] = e0i - o0i;
        tr[7] = e1r - o1i; ti[7] = e1i + o1r;
    }

    /* Group 2: {x[2], x[6], x[10], x[14]} → t[8..11] */
    {
        float e0r = in_re[2] + in_re[10], e0i = in_im[2] + in_im[10];
        float e1r = in_re[2] - in_re[10], e1i = in_im[2] - in_im[10];
        float o0r = in_re[6] + in_re[14], o0i = in_im[6] + in_im[14];
        float o1r = in_re[6] - in_re[14], o1i = in_im[6] - in_im[14];

        tr[8]  = e0r + o0r; ti[8]  = e0i + o0i;
        tr[9]  = e1r + o1i; ti[9]  = e1i - o1r;
        tr[10] = e0r - o0r; ti[10] = e0i - o0i;
        tr[11] = e1r - o1i; ti[11] = e1i + o1r;
    }

    /* Group 3: {x[3], x[7], x[11], x[15]} → t[12..15] */
    {
        float e0r = in_re[3] + in_re[11], e0i = in_im[3] + in_im[11];
        float e1r = in_re[3] - in_re[11], e1i = in_im[3] - in_im[11];
        float o0r = in_re[7] + in_re[15], o0i = in_im[7] + in_im[15];
        float o1r = in_re[7] - in_re[15], o1i = in_im[7] - in_im[15];

        tr[12] = e0r + o0r; ti[12] = e0i + o0i;
        tr[13] = e1r + o1i; ti[13] = e1i - o1r;
        tr[14] = e0r - o0r; ti[14] = e0i - o0i;
        tr[15] = e1r - o1i; ti[15] = e1i + o1r;
    }

    /* ---- Stage 2: Twiddle + radix-4 butterfly ----
     *
     * For each k=0..3:
     *   a = t[k]        (no twiddle)
     *   b = W16^k  * t[k+4]
     *   c = W16^2k * t[k+8]
     *   d = W16^3k * t[k+12]
     *
     * Radix-4 butterfly:
     *   p = a+c, q = b+d, r = a-c, s = b-d
     *   out[k]    = p + q
     *   out[k+4]  = r - j*s   (swap+negate)
     *   out[k+8]  = p - q
     *   out[k+12] = r + j*s   (swap+negate)
     */

    /* k=0: all twiddles W16^0 = 1 (free) */
    {
        float ar = tr[0],  ai = ti[0];
        float br = tr[4],  bi = ti[4];
        float cr = tr[8],  ci = ti[8];
        float dr = tr[12], di = ti[12];

        float pr = ar + cr, pi = ai + ci;
        float qr = br + dr, qi = bi + di;
        float rr = ar - cr, ri = ai - ci;
        float sr = br - dr, si = bi - di;

        out_re[0]  = pr + qr; out_im[0]  = pi + qi;
        out_re[4]  = rr + si; out_im[4]  = ri - sr;
        out_re[8]  = pr - qr; out_im[8]  = pi - qi;
        out_re[12] = rr - si; out_im[12] = ri + sr;
    }

    /* k=1: W16^1=(C8,-S8), W16^2=(S2,-S2), W16^3=(S8,-C8) */
    {
        float ar = tr[1], ai = ti[1];

        float br = FFT_COS_PI8 * tr[5] + FFT_SIN_PI8 * ti[5];
        float bi = FFT_COS_PI8 * ti[5] - FFT_SIN_PI8 * tr[5];

        float cr = FFT_SQRT2_2 * (tr[9] + ti[9]);
        float ci = FFT_SQRT2_2 * (ti[9] - tr[9]);

        float dr = FFT_SIN_PI8 * tr[13] + FFT_COS_PI8 * ti[13];
        float di = FFT_SIN_PI8 * ti[13] - FFT_COS_PI8 * tr[13];

        float pr = ar + cr, pi = ai + ci;
        float qr = br + dr, qi = bi + di;
        float rr = ar - cr, ri = ai - ci;
        float sr = br - dr, si = bi - di;

        out_re[1]  = pr + qr; out_im[1]  = pi + qi;
        out_re[5]  = rr + si; out_im[5]  = ri - sr;
        out_re[9]  = pr - qr; out_im[9]  = pi - qi;
        out_re[13] = rr - si; out_im[13] = ri + sr;
    }

    /* k=2: W16^2=(S2,-S2), W16^4=-j, W16^6=(-S2,-S2) */
    {
        float ar = tr[2], ai = ti[2];

        float br = FFT_SQRT2_2 * (tr[6] + ti[6]);
        float bi = FFT_SQRT2_2 * (ti[6] - tr[6]);

        float cr = ti[10];
        float ci = -tr[10];

        float dr = FFT_SQRT2_2 * (-tr[14] + ti[14]);
        float di = FFT_SQRT2_2 * (-ti[14] - tr[14]);

        float pr = ar + cr, pi = ai + ci;
        float qr = br + dr, qi = bi + di;
        float rr = ar - cr, ri = ai - ci;
        float sr = br - dr, si = bi - di;

        out_re[2]  = pr + qr; out_im[2]  = pi + qi;
        out_re[6]  = rr + si; out_im[6]  = ri - sr;
        out_re[10] = pr - qr; out_im[10] = pi - qi;
        out_re[14] = rr - si; out_im[14] = ri + sr;
    }

    /* k=3: W16^3=(S8,-C8), W16^6=(-S2,-S2), W16^9=(-C8,S8) */
    {
        float ar = tr[3], ai = ti[3];

        float br = FFT_SIN_PI8 * tr[7] + FFT_COS_PI8 * ti[7];
        float bi = FFT_SIN_PI8 * ti[7] - FFT_COS_PI8 * tr[7];

        float cr = FFT_SQRT2_2 * (-tr[11] + ti[11]);
        float ci = FFT_SQRT2_2 * (-ti[11] - tr[11]);

        /* W16^9 = (-cos(pi/8), sin(pi/8)) */
        float dr = -FFT_COS_PI8 * tr[15] - FFT_SIN_PI8 * ti[15];
        float di =  FFT_SIN_PI8 * tr[15] - FFT_COS_PI8 * ti[15];

        float pr = ar + cr, pi = ai + ci;
        float qr = br + dr, qi = bi + di;
        float rr = ar - cr, ri = ai - ci;
        float sr = br - dr, si = bi - di;

        out_re[3]  = pr + qr; out_im[3]  = pi + qi;
        out_re[7]  = rr + si; out_im[7]  = ri - sr;
        out_re[11] = pr - qr; out_im[11] = pi - qi;
        out_re[15] = rr - si; out_im[15] = ri + sr;
    }
}

#endif /* FFT_KERNELS_H_ */
