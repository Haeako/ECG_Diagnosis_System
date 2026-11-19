#ifndef __RING_BUFFER_H__
#define __RING_BUFFER_H__

#include <stdbool.h>
#include "freertos/ringbuf.h"

extern RingbufHandle_t sd_buf_handle;

bool ring_buffer_init(void);
bool ring_buffer_reset(void);

#endif
