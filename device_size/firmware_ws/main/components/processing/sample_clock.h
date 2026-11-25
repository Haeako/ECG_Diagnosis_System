#pragma once
#include <stdint.h>
/* Absolute rational deadlines: rounding never accumulates across samples. */
static inline uint64_t ecg_sample_ticks(uint64_t ordinal,uint32_t resolution,uint32_t rate) {
 return (ordinal/rate)*resolution+((ordinal%rate)*resolution)/rate;
}
static inline uint64_t ecg_sample_us(uint64_t ordinal,uint32_t rate) {
 return ecg_sample_ticks(ordinal,1000000U,rate);
}
