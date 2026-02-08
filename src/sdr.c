/** @file
    SDR input for HydraSDR with native CF32 support.

    Based on rtl_433 by Christian Zuckschwerdt and contributors.
    https://github.com/merbanan/rtl_433

    Copyright (C) 2012 by Steve Markgraf <steve@steve-m.de>
    Copyright (C) 2014 by Kyle Keen <keenerd@gmail.com>
    Copyright (C) 2016 by Robert X. Seger
    Copyright (C) 2018 Christian Zuckschwerdt (original rtl_433)
    Copyright (C) 2025-2026 Benjamin Vernoux <bvernoux@hydrasdr.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <math.h>
#include "sdr.h"
#include "r_util.h"
#include "optparse.h"
#include "logger.h"
#include "fatal.h"
#include "compat_pthread.h"
#ifdef HYDRASDR
#include <hydrasdr.h>
#endif

#ifndef _MSC_VER
#include <unistd.h>
#endif

#ifdef _WIN32
    #if !defined(_WIN32_WINNT) || (_WIN32_WINNT < 0x0600)
    #undef _WIN32_WINNT
    #define _WIN32_WINNT 0x0600   /* Needed to pull in 'struct sockaddr_storage' */
    #endif

    #include <winsock2.h>
    #include <ws2tcpip.h>
    #define SHUT_RDWR SD_BOTH
    #define perror(str)  ws2_perror(str)

    static void ws2_perror (const char *str)
    {
        if (str && *str)
            fprintf(stderr, "%s: ", str);
        fprintf(stderr, "Winsock error %d.\n", WSAGetLastError());
    }
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netdb.h>
    #include <netinet/in.h>

    #define SOCKET          int
    #define INVALID_SOCKET  (-1)
    #define closesocket(x)  close(x)
#endif

#define GAIN_STR_MAX_SIZE 64

/*============================================================================
 * Float32 Polyphase Resampler (HydraSDR)
 * Shared implementation from cf32_resampler.h
 *============================================================================*/
#ifdef HYDRASDR
#include "cf32_resampler.h"
#endif /* HYDRASDR */

struct sdr_dev {
    SOCKET rtl_tcp;
    uint32_t rtl_tcp_freq; ///< last known center frequency, rtl_tcp only.
    uint32_t rtl_tcp_rate; ///< last known sample rate, rtl_tcp only.

#ifdef HYDRASDR
    void *hydrasdr_ctx; ///< HydraSDR context (hydrasdr_ctx_t *)
#endif

    char *dev_info;

    int running;
    uint8_t *buffer; ///< sdr data buffer current and past frames
    size_t buffer_size; ///< sdr data buffer overall size (num * len)
    size_t buffer_pos; ///< sdr data buffer next write position

    int sample_size;
    int sample_signed;
    sdr_sample_format_t sample_format;  ///< Native sample format

    uint32_t sample_rate;
    uint32_t center_frequency;

#ifdef THREADS
    pthread_t thread;
    pthread_mutex_t lock; ///< lock for exit_acquire
    int exit_acquire;

    // acquire thread args
    sdr_event_cb_t async_cb;
    void *async_ctx;
    uint32_t buf_num;
    uint32_t buf_len;
#endif
};

/* rtl_tcp helpers */

#pragma pack(push, 1)
struct rtl_tcp_info {
    char magic[4];             // "RTL0"
    uint32_t tuner_number;     // big endian
    uint32_t tuner_gain_count; // big endian
};
#pragma pack(pop)

/* Disable GCC analyzer false positives for Windows socket code.
   The analyzer doesn't understand that INVALID_SOCKET is the only failure
   value on Windows (where SOCKET is an unsigned type). */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wanalyzer-fd-leak"
#pragma GCC diagnostic ignored "-Wanalyzer-fd-use-without-check"
#endif

static int rtltcp_open(sdr_dev_t **out_dev, char const *dev_query, int verbose)
{
    UNUSED(verbose);
    char const *host = "localhost";
    char const *port = "1234";
    char hostport[280]; // 253 chars DNS name plus extra chars

    char *param = arg_param(dev_query); // strip scheme
    hostport[0] = '\0';
    if (param) {
        snprintf(hostport, sizeof(hostport), "%s", param);
    }
    hostport_param(hostport, &host, &port);

    print_logf(LOG_CRITICAL, "SDR", "rtl_tcp input from %s port %s", host, port);

#ifdef _WIN32
    WSADATA wsa;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        perror("WSAStartup()");
        return -1;
    }
#endif

    struct addrinfo hints, *res, *res0;
    int ret;
    SOCKET sock;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = 0;
    hints.ai_flags    = AI_ADDRCONFIG;

    ret = getaddrinfo(host, port, &hints, &res0);
    if (ret) {
        print_log(LOG_ERROR, __func__, gai_strerror(ret));
        return -1;
    }
    sock = INVALID_SOCKET;
    for (res = res0; res; res = res->ai_next) {
        sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (sock != INVALID_SOCKET) {
            ret = connect(sock, res->ai_addr, (int)res->ai_addrlen);
            if (ret == 0) {
                break; // success
            }
            perror("connect");
            closesocket(sock);
            sock = INVALID_SOCKET;
        }
    }
    freeaddrinfo(res0);
    if (sock == INVALID_SOCKET) {
        perror("socket");
        return -1;
    }

    //int const value_one = 1;
    //ret = setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char *)&value_one, sizeof(value_one));
    //if (ret < 0)
    //    fprintf(stderr, "rtl_tcp TCP_NODELAY failed\n");

    struct rtl_tcp_info info;
    ret = recv(sock, (char *)&info, sizeof (info), 0);
    if (ret != 12) {
        print_logf(LOG_ERROR, __func__, "Bad rtl_tcp header (%d)", ret);
        closesocket(sock);
        return -1;
    }
    if (strncmp(info.magic, "RTL0", 4)) {
        info.tuner_number = 0; // terminate magic
        print_logf(LOG_ERROR, __func__, "Bad rtl_tcp header magic \"%s\"", info.magic);
        closesocket(sock);
        return -1;
    }

    unsigned tuner_number = ntohl(info.tuner_number);
    //int tuner_gain_count  = ntohl(info.tuner_gain_count);

    char const *tuner_names[] = { "Unknown", "E4000", "FC0012", "FC0013", "FC2580", "R820T", "R828D" };
    char const *tuner_name = tuner_number >= sizeof(tuner_names) / sizeof(tuner_names[0]) ? "Invalid" : tuner_names[tuner_number];

    print_logf(LOG_CRITICAL, "SDR", "rtl_tcp connected to %s:%s (Tuner: %s)", host, port, tuner_name);

    sdr_dev_t *dev = calloc(1, sizeof(sdr_dev_t));
    if (!dev) {
        WARN_CALLOC("rtltcp_open()");
        closesocket(sock);
        return -1; // NOTE: returns error on alloc failure.
    }
#ifdef THREADS
    pthread_mutex_init(&dev->lock, NULL);
#endif

    dev->rtl_tcp = sock;
    dev->sample_size = sizeof(uint8_t) * 2; // CU8
    dev->sample_signed = 0;

    *out_dev = dev;
    return 0;
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

static int rtltcp_close(SOCKET sock)
{
    int ret = shutdown(sock, SHUT_RDWR);
    if (ret == -1) {
        perror("shutdown");
        return -1;
    }

    ret = closesocket(sock);
    if (ret == -1) {
        perror("close");
        return -1;
    }

    return 0;
}

