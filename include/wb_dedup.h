/** @file
    Wideband cross-channel deduplication.

    The OS-PFB channelizer uses 2x oversampling (D=M/2), so adjacent channels
    overlap in frequency.  A signal near a channel boundary is decoded by both
    channels, producing duplicate output.  This module suppresses the second
    copy while allowing normal same-channel retransmissions through.
*/

#ifndef INCLUDE_WB_DEDUP_H_
#define INCLUDE_WB_DEDUP_H_

#include "data.h"

#define WB_DEDUP_CACHE_SIZE  32
#define WB_DEDUP_WINDOW_US   500000  /* 500 ms */

typedef struct wb_dedup wb_dedup_t;

/** Create a dedup context.  Returns NULL on allocation failure. */
wb_dedup_t *wb_dedup_create(void);

/** Free a dedup context (NULL-safe). */
void wb_dedup_free(wb_dedup_t *dedup);

/** Check if data is a cross-channel duplicate.
 *  Returns 1 if duplicate (suppress), 0 if unique (forward).
 *  chan_freq_hz is the channel center frequency. */
int wb_dedup_check(wb_dedup_t *dedup, data_t *data, float chan_freq_hz);

/** Return total number of suppressed duplicates since creation. */
unsigned wb_dedup_suppressed_count(wb_dedup_t *dedup);

#endif /* INCLUDE_WB_DEDUP_H_ */
