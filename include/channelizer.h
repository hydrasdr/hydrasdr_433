/** @file
    Polyphase Filter Bank Channelizer for HydraSDR wideband scanning.

    Implementation based on liquid-dsp's firpfbch algorithm by Joseph Gaeddert.
    Implements an M-channel 2x oversampled analysis PFB channelizer (OS-PFB).

    Architecture:
        Input @ fs  -->  [Polyphase Filter]  -->  [M-point FFT]  -->  M channels @ 2*fs/M

    Decimation factor D = M/2 (instead of D = M for critically-sampled).
    Adjacent channels overlap in frequency, eliminating dead zones at
    channel boundaries where the filter transition band attenuates signals.

    The polyphase structure reorders the FIR filter into M branches, processes
    samples via commutator pattern into window buffers, and uses an FFT for
    efficient frequency separation.

    Channel Frequency Mapping (natural FFT order, same as liquid-dsp):
        Channel 0:       center_freq + 0 Hz (DC)
        Channel 1:       center_freq + fs/M (positive offset)
        Channel k (k<M/2): center_freq + k*fs/M (positive frequencies)
        Channel M/2:     center_freq ± fs/2 (Nyquist, edge of band)
        Channel k (k>M/2): center_freq + (k-M)*fs/M (negative frequencies)

    Example for M=8, center=868.5 MHz, fs=2.5 MHz (spacing=312.5 kHz):
        Channel 0: 868.500 MHz (DC)
        Channel 1: 868.812 MHz (+312.5k)
        Channel 2: 869.125 MHz (+625k)
        Channel 3: 869.437 MHz (+937.5k)
        Channel 4: 869.750 MHz (Nyquist, +1.25MHz)
        Channel 5: 867.562 MHz (-937.5k)
        Channel 6: 867.875 MHz (-625k)
        Channel 7: 868.187 MHz (-312.5k)

    Performance characteristics (with m=24, 48 taps/branch):
        - Passband flatness: 0 dB ±0.1 dB
        - Adjacent channel rejection: >41 dB (8-ch), >44 dB (4-ch)
        - Non-adjacent rejection: >49 dB
        - 2x oversampled: adjacent channels overlap, no dead zones
        - Real-time margin: ~5x at 2.5 MSps

    Known limitations:
        - Channel M/2 (Nyquist) has frequency ambiguity; avoid for signals
        - Phase discontinuity of π/2 at M-sample block boundaries

    Original liquid-dsp copyright:
    Copyright (c) 2007 - 2024 Joseph Gaeddert (MIT License)

    Adaptation for HydraSDR:
    Copyright (C) 2025-2026 Benjamin Vernoux <bvernoux@hydrasdr.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_CHANNELIZER_H_
#define INCLUDE_CHANNELIZER_H_

#include <stdint.h>
#include <stdlib.h>
#include "hydrasdr_lfft.h"

#define CHANNELIZER_MAX_CHANNELS 16
#define CHANNELIZER_MAX_TAPS_PER_BRANCH 64  /* Supports up to m=32 (64 taps/branch) */
#define CHANNELIZER_CUTOFF_RATIO 0.9f       /* Cutoff at 90% of channel spacing = usable BW */

/**
 * Polyphase Filter Bank Channelizer
 *
 * Splits wideband IQ input into M equal-width frequency channels.
 * Uses 2x oversampled PFB: output rate per channel = input_rate / (M/2)
 */
