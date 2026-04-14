#ifndef __RING_BUFFER_H__
#define __RING_BUFFER_H__

#include "freertos/ringbuf.h"

extern RingbufHandle_t buf_handle;

void ring_buffer_init(void);

#endif