static int rtltcp_read_loop(sdr_dev_t *dev, sdr_event_cb_t cb, void *ctx, uint32_t buf_num, uint32_t buf_len)
{
    size_t buffer_size = (size_t)buf_num * buf_len;
    if (dev->buffer_size != buffer_size) {
        free(dev->buffer);
        dev->buffer = malloc(buffer_size);
        if (!dev->buffer) {
            WARN_MALLOC("rtltcp_read_loop()");
            return -1; // NOTE: returns error on alloc failure.
        }
        dev->buffer_size = buffer_size;
        dev->buffer_pos = 0;
    }

    dev->running = 1;
    do {
        if (dev->buffer_pos + buf_len > buffer_size)
            dev->buffer_pos = 0;
        uint8_t *buffer = &dev->buffer[dev->buffer_pos];
        dev->buffer_pos += buf_len;

        unsigned n_read = 0;
        int r;

        do {
            r = recv(dev->rtl_tcp, &buffer[n_read], buf_len - n_read, MSG_WAITALL);
            if (r <= 0)
                break;
            n_read += r;
            //fprintf(stderr, "readStream ret=%d (of %u)\n", r, n_read);
        } while (n_read < buf_len);
        //fprintf(stderr, "readStream ret=%d (read %u)\n", r, n_read);

        if (r < 0) {
            print_logf(LOG_WARNING, __func__, "sync read failed. %d", r);
        }
        if (n_read == 0) {
            perror("rtl_tcp");
            dev->running = 0;
        }

#ifdef THREADS
        pthread_mutex_lock(&dev->lock);
#endif
        uint32_t sample_rate      = dev->sample_rate;
        uint32_t center_frequency = dev->center_frequency;
#ifdef THREADS
        pthread_mutex_unlock(&dev->lock);
#endif
        sdr_event_t ev = {
                .ev               = SDR_EV_DATA,
                .sample_rate      = sample_rate,
                .center_frequency = center_frequency,
                .buf              = buffer,
                .len              = n_read,
        };
#ifdef THREADS
        pthread_mutex_lock(&dev->lock);
        int exit_acquire = dev->exit_acquire;
        pthread_mutex_unlock(&dev->lock);
        if (exit_acquire) {
            break; // do not deliver any more events
        }
#endif
        if (n_read > 0) // prevent a crash in callback
            cb(&ev, ctx);

    } while (dev->running);

    return 0;
}

#pragma pack(push, 1)
struct command {
    unsigned char cmd;
    unsigned int param;
};
#pragma pack(pop)

// rtl_tcp API
#define RTLTCP_SET_FREQ 0x01
#define RTLTCP_SET_SAMPLE_RATE 0x02
#define RTLTCP_SET_GAIN_MODE 0x03
#define RTLTCP_SET_GAIN 0x04
#define RTLTCP_SET_FREQ_CORRECTION 0x05
#define RTLTCP_SET_IF_TUNER_GAIN 0x06
#define RTLTCP_SET_TEST_MODE 0x07
#define RTLTCP_SET_AGC_MODE 0x08
#define RTLTCP_SET_DIRECT_SAMPLING 0x09
#define RTLTCP_SET_OFFSET_TUNING 0x0a
#define RTLTCP_SET_RTL_XTAL 0x0b
#define RTLTCP_SET_TUNER_XTAL 0x0c
#define RTLTCP_SET_TUNER_GAIN_BY_ID 0x0d
#define RTLTCP_SET_BIAS_TEE 0x0e

static int rtltcp_command(sdr_dev_t *dev, char cmd, int param)
{
    struct command command;
    command.cmd   = cmd;
    command.param = htonl(param);

    return sizeof(command) == send(dev->rtl_tcp, (const char*) &command, sizeof(command), 0) ? 0 : -1;
}

/* HydraSDR helpers */

#ifdef HYDRASDR

/**
 * HydraSDR device context stored within sdr_dev_t
 */
typedef struct {
    struct hydrasdr_device *dev;
    hydrasdr_device_info_t info;
    uint32_t *samplerates;
    uint32_t samplerate_count;
    uint32_t current_samplerate;
    uint32_t current_frequency;
    int streaming;

    /* Callback bridge */
    sdr_event_cb_t cb;
    void *cb_ctx;

    /* Buffer management */
    uint8_t *buffer;
    size_t buffer_size;
    size_t buffer_pos;

    /* Sample info */
    int sample_size;
    int sample_signed;

    /* Polyphase resampler (float32 -> CU8 with rate conversion) */
    cf32_resampler_t resampler;
    uint32_t requested_samplerate;    /* Rate requested by hydrasdr_433 */
    int needs_resampling;        /* 1 if actual != requested */

    int manual_gain_set;         /* 1 if gain was manually set via settings */
    uint32_t agc_enabled;        /* bitmask: bit N = AGC type N is enabled */
    uint32_t gains_set;          /* bitmask: bit N = gain type N was explicitly set */
} hydrasdr_ctx_t;

/**
 * Parse device query string for HydraSDR
 * Formats:
 *   hydrasdr          - first device
 *   hydrasdr:0        - device by index
 *   hydrasdr:serial=XXXX - device by serial
 */
static int hydrasdr_parse_query(char const *dev_query, char *serial_out, size_t serial_len)
{
    int index = 0;
    serial_out[0] = '\0';

    if (!dev_query)
        return 0;

    /* Strip scheme prefix */
    char const *param = dev_query;
    if (strncmp(param, "hydrasdr", 8) == 0) {
        param += 8;
        if (*param == ':')
            param++;
    }

    if (!*param)
        return 0;

    /* Check for serial= */
    if (strncmp(param, "serial=", 7) == 0) {
        snprintf(serial_out, serial_len, "%s", param + 7);
        return -1; /* Indicates serial mode */
    }

    /* Try as index */
    index = atoi(param);
    return index;
}

/**
 * Find best matching sample rate from available rates
 * Tries to find exact match first, then prefers the next higher rate
 * for better signal quality, finally falls back to highest available rate.
 */
static uint32_t hydrasdr_find_best_samplerate(hydrasdr_ctx_t *ctx, uint32_t requested, int verbose)
{
    if (!ctx->samplerates || ctx->samplerate_count == 0)
        return 0;

    /* First, check for exact match */
    for (uint32_t i = 0; i < ctx->samplerate_count; i++) {
        if (ctx->samplerates[i] == requested) {
            if (verbose)
                print_logf(LOG_NOTICE, "HydraSDR", "Exact sample rate match: %u Hz", requested);
            return requested;
        }
    }

    /* Find smallest rate that is >= requested (prefer higher for quality) */
    uint32_t best_higher = UINT32_MAX;
    for (uint32_t i = 0; i < ctx->samplerate_count; i++) {
        uint32_t rate = ctx->samplerates[i];
        if (rate >= requested && rate < best_higher) {
            best_higher = rate;
        }
    }

    if (best_higher != UINT32_MAX) {
        if (verbose)
            print_logf(LOG_NOTICE, "HydraSDR",
                       "Requested %u Hz, using higher rate %u Hz",
                       requested, best_higher);
        return best_higher;
    }

    /* Fall back to highest available rate (all rates are lower than requested) */
    uint32_t best_rate = 0;
    for (uint32_t i = 0; i < ctx->samplerate_count; i++) {
        if (ctx->samplerates[i] > best_rate) {
            best_rate = ctx->samplerates[i];
        }
    }

    if (verbose)
        print_logf(LOG_WARNING, "HydraSDR",
                   "Requested %u Hz too high, using maximum rate %u Hz",
                   requested, best_rate);

    return best_rate;
}

/**
 * Distribute gain across available gain stages
 * Uses unified gain API in a hardware-agnostic way
 */
/**
 * Check if a gain type is actually managed by this hardware.
 * A gain range of 0-0 in device_info means the gain is not managed,
 * even if the capability flag is set and the gain API returns cached values.
 * Returns the max_value from device_info, or 0 if not managed.
 */
static int hydrasdr_get_gain_max(hydrasdr_ctx_t *ctx, hydrasdr_gain_type_t type)
{
    hydrasdr_device_info_t *info = &ctx->info;

    switch (type) {
    case HYDRASDR_GAIN_TYPE_LNA:
        return (info->features & HYDRASDR_CAP_LNA_GAIN) ? info->lna_gain.max_value : 0;
    case HYDRASDR_GAIN_TYPE_RF:
        return (info->features & HYDRASDR_CAP_RF_GAIN) ? info->rf_gain.max_value : 0;
    case HYDRASDR_GAIN_TYPE_MIXER:
        return (info->features & HYDRASDR_CAP_MIXER_GAIN) ? info->mixer_gain.max_value : 0;
    case HYDRASDR_GAIN_TYPE_FILTER:
        return (info->features & HYDRASDR_CAP_FILTER_GAIN) ? info->filter_gain.max_value : 0;
    case HYDRASDR_GAIN_TYPE_VGA:
        return (info->features & HYDRASDR_CAP_VGA_GAIN) ? info->vga_gain.max_value : 0;
    case HYDRASDR_GAIN_TYPE_LINEARITY:
        return (info->features & HYDRASDR_CAP_LINEARITY_GAIN) ? info->linearity_gain.max_value : 0;
    case HYDRASDR_GAIN_TYPE_SENSITIVITY:
        return (info->features & HYDRASDR_CAP_SENSITIVITY_GAIN) ? info->sensitivity_gain.max_value : 0;
    default:
        return 0;
    }
}

