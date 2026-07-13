#include "ring_buffer.h"
#include <stdbool.h>
#include <stdio.h>

RingbufHandle_t sd_buf_handle = NULL;

bool ring_buffer_init(void) {
    sd_buf_handle = xRingbufferCreate(2 * 16384, RINGBUF_TYPE_NOSPLIT);

    if (sd_buf_handle == NULL) {
        printf("Failed to create ECG SD ring buffer\n");
        return false;
    }

    printf("ECG SD ring buffer created successfully\n");
    return true;
}
