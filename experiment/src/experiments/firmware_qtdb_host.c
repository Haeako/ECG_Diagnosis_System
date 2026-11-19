/* Host adapter: calls the production detector without reimplementing it. */
#include "ecg_signal.h"
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif
EXPORT unsigned run_detector(const int *adc, unsigned length, unsigned *peaks)
{
    unsigned count = 0;
    ecg_signal_init(0);
    for (unsigned i = 0; i < length; ++i) {
        ecg_record_t result = ecg_signal_process(adc[i],
            (uint32_t)(((uint64_t)i * 1000U + 180U) / 360U));
        if (result.is_peak) peaks[count++] = result.r_peak_timestamp_ms;
    }
    return count;
}