static int hydrasdr_set_total_gain(hydrasdr_ctx_t *ctx, int gain_val, int verbose)
{
    int r;

    /*
     * Strategy: Hardware-agnostic gain setting.
     * Use device_info gain ranges as the source of truth — a range of
     * 0-0 means the gain type is not managed by this hardware.
     * The gain_val is a raw value, clamped to the device's range.
     *
     * Priority order:
     *  1. Linearity — composite preset, configures all gain stages
     *  2. Sensitivity — composite preset, configures all gain stages
     *  3. VGA — fallback for devices without composite presets
     */
    static const struct {
        hydrasdr_gain_type_t type;
        const char *name;
    } gain_order[] = {
        { HYDRASDR_GAIN_TYPE_LINEARITY,   "Linearity" },
        { HYDRASDR_GAIN_TYPE_SENSITIVITY, "Sensitivity" },
        { HYDRASDR_GAIN_TYPE_VGA,         "VGA" },
    };

    for (int i = 0; i < (int)(sizeof(gain_order) / sizeof(gain_order[0])); i++) {
        int max_val = hydrasdr_get_gain_max(ctx, gain_order[i].type);
        if (max_val <= 0)
            continue;

        int val = gain_val;
        if (val < 0)
            val = 0;
        if (val > max_val)
            val = max_val;

        r = hydrasdr_set_gain(ctx->dev, gain_order[i].type, (uint8_t)val);
        if (r == HYDRASDR_SUCCESS) {
            ctx->gains_set |= (1u << gain_order[i].type);
            if (verbose)
                print_logf(LOG_NOTICE, "HydraSDR", "%s gain set to %d (range 0-%d)",
                           gain_order[i].name, val, max_val);
            return 0;
        }
    }

    if (verbose)
        print_log(LOG_WARNING, "HydraSDR", "No supported gain mode available");

    return -1;
}

/**
 * Enable AGC on all available gain stages
 */
static int hydrasdr_enable_agc(hydrasdr_ctx_t *ctx, int verbose)
{
    int r = 0;

    if (ctx->info.features & HYDRASDR_CAP_LNA_AGC) {
        r = hydrasdr_set_gain(ctx->dev, HYDRASDR_GAIN_TYPE_LNA_AGC, 1);
        if (r == HYDRASDR_SUCCESS) {
            ctx->agc_enabled |= (1u << HYDRASDR_GAIN_TYPE_LNA_AGC);
            if (verbose)
                print_log(LOG_NOTICE, "HydraSDR", "LNA AGC enabled");
        }
    }

    if (ctx->info.features & HYDRASDR_CAP_MIXER_AGC) {
        r = hydrasdr_set_gain(ctx->dev, HYDRASDR_GAIN_TYPE_MIXER_AGC, 1);
        if (r == HYDRASDR_SUCCESS) {
            ctx->agc_enabled |= (1u << HYDRASDR_GAIN_TYPE_MIXER_AGC);
            if (verbose)
                print_log(LOG_NOTICE, "HydraSDR", "Mixer AGC enabled");
        }
    }

    if (verbose)
        print_log(LOG_NOTICE, "HydraSDR", "Automatic gain control enabled");

    return 0;
}

/**
 * HydraSDR sample callback - bridges to hydrasdr_433 callback
 * Handles native float32 IQ samples with optional polyphase resampling.
 */
static int hydrasdr_sample_callback(hydrasdr_transfer_t *transfer)
{
    hydrasdr_ctx_t *ctx = (hydrasdr_ctx_t *)transfer->ctx;

    if (!ctx || !ctx->cb || !ctx->streaming)
        return -1;

    const float *input_samples = (const float *)transfer->samples;
    int num_complex_samples = (int)transfer->sample_count;

    const float *output_samples;
    int output_sample_count;
    uint32_t output_rate;

    if (ctx->needs_resampling && ctx->resampler.initialized) {
        /* Resample float32 -> float32 at correct rate */
        float *resampled;
        int max_output = (int)ctx->resampler.output_buf_size;
        output_sample_count = cf32_resampler_process(&ctx->resampler,
            input_samples, num_complex_samples, &resampled, max_output);
        output_samples = resampled;
        output_rate = ctx->requested_samplerate;
    } else {
        /* Pass through without resampling (preserves const) */
        output_samples = input_samples;
        output_sample_count = num_complex_samples;
        output_rate = ctx->current_samplerate;
    }

    /* Build event for hydrasdr_433 with native CF32 format */
    sdr_event_t ev = {
        .ev               = SDR_EV_DATA,
        .sample_rate      = output_rate,
        .center_frequency = ctx->current_frequency,
        .buf              = output_samples,
        .len              = (int)((size_t)output_sample_count * sizeof(float) * 2),
        .sample_format    = SDR_SAMPLE_CF32,
    };

    ctx->cb(&ev, ctx->cb_ctx);

    return ctx->streaming ? 0 : -1;
}

/**
 * Show HydraSDR device information
 */
