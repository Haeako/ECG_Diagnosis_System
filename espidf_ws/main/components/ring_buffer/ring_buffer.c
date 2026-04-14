#include "ring_buffer.h"
#include <stdio.h>

RingbufHandle_t buf_handle = NULL;

void ring_buffer_init(void) {
    buf_handle = xRingbufferCreate(2*16384, RINGBUF_TYPE_NOSPLIT);
    if (buf_handle == NULL) {
        printf("Failed to create ring buffer\n");
    } else {
        printf("Ring buffer created successfully\n");
    }
}