/** @file
    Definition of r_private state structure.
*/

#ifndef INCLUDE_R_PRIVATE_H_
#define INCLUDE_R_PRIVATE_H_

#include <stdint.h>
#include <time.h>
#include "list.h"
#include "baseband.h"
#include "pulse_detect.h"
#include "fileformat.h"
#include "samp_grab.h"
#include "am_analyze.h"
#include "rtl_433.h"
#include "compat_time.h"
#include "cf32_resampler.h"
#include "wb_dedup.h"

struct dm_state {
    float auto_level;
    float squelch_offset;
    float level_limit;
    float noise_level;
    float min_level_auto;
    float min_level;
    float min_snr;
    float low_pass;
    int use_mag_est;
    int detect_verbosity;

    int16_t am_buf[MAXIMAL_BUF_LENGTH];  // AM demodulated signal (for OOK decoding)
    union {
        // These buffers aren't used at the same time, so let's use a union to save some memory
        int16_t fm[MAXIMAL_BUF_LENGTH];  // FM demodulated signal (for FSK decoding)
        uint16_t temp[MAXIMAL_BUF_LENGTH];  // Temporary buffer (to be optimized out..)
    } buf;
    uint8_t u8_buf[MAXIMAL_BUF_LENGTH]; // format conversion buffer
    float f32_buf[MAXIMAL_BUF_LENGTH]; // format conversion buffer
    int sample_size; // CU8: 2, CS16: 4
    pulse_detect_t *pulse_detect;
    filter_state_t lowpass_filter_state;
    demodfm_state_t demod_FM_state;
    int enable_FM_demod;
    unsigned fsk_pulse_detect_mode;
    unsigned frequency;
    samp_grab_t *samp_grab;
    am_analyze_t *am_analyze;
    int analyze_pulses;
    file_info_t load_info;
    list_t dumper;

    /* Protocol states */
    list_t r_devs;

    pulse_data_t    pulse_data;
    pulse_data_t    fsk_pulse_data;
    unsigned frame_event_count;
    unsigned frame_start_ago;
    unsigned frame_end_ago;
    struct timeval now;
    float sample_file_pos;

    /*
     * Per-channel state for wideband mode.
     * These fields are grouped here rather than in a separate struct
     * to avoid an extra indirection level and to keep dm_state as the
     * single owner of all demodulator state (matching upstream r_private.h).
     */
    int wideband_channels_allocated;                        ///< Number of channels allocated
    pulse_detect_t **wb_pulse_detect;                       ///< Per-channel pulse detectors
    filter_state_t *wb_lowpass_filter_state;                ///< Per-channel lowpass filter states
    demodfm_state_t *wb_demod_FM_state;                     ///< Per-channel FM demod states
    float *wb_noise_level;                                  ///< Per-channel noise levels (dB)
    float *wb_min_level_auto;                               ///< Per-channel auto min levels (dB)
    cf32_resampler_t *wb_resamplers;                        ///< Per-channel resamplers (channel_rate -> target_rate)
    uint32_t wb_target_rate;                                ///< Target sample rate after resampling (e.g., 250000)
    pulse_data_t *wb_pulse_data;                            ///< Per-channel OOK pulse data
    pulse_data_t *wb_fsk_pulse_data;                        ///< Per-channel FSK pulse data

    /* Per-channel scratch buffers (isolate channels from shared demod buffers) */
    int16_t *wb_am_bufs;                                    ///< Per-channel AM demod buffers [ch * wb_buf_len]
    int16_t *wb_fm_bufs;                                    ///< Per-channel FM demod buffers [ch * wb_buf_len]
    uint16_t *wb_temp_bufs;                                 ///< Per-channel temp/magnitude buffers [ch * wb_buf_len]
    size_t wb_buf_len;                                      ///< Per-channel buffer length (samples)
    wb_dedup_t *wb_dedup;                                   ///< Wideband cross-channel deduplication
    unsigned *wb_decode_count;                               ///< Per-channel successful decode count [num_channels]
    float *wb_channel_freqs;                                 ///< Per-channel center frequencies (Hz) [num_channels]
    float *wb_smoothed_power;                                ///< Per-channel smoothed power (dB) [num_channels]
};

#endif /* INCLUDE_R_PRIVATE_H_ */