static void hydrasdr_show_device_info(hydrasdr_ctx_t *ctx, int verbose)
{
    if (!verbose)
        return;

    hydrasdr_device_info_t *info = &ctx->info;

    print_logf(LOG_CRITICAL, "HydraSDR", "Board: %s (ID: %d)", info->board_name, info->board_id);
    print_logf(LOG_CRITICAL, "HydraSDR", "Firmware: %s", info->firmware_version);
    print_logf(LOG_NOTICE, "HydraSDR", "Frequency range: %llu - %llu Hz",
               (unsigned long long)info->min_frequency,
               (unsigned long long)info->max_frequency);

    /* List available sample rates */
    if (ctx->samplerates && ctx->samplerate_count > 0) {
        fprintf(stderr, "HydraSDR: Available sample rates: ");
        for (uint32_t i = 0; i < ctx->samplerate_count; i++) {
            fprintf(stderr, "%u%s", ctx->samplerates[i],
                    (i < ctx->samplerate_count - 1) ? ", " : "\n");
        }
    }

    /* Report capabilities */
    if (info->features & HYDRASDR_CAP_BIAS_TEE)
        print_log(LOG_NOTICE, "HydraSDR", "Bias tee: available");

    /*
     * Show gain controls using device_info as the authoritative source.
     * A gain range of 0-0 in device_info means the gain type is not
     * managed by this hardware.
     *
     * Gain modes are mutually exclusive:
     *  - Composite presets (linearity/sensitivity): configure all stages
     *  - Manual per-stage (lna/mixer/vga): set individual stages
     *  - AGC: automatic gain control per stage
     * Use -g for composite presets, -t for per-stage or AGC control.
     */
    static const struct {
        hydrasdr_gain_type_t type;
        const char *name;
    } stage_gains[] = {
        { HYDRASDR_GAIN_TYPE_LNA,    "LNA" },
        { HYDRASDR_GAIN_TYPE_RF,     "RF" },
        { HYDRASDR_GAIN_TYPE_MIXER,  "Mixer" },
        { HYDRASDR_GAIN_TYPE_FILTER, "Filter" },
        { HYDRASDR_GAIN_TYPE_VGA,    "VGA" },
    };
    static const struct {
        hydrasdr_gain_type_t type;
        const char *name;
    } preset_gains[] = {
        { HYDRASDR_GAIN_TYPE_LINEARITY,   "Linearity" },
        { HYDRASDR_GAIN_TYPE_SENSITIVITY, "Sensitivity" },
    };
    static const struct {
        hydrasdr_gain_type_t type;
        uint32_t cap_flag;
        const char *name;
    } agc_gains[] = {
        { HYDRASDR_GAIN_TYPE_LNA_AGC,   HYDRASDR_CAP_LNA_AGC,   "LNA AGC" },
        { HYDRASDR_GAIN_TYPE_MIXER_AGC, HYDRASDR_CAP_MIXER_AGC, "Mixer AGC" },
    };

    int has_stages = 0, has_presets = 0, has_agc = 0;

    /* Check what's available */
    for (int i = 0; i < (int)(sizeof(stage_gains) / sizeof(stage_gains[0])); i++)
        if (hydrasdr_get_gain_max(ctx, stage_gains[i].type) > 0)
            has_stages = 1;
    for (int i = 0; i < (int)(sizeof(preset_gains) / sizeof(preset_gains[0])); i++)
        if (hydrasdr_get_gain_max(ctx, preset_gains[i].type) > 0)
            has_presets = 1;
    for (int i = 0; i < (int)(sizeof(agc_gains) / sizeof(agc_gains[0])); i++)
        if (info->features & agc_gains[i].cap_flag)
            has_agc = 1;

    if (!has_stages && !has_presets && !has_agc)
        return;

    print_log(LOG_NOTICE, "HydraSDR", "Gain controls (modes are mutually exclusive):");

    if (has_presets) {
        print_log(LOG_NOTICE, "HydraSDR",
                  "  Presets (-g dB or -t name=val):");
        for (int i = 0; i < (int)(sizeof(preset_gains) / sizeof(preset_gains[0])); i++) {
            int max_val = hydrasdr_get_gain_max(ctx, preset_gains[i].type);
            if (max_val <= 0)
                continue;
            print_logf(LOG_NOTICE, "HydraSDR", "    %-12s range 0-%d",
                       preset_gains[i].name, max_val);
        }
    }

    if (has_stages) {
        print_log(LOG_NOTICE, "HydraSDR",
                  "  Per-stage (-t name=val, manual control):");
        for (int i = 0; i < (int)(sizeof(stage_gains) / sizeof(stage_gains[0])); i++) {
            int max_val = hydrasdr_get_gain_max(ctx, stage_gains[i].type);
            if (max_val <= 0)
                continue;
            print_logf(LOG_NOTICE, "HydraSDR", "    %-12s range 0-%d",
                       stage_gains[i].name, max_val);
        }
    }

    if (has_agc) {
        print_log(LOG_NOTICE, "HydraSDR",
                  "  AGC (-g 0 enables all, or -t is not set):");
        for (int i = 0; i < (int)(sizeof(agc_gains) / sizeof(agc_gains[0])); i++) {
            if (!(info->features & agc_gains[i].cap_flag))
                continue;
            print_logf(LOG_NOTICE, "HydraSDR", "    %-12s on/off",
                       agc_gains[i].name);
        }
    }
}

static int sdr_open_hydrasdr(sdr_dev_t **out_dev, char const *dev_query, int verbose)
{
    int r;
    char serial[64] = {0};
    int dev_index;
    hydrasdr_ctx_t *ctx = NULL;

    /* Parse query string */
    dev_index = hydrasdr_parse_query(dev_query, serial, sizeof(serial));

    /* Enumerate devices - first get count */
    int count = hydrasdr_list_devices(NULL, 0);
    if (count < 0) {
        print_logf(LOG_ERROR, "HydraSDR", "Failed to enumerate devices: %s",
                   hydrasdr_error_name(count));
        return -1;
    }
    if (count == 0) {
        print_log(LOG_ERROR, "HydraSDR", "No HydraSDR devices found");
        return -1;
    }

    /* Get serial numbers */
    uint64_t serials[16];
    int max_count = count < 16 ? count : 16;
    r = hydrasdr_list_devices(serials, max_count);
    if (r < 0) {
        print_logf(LOG_ERROR, "HydraSDR", "Failed to get device serials: %s",
                   hydrasdr_error_name(r));
        return -1;
    }

    if (verbose)
        print_logf(LOG_NOTICE, "HydraSDR", "Found %d device(s)", count);

    /* Allocate device structure */
    sdr_dev_t *dev = calloc(1, sizeof(sdr_dev_t));
    if (!dev) {
        WARN_CALLOC("sdr_open_hydrasdr()");
        return -1;
    }
#ifdef THREADS
    pthread_mutex_init(&dev->lock, NULL);
#endif

    ctx = calloc(1, sizeof(hydrasdr_ctx_t));
    if (!ctx) {
        WARN_CALLOC("sdr_open_hydrasdr()");
        free(dev);
        return -1;
    }

    /* Open device */
    if (serial[0]) {
        /* Open by serial number */
        r = hydrasdr_open_sn(&ctx->dev, (uint64_t)strtoull(serial, NULL, 16));
    } else if (dev_index >= 0 && dev_index < count) {
        /* Open by index - first open, then close others */
        r = hydrasdr_open(&ctx->dev);
    } else {
        /* Default: open first device */
        r = hydrasdr_open(&ctx->dev);
    }

    if (r != HYDRASDR_SUCCESS) {
        print_logf(LOG_ERROR, "HydraSDR", "Failed to open device: %s",
                   hydrasdr_error_name(r));
        free(ctx);
        free(dev);
        return -1;
    }

    /* Get device info for capability discovery */
    r = hydrasdr_get_device_info(ctx->dev, &ctx->info);
    if (r != HYDRASDR_SUCCESS) {
        print_logf(LOG_WARNING, "HydraSDR", "Failed to get device info: %s",
                   hydrasdr_error_name(r));
        /* Continue anyway, just won't have full capability info */
    }

    /* Get available sample rates */
    uint32_t rate_count = 0;
    r = hydrasdr_get_samplerates(ctx->dev, &rate_count, 0);
    if (r == HYDRASDR_SUCCESS && rate_count > 0) {
        ctx->samplerates = malloc(rate_count * sizeof(uint32_t));
        if (!ctx->samplerates) {
            /* Non-critical: continue without sample rate list */
        }
        else {
            r = hydrasdr_get_samplerates(ctx->dev, ctx->samplerates, rate_count);
            if (r == HYDRASDR_SUCCESS) {
                ctx->samplerate_count = rate_count;
            }
        }
    }

    /* Set sample type to FLOAT32_IQ (native format, preserves full ADC precision) */
    r = hydrasdr_set_sample_type(ctx->dev, HYDRASDR_SAMPLE_FLOAT32_IQ);
    if (r != HYDRASDR_SUCCESS) {
        print_logf(LOG_WARNING, "HydraSDR", "Failed to set sample type: %s",
                   hydrasdr_error_name(r));
    }



    /* Configure for native float32 IQ */
    ctx->sample_size = sizeof(float) * 2; /* CF32: 8 bytes per complex sample */
    ctx->sample_signed = 1;

    /* Show device info */
    hydrasdr_show_device_info(ctx, verbose);

    /* Build device info JSON string */
    const char *board_name = ctx->info.board_name ? ctx->info.board_name : "unknown";
    const char *firmware_ver = ctx->info.firmware_version ? ctx->info.firmware_version : "unknown";
    size_t info_len = 256 + strlen(board_name) + strlen(firmware_ver);
    dev->dev_info = malloc(info_len);
    if (!dev->dev_info) {
        /* Non-critical: device info just won't be available */
    }
    else {
        snprintf(dev->dev_info, info_len,
                 "{\"vendor\":\"HydraSDR\", \"product\":\"%s\", \"firmware\":\"%s\", \"sample_format\":\"CF32\"}",
                 board_name, firmware_ver);
    }

    /* Store context pointer */
    dev->hydrasdr_ctx = ctx;
    dev->sample_size = ctx->sample_size;
    dev->sample_signed = ctx->sample_signed;
    dev->sample_format = SDR_SAMPLE_CF32;

    *out_dev = dev;
    return 0;
}

