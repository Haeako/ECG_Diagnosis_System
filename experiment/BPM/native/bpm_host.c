/* BPM/QRS-only replay. No AF, SQI, transport or peripheral dependencies. */
#include <stdint.h>
#include "components/bpm/ExtractFeature.h"
#ifdef _WIN32
#define API __declspec(dllexport)
#else
#define API
#endif
API int bpm_replay(const int32_t *adc, uint32_t n, uint32_t *peaks,
                   uint32_t peak_capacity, uint32_t *bpm, uint32_t bpm_capacity,
                   uint32_t *counts)
{
    uint32_t np = 0, nb = 0, next_bpm = 5000;
    ecg_signal_init(0);
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t t = (uint32_t)(((uint64_t)i * 1000 + 180) / 360);
        ecg_record_t r = ecg_signal_process(adc[i], t);
        if (t >= next_bpm) {
            if (nb >= bpm_capacity) return -2;
            bpm[2 * nb] = t;
            bpm[2 * nb + 1] = r.bpm;
            ++nb;
            next_bpm += 5000;
        }
        if (r.is_peak) {
            if (np >= peak_capacity) return -1;
            peaks[np++] = r.r_peak_timestamp_ms;
        }
    }
    counts[0] = np;
    counts[1] = nb;
    return 0;
}