typedef struct channelizer {
    int num_channels;           /* M - number of output channels */
    int taps_per_branch;        /* K - filter taps per polyphase branch */
    int total_taps;             /* M * K */

    float center_freq;          /* Center frequency of wideband input (Hz) */
    float bandwidth;            /* Total bandwidth covered (Hz) */
    float channel_spacing;      /* Spacing between channel centers (Hz) */

    uint32_t input_rate;        /* Wideband input sample rate (Hz) */
    uint32_t channel_rate;      /* Per-channel output rate = input_rate / decimation_factor */

    /* Polyphase filter coefficients [num_channels][taps_per_branch] */
    float *filter_coeffs;       /* Contiguous storage for branches */
    float **branches;           /* Pointer array to each branch */

    /* Window buffers for each polyphase branch (circular, SoA split I/Q) */
    float *window_re;           /* Real parts: M * window_alloc floats */
    float *window_im;           /* Imag parts: M * window_alloc floats */
    float **window_re_ptrs;     /* [M] pointers into window_re */
    float **window_im_ptrs;     /* [M] pointers into window_im */
    int window_len;             /* Logical length of each window = taps_per_branch */
    int window_alloc;           /* Allocated size = 2 * window_len (linear buffer) */
    int *window_write_pos;      /* Per-branch write position [num_channels] */
    int filter_index;           /* Running filter index for commutator */
    int channel_mask;           /* num_channels - 1 for fast modulo */
    int decimation_factor;      /* D = M/2 for 2x oversampled PFB */

    /* FFT work buffers (SoA split format) */
    float *fft_in_re;           /* FFT input real [num_channels] */
    float *fft_in_im;           /* FFT input imag [num_channels] */
    float *fft_out_re;          /* FFT output real [num_channels] */
    float *fft_out_im;          /* FFT output imag [num_channels] */

    /* hydrasdr-lfft plan for M-point FFT */
    hlfft_plan_t *fft_plan;     /* FFT plan for M-point forward FFT */

    /* Output buffers for each channel (CF32 interleaved I/Q) */
    float **channel_outputs;    /* [num_channels] pointers to output buffers */
    float *output_storage;      /* Contiguous storage for all outputs */
    size_t output_buf_size;     /* Max samples per channel per call */

    /* Channel frequency map (center freq of each channel output) */
    float *channel_freqs;       /* [num_channels] actual center frequencies */

    int initialized;
} channelizer_t;

/**
 * Initialize the polyphase filter bank channelizer.
 *
 * @param ch           Channelizer context to initialize
 * @param num_channels Number of output channels (M, must be power of 2, 2-16)
 * @param center_freq  Center frequency of wideband input (Hz)
 * @param bandwidth    Total bandwidth to channelize (Hz)
 * @param input_rate   Wideband input sample rate (Hz)
 * @param max_input    Maximum input samples per process call
 *
 * @return 0 on success, -1 on error
 *
 * The channelizer will create num_channels outputs, each with:
 *   - Sample rate: 2 * input_rate / num_channels (2x oversampled)
 *   - Bandwidth: bandwidth / num_channels
 *   - Center frequencies evenly spaced across the input bandwidth
 */
int channelizer_init(channelizer_t *ch, int num_channels,
                     float center_freq, float bandwidth,
                     uint32_t input_rate, size_t max_input);

/**
 * Process wideband IQ samples through the channelizer.
 *
 * @param ch             Initialized channelizer context
 * @param input          Input CF32 samples (interleaved I/Q)
 * @param n_samples      Number of complex samples in input
 * @param channel_out    Output: array of pointers to channel output buffers
 * @param out_samples    Output: number of samples produced per channel
 *
 * @return 0 on success, -1 on error
 *
 * Each output buffer contains CF32 (interleaved I/Q) at channel_rate.
 * Output samples = n_samples / decimation_factor (2x oversampled).
 */
int channelizer_process(channelizer_t *ch, const float *input, int n_samples,
                        float **channel_out, int *out_samples);

/**
 * Get the center frequency of a specific channel.
 *
 * @param ch      Initialized channelizer context
 * @param channel Channel index (0 to num_channels-1)
 *
 * @return Center frequency in Hz, or 0 if invalid
 */
float channelizer_get_channel_freq(channelizer_t *ch, int channel);

/**
 * Free all channelizer resources.
 *
 * @param ch Channelizer context to free
 */
void channelizer_free(channelizer_t *ch);

/**
 * Get build info string for the channelizer DSP hot path.
 *
 * Returns a string describing the compiler, SIMD capabilities, and
 * optimization flags used to compile the channelizer (which may differ
 * from the main executable due to per-file DSP optimization flags).
 *
 * @return Static string, e.g. "Release GCC 15.2.0 AVX-512 FMA fast-math"
 */
const char *channelizer_build_info(void);

#endif /* INCLUDE_CHANNELIZER_H_ */