static int sdr_close_hydrasdr(sdr_dev_t *dev)
{
    if (!dev || !dev->hydrasdr_ctx)
        return -1;

    hydrasdr_ctx_t *ctx = (hydrasdr_ctx_t *)dev->hydrasdr_ctx;

    /* Stop streaming if active */
    if (ctx->streaming) {
        ctx->streaming = 0;
        hydrasdr_stop_rx(ctx->dev);
    }

    /* Close device */
    if (ctx->dev) {
        hydrasdr_close(ctx->dev);
        ctx->dev = NULL;
    }

    /* Free resampler if initialized */
    if (ctx->resampler.initialized)
        cf32_resampler_free(&ctx->resampler);

    /* Free resources */
    free(ctx->samplerates);
    free(ctx->buffer);
    free(ctx);
    dev->hydrasdr_ctx = NULL;

    return 0;
}

static int sdr_set_center_freq_hydrasdr(sdr_dev_t *dev, uint32_t freq, int verbose)
{
    if (!dev || !dev->hydrasdr_ctx)
        return -1;

    hydrasdr_ctx_t *ctx = (hydrasdr_ctx_t *)dev->hydrasdr_ctx;

    int r = hydrasdr_set_freq(ctx->dev, (uint64_t)freq);
    if (r != HYDRASDR_SUCCESS) {
        if (verbose)
            print_logf(LOG_WARNING, "HydraSDR", "Failed to set frequency: %s",
                       hydrasdr_error_name(r));
        return -1;
    }

    ctx->current_frequency = freq;

    if (verbose)
        print_logf(LOG_NOTICE, "HydraSDR", "Tuned to %s", nice_freq(freq));

    return 0;
}

static uint32_t sdr_get_center_freq_hydrasdr(sdr_dev_t *dev)
{
    if (!dev || !dev->hydrasdr_ctx)
        return 0;

    hydrasdr_ctx_t *ctx = (hydrasdr_ctx_t *)dev->hydrasdr_ctx;
    return ctx->current_frequency;
}

static int sdr_set_sample_rate_hydrasdr(sdr_dev_t *dev, uint32_t rate, int verbose)
{
    if (!dev || !dev->hydrasdr_ctx)
        return -1;

    hydrasdr_ctx_t *ctx = (hydrasdr_ctx_t *)dev->hydrasdr_ctx;

    /* Store the requested rate (what hydrasdr_433 expects) */
    ctx->requested_samplerate = rate;

    /* Find best matching rate (HydraSDR handles decimation internally) */
    uint32_t actual_rate = hydrasdr_find_best_samplerate(ctx, rate, verbose);
    if (actual_rate == 0) {
        print_log(LOG_ERROR, "HydraSDR", "No valid sample rate available");
        return -1;
    }

    int r = hydrasdr_set_samplerate(ctx->dev, actual_rate);
    if (r != HYDRASDR_SUCCESS) {
        if (verbose)
            print_logf(LOG_WARNING, "HydraSDR", "Failed to set sample rate: %s",
                       hydrasdr_error_name(r));
        return -1;
    }

    ctx->current_samplerate = actual_rate;

    /* Free old resampler if exists */
    if (ctx->resampler.initialized) {
        cf32_resampler_free(&ctx->resampler);
        ctx->needs_resampling = 0;
    }

    /* Initialize polyphase resampler if rate conversion needed */
    if (actual_rate != rate) {
        /* Calculate max samples per callback (based on buffer size) */
        size_t max_samples = ctx->buffer_size / ctx->sample_size;
        if (max_samples < 16384)
            max_samples = 16384;

        r = cf32_resampler_init(&ctx->resampler, actual_rate, rate, max_samples);
        if (r == 0) {
            ctx->needs_resampling = 1;
            if (verbose)
                print_logf(LOG_NOTICE, "HydraSDR",
                           "Polyphase resampler initialized: %u Hz -> %u Hz (L=%d, M=%d)",
                           actual_rate, rate,
                           ctx->resampler.up_factor, ctx->resampler.down_factor);
        } else {
            print_log(LOG_WARNING, "HydraSDR", "Failed to initialize resampler, using raw rate");
            ctx->needs_resampling = 0;
        }
    } else {
        ctx->needs_resampling = 0;
    }

    if (verbose)
        print_logf(LOG_NOTICE, "HydraSDR", "Sample rate: HW=%u Hz, output=%u Hz",
                   actual_rate, ctx->needs_resampling ? rate : actual_rate);

    return 0;
}

static uint32_t sdr_get_sample_rate_hydrasdr(sdr_dev_t *dev)
{
    if (!dev || !dev->hydrasdr_ctx)
        return 0;

    hydrasdr_ctx_t *ctx = (hydrasdr_ctx_t *)dev->hydrasdr_ctx;
    return ctx->current_samplerate;
}

static int sdr_set_auto_gain_hydrasdr(sdr_dev_t *dev, int verbose)
{
    if (!dev || !dev->hydrasdr_ctx)
        return -1;

    hydrasdr_ctx_t *ctx = (hydrasdr_ctx_t *)dev->hydrasdr_ctx;

    return hydrasdr_enable_agc(ctx, verbose);
}

static int sdr_set_tuner_gain_hydrasdr(sdr_dev_t *dev, char const *gain_str, int verbose)
{
    if (!dev || !dev->hydrasdr_ctx)
        return -1;

    hydrasdr_ctx_t *ctx = (hydrasdr_ctx_t *)dev->hydrasdr_ctx;

    int gain = atoi(gain_str);
    if (gain == 0) {
        /* Explicit -g 0: always enable AGC regardless of -t settings */
        return sdr_set_auto_gain_hydrasdr(dev, verbose);
    }

    return hydrasdr_set_total_gain(ctx, gain, verbose);
}

static int sdr_apply_settings_hydrasdr(sdr_dev_t *dev, char const *sdr_settings, int verbose)
{
    if (!dev || !dev->hydrasdr_ctx)
        return -1;

    if (!sdr_settings || !*sdr_settings)
        return 0;

    hydrasdr_ctx_t *ctx = (hydrasdr_ctx_t *)dev->hydrasdr_ctx;
    int r = 0;

    /* Parse settings string */
    char settings_copy[256];
    snprintf(settings_copy, sizeof(settings_copy), "%s", sdr_settings);
    char *settings_p = settings_copy;

    char *name, *value;
    while (getkwargs(&settings_p, &name, &value)) {
        if (strcmp(name, "biastee") == 0 || strcmp(name, "bias_tee") == 0) {
            if (ctx->info.features & HYDRASDR_CAP_BIAS_TEE) {
                int bias = atobv(value, 1);
                r = hydrasdr_set_rf_bias(ctx->dev, (uint8_t)bias);
                if (r == HYDRASDR_SUCCESS && verbose)
                    print_logf(LOG_NOTICE, "HydraSDR", "Bias tee %s",
                               bias ? "enabled" : "disabled");
            } else {
                print_log(LOG_WARNING, "HydraSDR", "Bias tee not supported by this device");
            }
        } else if (strcmp(name, "decimation") == 0 || strcmp(name, "hd") == 0) {
            int mode = atobv(value, 1);
            r = hydrasdr_set_decimation_mode(ctx->dev,
                mode ? HYDRASDR_DEC_MODE_HIGH_DEFINITION : HYDRASDR_DEC_MODE_LOW_BANDWIDTH);
            if (r == HYDRASDR_SUCCESS && verbose)
                print_logf(LOG_NOTICE, "HydraSDR", "Decimation mode: %s",
                           mode ? "high definition (10 MSPS IQ)" : "low bandwidth");
        } else if (strcmp(name, "bandwidth") == 0) {
            if (ctx->info.features & HYDRASDR_CAP_BANDWIDTH) {
                uint32_t bw = atouint32_metric(value, "-t bandwidth= ");
                r = hydrasdr_set_bandwidth(ctx->dev, bw);
                if (r == HYDRASDR_SUCCESS && verbose)
                    print_logf(LOG_NOTICE, "HydraSDR", "Bandwidth set to %u Hz", bw);
            } else {
                print_log(LOG_WARNING, "HydraSDR", "Bandwidth control not supported");
            }
        } else if (strcmp(name, "lna") == 0
                || strcmp(name, "vga") == 0
                || strcmp(name, "mixer") == 0
                || strcmp(name, "sensitivity") == 0
                || strcmp(name, "linearity") == 0) {
            /* Map setting name to gain type */
            hydrasdr_gain_type_t type;
            const char *label;
            if (strcmp(name, "lna") == 0) {
                type = HYDRASDR_GAIN_TYPE_LNA;
                label = "LNA";
            } else if (strcmp(name, "vga") == 0) {
                type = HYDRASDR_GAIN_TYPE_VGA;
                label = "VGA";
            } else if (strcmp(name, "mixer") == 0) {
                type = HYDRASDR_GAIN_TYPE_MIXER;
                label = "Mixer";
            } else if (strcmp(name, "sensitivity") == 0) {
                type = HYDRASDR_GAIN_TYPE_SENSITIVITY;
                label = "Sensitivity";
            } else {
                type = HYDRASDR_GAIN_TYPE_LINEARITY;
                label = "Linearity";
            }

            /* Check if this gain type is actually managed */
            int max_val = hydrasdr_get_gain_max(ctx, type);
            if (max_val <= 0) {
                print_logf(LOG_WARNING, "HydraSDR",
                           "%s gain not available on this device", label);
            } else {
                int gain_val = atoi(value);
                if (gain_val < 0) gain_val = 0;
                if (gain_val > max_val) gain_val = max_val;
                r = hydrasdr_set_gain(ctx->dev, type, (uint8_t)gain_val);
                if (r == HYDRASDR_SUCCESS) {
                    ctx->manual_gain_set = 1;
                    ctx->gains_set |= (1u << type);
                    if (verbose)
                        print_logf(LOG_NOTICE, "HydraSDR",
                                   "%s gain set to %d (range 0-%d)",
                                   label, gain_val, max_val);
                } else {
                    print_logf(LOG_WARNING, "HydraSDR",
                               "Failed to set %s gain: %s",
                               label, hydrasdr_error_name(r));
                }
            }
        } else {
            print_logf(LOG_WARNING, "HydraSDR", "Unknown setting: %s", name);
        }
    }

    return r;
}

