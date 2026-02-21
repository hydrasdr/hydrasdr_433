/** @file
    Wideband cross-channel deduplication.

    Ring buffer of recent decode fingerprints.  Each entry stores an FNV-1a
    hash of all data_t key-value pairs, the channel center frequency, and a
    microsecond timestamp.

    A decode is suppressed (return 1) only when:
      - the hash matches a recent entry within the time window, AND
      - the frequencies differ (cross-channel duplicate).

    Same-channel repeats (same hash, same freq) are allowed through.
*/

#include "wb_dedup.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

/* FNV-1a parameters (32-bit) */
#define FNV_OFFSET_BASIS  0x811c9dc5u
#define FNV_PRIME         0x01000193u

/* Minimum frequency difference (Hz) to consider two channels as different */
#define MIN_FREQ_DIFF     1000.0f

struct wb_dedup_entry {
    uint32_t hash;
    float    freq;
    int64_t  timestamp_us;
};

struct wb_dedup {
    struct wb_dedup_entry cache[WB_DEDUP_CACHE_SIZE];
    int head;   /* next write position */
    int count;  /* entries in use (up to WB_DEDUP_CACHE_SIZE) */
};

static int64_t get_timestamp_us(void)
{
#ifdef _WIN32
    FILETIME ft;
    ULARGE_INTEGER ul;

    GetSystemTimeAsFileTime(&ft);
    ul.LowPart = ft.dwLowDateTime;
    ul.HighPart = ft.dwHighDateTime;
    /* FILETIME is 100-ns intervals since 1601-01-01 */
    return (int64_t)(ul.QuadPart / 10);
#else
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
#endif
}

static uint32_t fnv1a_hash_bytes(uint32_t h, const void *data, size_t len)
{
    const unsigned char *p = (const unsigned char *)data;

    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= FNV_PRIME;
    }
    return h;
}

static uint32_t fnv1a_hash_str(uint32_t h, const char *s)
{
    if (!s)
        return h;
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= FNV_PRIME;
    }
    return h;
}

/** Hash all key-value pairs in data_t linked list. */
static uint32_t hash_data(data_t *data)
{
    uint32_t h = FNV_OFFSET_BASIS;

    for (data_t *d = data; d; d = d->next) {
        h = fnv1a_hash_str(h, d->key);
        switch (d->type) {
        case DATA_INT:
            h = fnv1a_hash_bytes(h, &d->value.v_int, sizeof(d->value.v_int));
            break;
        case DATA_DOUBLE:
            h = fnv1a_hash_bytes(h, &d->value.v_dbl, sizeof(d->value.v_dbl));
            break;
        case DATA_STRING:
            h = fnv1a_hash_str(h, d->value.v_ptr);
            break;
        case DATA_ARRAY: {
            data_array_t *arr = d->value.v_ptr;
            if (arr) {
                h = fnv1a_hash_bytes(h, &arr->num_values,
                                     sizeof(arr->num_values));
                h = fnv1a_hash_bytes(h, &arr->type,
                                     sizeof(arr->type));
            }
            break;
        }
        default:
            h = fnv1a_hash_bytes(h, &d->type, sizeof(d->type));
            break;
        }
    }
    return h;
}

wb_dedup_t *wb_dedup_create(void)
{
    wb_dedup_t *dedup = calloc(1, sizeof(*dedup));
    if (!dedup)
        return NULL;
    return dedup;
}

void wb_dedup_free(wb_dedup_t *dedup)
{
    free(dedup);
}

int wb_dedup_check(wb_dedup_t *dedup, data_t *data, float chan_freq_hz)
{
    if (!dedup || !data)
        return 0;

    uint32_t h = hash_data(data);
    int64_t now = get_timestamp_us();
    int64_t window = WB_DEDUP_WINDOW_US;

    /* Search cache for matching hash within time window */
    for (int i = 0; i < dedup->count; i++) {
        struct wb_dedup_entry *e = &dedup->cache[i];

        if (e->hash != h)
            continue;
        if ((now - e->timestamp_us) > window)
            continue;

        /* Hash match within window */
        if (fabsf(chan_freq_hz - e->freq) > MIN_FREQ_DIFF) {
            /* Different channel — cross-channel duplicate: suppress */
            return 1;
        }
        /* Same channel — normal retransmission: allow */
    }

    /* No duplicate found — record this decode */
    struct wb_dedup_entry *slot = &dedup->cache[dedup->head];
    slot->hash = h;
    slot->freq = chan_freq_hz;
    slot->timestamp_us = now;

    dedup->head = (dedup->head + 1) % WB_DEDUP_CACHE_SIZE;
    if (dedup->count < WB_DEDUP_CACHE_SIZE)
        dedup->count++;

    return 0;
}