static int sdr_start_hydrasdr(sdr_dev_t *dev, sdr_event_cb_t cb, void *ctx, uint32_t buf_num, uint32_t buf_len)
{
    if (!dev || !dev->hydrasdr_ctx)
        return -1;

    hydrasdr_ctx_t *hctx = (hydrasdr_ctx_t *)dev->hydrasdr_ctx;

    /* Allocate buffer */
    size_t buffer_size = (size_t)buf_num * buf_len;
    if (hctx->buffer_size != buffer_size) {
        free(hctx->buffer);
        hctx->buffer = malloc(buffer_size);
        if (!hctx->buffer) {
            WARN_MALLOC("sdr_start_hydrasdr()");
            return -1;
        }
        hctx->buffer_size = buffer_size;
        hctx->buffer_pos = 0;
    }

    /* Store callback info */
    hctx->cb = cb;
    hctx->cb_ctx = ctx;
    hctx->streaming = 1;

    /* Start streaming */
    int r = hydrasdr_start_rx(hctx->dev, hydrasdr_sample_callback, hctx);
    if (r != HYDRASDR_SUCCESS) {
        print_logf(LOG_ERROR, "HydraSDR", "Failed to start streaming: %s",
                   hydrasdr_error_name(r));
        hctx->streaming = 0;
        free(hctx->buffer);
        hctx->buffer = NULL;
        hctx->buffer_size = 0;
        return -1;
    }

    return 0;
}

static int sdr_stop_hydrasdr(sdr_dev_t *dev)
{
    if (!dev || !dev->hydrasdr_ctx)
        return -1;

    hydrasdr_ctx_t *ctx = (hydrasdr_ctx_t *)dev->hydrasdr_ctx;

    ctx->streaming = 0;

    int r = hydrasdr_stop_rx(ctx->dev);
    if (r != HYDRASDR_SUCCESS) {
        print_logf(LOG_WARNING, "HydraSDR", "Failed to stop streaming: %s",
                   hydrasdr_error_name(r));
        return -1;
    }

    return 0;
}

#endif /* HYDRASDR */

/* Public API */

int sdr_open(sdr_dev_t **out_dev, char const *dev_query, int verbose)
{
    /* rtl_tcp input (remote SDR over network) */
    if (dev_query && !strncmp(dev_query, "rtl_tcp", 7))
        return rtltcp_open(out_dev, dev_query, verbose);

#ifdef HYDRASDR
    /* HydraSDR: use by default or if explicitly requested */
    if (!dev_query || !strncmp(dev_query, "hydrasdr", 8) ||
        *dev_query == ':' || (*dev_query >= '0' && *dev_query <= '9'))
        return sdr_open_hydrasdr(out_dev, dev_query, verbose);
#endif

#if !defined(HYDRASDR)
    if (verbose)
        print_log(LOG_ERROR, __func__, "No input drivers (HydraSDR) compiled in.");
    return -1;
#endif

    print_log(LOG_ERROR, __func__, "No matching input driver found.");
    return -1;
}

int sdr_close(sdr_dev_t *dev)
{
    if (!dev)
        return -1;

    int ret = sdr_stop(dev);

    if (dev->rtl_tcp)
        ret = rtltcp_close(dev->rtl_tcp);

#ifdef HYDRASDR
    if (dev->hydrasdr_ctx)
        ret = sdr_close_hydrasdr(dev);
#endif

#ifdef THREADS
    pthread_mutex_destroy(&dev->lock);
#endif

    free(dev->dev_info);
    free(dev->buffer);
    free(dev);
    return ret;
}

char const *sdr_get_dev_info(sdr_dev_t *dev)
{
    if (!dev)
        return NULL;

    return dev->dev_info;
}

int sdr_get_sample_size(sdr_dev_t *dev)
{
    if (!dev)
        return 0;

    return dev->sample_size;
}

int sdr_get_sample_signed(sdr_dev_t *dev)
{
    if (!dev)
        return 0;

    return dev->sample_signed;
}

sdr_sample_format_t sdr_get_sample_format(sdr_dev_t *dev)
{
    if (!dev)
        return SDR_SAMPLE_CU8;

    return dev->sample_format;
}

int sdr_set_center_freq(sdr_dev_t *dev, uint32_t freq, int verbose)
{
    if (!dev)
        return -1;

#ifdef THREADS
    if (pthread_equal(dev->thread, pthread_self())) {
        fprintf(stderr, "%s: must not be called from acquire callback!\n", __func__);
        return -1;
    }
#endif

    int r = -1;

#ifdef HYDRASDR
    if (dev->hydrasdr_ctx) {
        r = sdr_set_center_freq_hydrasdr(dev, freq, verbose);
        dev->center_frequency = freq;
        return r;
    }
#endif

    if (dev->rtl_tcp) {
        dev->rtl_tcp_freq = freq;
        r = rtltcp_command(dev, RTLTCP_SET_FREQ, freq);
        if (verbose) {
            if (r < 0)
                print_log(LOG_WARNING, __func__, "Failed to set center freq.");
            else
                print_logf(LOG_NOTICE, "SDR", "Tuned to %s.", nice_freq(sdr_get_center_freq(dev)));
        }
    }

#ifdef THREADS
    pthread_mutex_lock(&dev->lock);
#endif
    dev->center_frequency = freq;
#ifdef THREADS
    pthread_mutex_unlock(&dev->lock);
#endif

    return r;
}

uint32_t sdr_get_center_freq(sdr_dev_t *dev)
{
    if (!dev)
        return 0;

#ifdef HYDRASDR
    if (dev->hydrasdr_ctx)
        return sdr_get_center_freq_hydrasdr(dev);
#endif

    if (dev->rtl_tcp)
        return dev->rtl_tcp_freq;

    return 0;
}

int sdr_set_freq_correction(sdr_dev_t *dev, int ppm, int verbose)
{
    if (!dev)
        return -1;

#ifdef THREADS
    if (pthread_equal(dev->thread, pthread_self())) {
        fprintf(stderr, "%s: must not be called from acquire callback!\n", __func__);
        return -1;
    }
#endif

    int r = -1;

    if (dev->rtl_tcp)
        r = rtltcp_command(dev, RTLTCP_SET_FREQ_CORRECTION, ppm);

    if (verbose) {
        if (r < 0)
            print_log(LOG_WARNING, __func__, "Failed to set frequency correction.");
        else
            print_logf(LOG_NOTICE, "SDR", "Frequency correction set to %d ppm.", ppm);
    }
    return r;
}

int sdr_set_auto_gain(sdr_dev_t *dev, int verbose)
{
    if (!dev)
        return -1;

#ifdef THREADS
    if (pthread_equal(dev->thread, pthread_self())) {
        fprintf(stderr, "%s: must not be called from acquire callback!\n", __func__);
        return -1;
    }
#endif

    int r = -1;

#ifdef HYDRASDR
    if (dev->hydrasdr_ctx) {
        hydrasdr_ctx_t *ctx = (hydrasdr_ctx_t *)dev->hydrasdr_ctx;
        /*
         * Default auto gain — skip AGC if manual gain was set via -t,
         * so that e.g. "-t linearity=10" is not overridden by AGC.
         */
        if (ctx->manual_gain_set) {
            if (verbose)
                print_log(LOG_NOTICE, "HydraSDR",
                          "Manual gain set via -t, skipping default AGC");
            return 0;
        }
        return sdr_set_auto_gain_hydrasdr(dev, verbose);
    }
#endif

    if (dev->rtl_tcp) {
        r = rtltcp_command(dev, RTLTCP_SET_GAIN_MODE, 0);
        if (verbose) {
            if (r < 0)
                print_log(LOG_WARNING, __func__, "Failed to enable automatic gain.");
            else
                print_log(LOG_NOTICE, "SDR", "Tuner gain set to Auto.");
        }
    }
    return r;
}

int sdr_set_tuner_gain(sdr_dev_t *dev, char const *gain_str, int verbose)
{
    if (!dev)
        return -1;

#ifdef THREADS
    if (pthread_equal(dev->thread, pthread_self())) {
        fprintf(stderr, "%s: must not be called from acquire callback!\n", __func__);
        return -1;
    }
#endif

    int r = -1;

    if (!gain_str || !*gain_str) {
        /* Enable automatic gain */
        return sdr_set_auto_gain(dev, verbose);
    }

#ifdef HYDRASDR
    if (dev->hydrasdr_ctx)
        return sdr_set_tuner_gain_hydrasdr(dev, gain_str, verbose);
#endif

    int gain = (int)(atof(gain_str) * 10); /* tenths of a dB */
    if (gain == 0) {
        /* Enable automatic gain */
        return sdr_set_auto_gain(dev, verbose);
    }

    if (dev->rtl_tcp) {
        r = rtltcp_command(dev, RTLTCP_SET_GAIN_MODE, 1)
                || rtltcp_command(dev, RTLTCP_SET_GAIN, gain);
        if (verbose) {
            if (r < 0)
                print_log(LOG_WARNING, __func__, "Failed to set tuner gain.");
            else
                print_logf(LOG_NOTICE, "SDR", "Tuner gain set to %f dB.", gain / 10.0);
        }
    }

    return r;
}

int sdr_set_antenna(sdr_dev_t *dev, char const *antenna_str, int verbose)
{
    if (!dev)
        return -1;

    POSSIBLY_UNUSED(verbose);
    POSSIBLY_UNUSED(antenna_str);

    /* Antenna selection not supported in HydraSDR */
    print_log(LOG_WARNING, __func__, "Antenna selection not available for HydraSDR devices");

    return -1;
}

int sdr_set_sample_rate(sdr_dev_t *dev, uint32_t rate, int verbose)
{
    if (!dev)
        return -1;

#ifdef THREADS
    if (pthread_equal(dev->thread, pthread_self())) {
        fprintf(stderr, "%s: must not be called from acquire callback!\n", __func__);
        return -1;
    }
#endif

    int r = -1;

#ifdef HYDRASDR
    if (dev->hydrasdr_ctx) {
        r = sdr_set_sample_rate_hydrasdr(dev, rate, verbose);
        dev->sample_rate = sdr_get_sample_rate_hydrasdr(dev);
        return r;
    }
#endif

    if (dev->rtl_tcp) {
        dev->rtl_tcp_rate = rate;
        r = rtltcp_command(dev, RTLTCP_SET_SAMPLE_RATE, rate);
        if (verbose) {
            if (r < 0)
                print_log(LOG_WARNING, __func__, "Failed to set sample rate.");
            else
                print_logf(LOG_NOTICE, "SDR", "Sample rate set to %u S/s.", sdr_get_sample_rate(dev));
        }
    }

#ifdef THREADS
    pthread_mutex_lock(&dev->lock);
#endif
    dev->sample_rate = rate;
#ifdef THREADS
    pthread_mutex_unlock(&dev->lock);
#endif

    return r;
}

uint32_t sdr_get_sample_rate(sdr_dev_t *dev)
{
    if (!dev)
        return 0;

#ifdef HYDRASDR
    if (dev->hydrasdr_ctx)
        return sdr_get_sample_rate_hydrasdr(dev);
#endif

    if (dev->rtl_tcp)
        return dev->rtl_tcp_rate;

    return 0;
}

int sdr_apply_settings(sdr_dev_t *dev, char const *sdr_settings, int verbose)
{
    if (!dev)
        return -1;

    POSSIBLY_UNUSED(verbose);
    int r = 0;

    if (!sdr_settings || !*sdr_settings)
        return 0;

#ifdef HYDRASDR
    if (dev->hydrasdr_ctx)
        return sdr_apply_settings_hydrasdr(dev, sdr_settings, verbose);
#endif

    if (dev->rtl_tcp) {
        while (sdr_settings && *sdr_settings) {
            char const *val = NULL;
            // rtl_tcp settings
            if (kwargs_match(sdr_settings, "direct_samp", &val)) {
                int direct_sampling = atoiv(val, 1);
                r = rtltcp_command(dev, RTLTCP_SET_DIRECT_SAMPLING, direct_sampling);
            }
            else if (kwargs_match(sdr_settings, "offset_tune", &val)) {
                int offset_tuning = atobv(val, 1);
                r = rtltcp_command(dev, RTLTCP_SET_OFFSET_TUNING, offset_tuning);
            }
            else if (kwargs_match(sdr_settings, "digital_agc", &val)) {
                int digital_agc = atobv(val, 1);
                r = rtltcp_command(dev, RTLTCP_SET_AGC_MODE, digital_agc);
            }
            else if (kwargs_match(sdr_settings, "biastee", &val)) {
                int biastee = atobv(val, 1);
                r = rtltcp_command(dev, RTLTCP_SET_BIAS_TEE, biastee);
            }
            else {
                print_logf(LOG_ERROR, __func__, "Unknown rtl_tcp setting: %s", sdr_settings);
                return -1;
            }
            sdr_settings = kwargs_skip(sdr_settings);
        }
        return r;
    }

    print_log(LOG_WARNING, __func__, "sdr settings not available."); // no open device

    return -1;
}

/**
 * AGC-to-stage mapping table.
 * Maps each AGC gain type to the stage it controls.
 * Add entries here when new AGC types are added to the firmware.
 */
static const struct {
    hydrasdr_gain_type_t agc_type;
    hydrasdr_gain_type_t stage_type;
} agc_stage_map[] = {
    { HYDRASDR_GAIN_TYPE_LNA_AGC,   HYDRASDR_GAIN_TYPE_LNA },
    { HYDRASDR_GAIN_TYPE_MIXER_AGC, HYDRASDR_GAIN_TYPE_MIXER },
};

/**
 * Check if a gain stage is under AGC control.
 * Uses the locally tracked agc_enabled bitmask (set by hydrasdr_enable_agc).
 */
static int hydrasdr_stage_has_agc_on(hydrasdr_ctx_t *ctx, hydrasdr_gain_type_t stage_type)
{
    for (int i = 0; i < (int)(sizeof(agc_stage_map) / sizeof(agc_stage_map[0])); i++) {
        if (agc_stage_map[i].stage_type == stage_type
                && (ctx->agc_enabled & (1u << agc_stage_map[i].agc_type)))
            return 1;
    }
    return 0;
}

void sdr_show_gain_state(sdr_dev_t *dev)
{
    if (!dev)
        return;

#ifdef HYDRASDR
    if (dev->hydrasdr_ctx) {
        hydrasdr_ctx_t *ctx = (hydrasdr_ctx_t *)dev->hydrasdr_ctx;

        static const struct {
            hydrasdr_gain_type_t type;
            const char *name;
        } stage_types[] = {
            { HYDRASDR_GAIN_TYPE_LNA,    "LNA" },
            { HYDRASDR_GAIN_TYPE_RF,     "RF" },
            { HYDRASDR_GAIN_TYPE_MIXER,  "Mixer" },
            { HYDRASDR_GAIN_TYPE_FILTER, "Filter" },
            { HYDRASDR_GAIN_TYPE_VGA,    "VGA" },
        };

        /*
         * Apply midpoint default for gains not explicitly set.
         * Stages under AGC are left alone. For manual-only stages
         * (e.g. VGA which has no AGC), the firmware power-on default
         * is often max — set to midpoint for a balanced starting point.
         */
        for (int i = 0; i < (int)(sizeof(stage_types) / sizeof(stage_types[0])); i++) {
            hydrasdr_gain_type_t t = stage_types[i].type;
            int max_val = hydrasdr_get_gain_max(ctx, t);
            if (max_val <= 0)
                continue;
            if (ctx->gains_set & (1u << t))
                continue; /* explicitly set */
            if (hydrasdr_stage_has_agc_on(ctx, t))
                continue; /* AGC controls it */

            int mid = (max_val + 1) / 2;
            if (hydrasdr_set_gain(ctx->dev, t, (uint8_t)mid) == HYDRASDR_SUCCESS)
                ctx->gains_set |= (1u << t);
        }

        /* Print effective per-stage gains */
        fprintf(stderr, "[HydraSDR] Active gain: ");
        int first = 1;
        for (int i = 0; i < (int)(sizeof(stage_types) / sizeof(stage_types[0])); i++) {
            int max_val = hydrasdr_get_gain_max(ctx, stage_types[i].type);
            int has_agc = hydrasdr_stage_has_agc_on(ctx, stage_types[i].type);

            /* Show stage if it has manual range OR AGC is active */
            if (max_val <= 0 && !has_agc)
                continue;

            if (has_agc) {
                /* AGC is controlling this stage — value is dynamic */
                fprintf(stderr, "%s%s=AGC",
                        first ? "" : ", ", stage_types[i].name);
            } else {
                /* Manual or midpoint default — show fixed value */
                hydrasdr_gain_info_t gi;
                if (hydrasdr_get_gain(ctx->dev, stage_types[i].type, &gi) != HYDRASDR_SUCCESS)
                    continue;
                fprintf(stderr, "%s%s=%d/%d",
                        first ? "" : ", ", stage_types[i].name,
                        gi.value, max_val);
            }
            first = 0;
        }
        fprintf(stderr, "\n");
        return;
    }
#endif
}

int sdr_activate(sdr_dev_t *dev)
{
    if (!dev)
        return -1;

    /* HydraSDR doesn't need explicit stream activation */
    return 0;
}

int sdr_deactivate(sdr_dev_t *dev)
{
    if (!dev)
        return -1;

    /* HydraSDR doesn't need explicit stream deactivation */
    return 0;
}

int sdr_reset(sdr_dev_t *dev, int verbose)
{
    if (!dev)
        return -1;

    POSSIBLY_UNUSED(verbose);

    /* HydraSDR doesn't need buffer reset */
    return 0;
}

int sdr_start_sync(sdr_dev_t *dev, sdr_event_cb_t cb, void *ctx, uint32_t buf_num, uint32_t buf_len)
{
    if (!dev)
        return -1;

    if (buf_num == 0)
        buf_num = SDR_DEFAULT_BUF_NUMBER;
    if (buf_len == 0)
        buf_len = SDR_DEFAULT_BUF_LENGTH;

#ifdef HYDRASDR
    if (dev->hydrasdr_ctx)
        return sdr_start_hydrasdr(dev, cb, ctx, buf_num, buf_len);
#endif

    if (dev->rtl_tcp)
        return rtltcp_read_loop(dev, cb, ctx, buf_num, buf_len);

    return -1;
}

int sdr_stop_sync(sdr_dev_t *dev)
{
    if (!dev)
        return -1;

#ifdef HYDRASDR
    if (dev->hydrasdr_ctx)
        return sdr_stop_hydrasdr(dev);
#endif

    if (dev->rtl_tcp) {
        dev->running = 0;
        return 0;
    }

    return -1;
}

void sdr_redirect_logging(void)
{
    /* No-op: HydraSDR/libhydrasdr uses its own logging */
}

/* threading */

#ifdef THREADS
static THREAD_RETURN THREAD_CALL acquire_thread(void *arg)
{
    sdr_dev_t *dev = arg;
    print_log(LOG_DEBUG, __func__, "acquire_thread enter...");

    int r = sdr_start_sync(dev, dev->async_cb, dev->async_ctx, dev->buf_num, dev->buf_len);
    // if (cfg->verbosity > 1)
    print_log(LOG_DEBUG, __func__, "acquire_thread async stop...");

    if (r < 0) {
        print_logf(LOG_ERROR, "SDR", "async read failed (%d).", r);
    }

//    sdr_event_t ev = {
//            .ev  = SDR_EV_QUIT,
//    };
//    dev->async_cb(&ev, dev->async_ctx);

    print_log(LOG_DEBUG, __func__, "acquire_thread done...");
    return (THREAD_RETURN)(intptr_t)r;
}

int sdr_start(sdr_dev_t *dev, sdr_event_cb_t async_cb, void *async_ctx, uint32_t buf_num, uint32_t buf_len)
{
    if (!dev)
        return -1;

    dev->async_cb = async_cb;
    dev->async_ctx = async_ctx;
    dev->buf_num = buf_num;
    dev->buf_len = buf_len;

#ifndef _WIN32
    // Block all signals from the worker thread
    sigset_t sigset;
    sigset_t oldset;
    sigfillset(&sigset);
    pthread_sigmask(SIG_SETMASK, &sigset, &oldset);
#endif
    int r = pthread_create(&dev->thread, NULL, acquire_thread, dev);
#ifndef _WIN32
    pthread_sigmask(SIG_SETMASK, &oldset, NULL);
#endif
    if (r) {
        fprintf(stderr, "%s: error in pthread_create, rc: %d\n", __func__, r);
    }
    return r;
}

int sdr_stop(sdr_dev_t *dev)
{
    if (!dev)
        return -1;

    if (pthread_equal(dev->thread, pthread_self())) {
        fprintf(stderr, "%s: must not be called from acquire callback!\n", __func__);
        return -1;
    }

    print_log(LOG_DEBUG, __func__, "EXITING...");
    pthread_mutex_lock(&dev->lock);
    if (dev->exit_acquire) {
        pthread_mutex_unlock(&dev->lock);
        print_log(LOG_DEBUG, __func__, "Already exiting.");
        return 0;
    }
    dev->exit_acquire = 1; // for rtl_tcp
    sdr_stop_sync(dev); // for rtlsdr
    pthread_mutex_unlock(&dev->lock);

    print_log(LOG_DEBUG, __func__, "JOINING...");
    int r = pthread_join(dev->thread, NULL);
    if (r) {
        fprintf(stderr, "%s: error in pthread_join, rc: %d\n", __func__, r);
    }

    print_log(LOG_DEBUG, __func__, "EXITED.");
    return r;
}
#else
int sdr_start(sdr_dev_t *dev, sdr_event_cb_t cb, void *ctx, uint32_t buf_num, uint32_t buf_len)
{
    UNUSED(dev);
    UNUSED(cb);
    UNUSED(ctx);
    UNUSED(buf_num);
    UNUSED(buf_len);
    print_log(LOG_ERROR, __func__, "hydrasdr_433 compiled without thread support, SDR inputs not available.");
    return -1;
}
int sdr_stop(sdr_dev_t *dev)
{
    UNUSED(dev);
    return -1;
}
#endif
