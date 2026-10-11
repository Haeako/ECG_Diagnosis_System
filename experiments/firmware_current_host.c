/* Host adapter for the current production streaming detector. */
#include "task/Core1/ECGPipeline/ExFeature/ExtractFeature.h"
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif
EXPORT unsigned run_detector(const int *adc, unsigned length, unsigned *peaks_ms)
{
    unsigned count = 0;
    ecg_signal_init(0);
    for (unsigned i = 0; i < length; ++i) {
        uint32_t now_ms = (uint32_t)(((uint64_t)i * 1000U + 180U) / 360U);
        ecg_record_t result = ecg_signal_process(adc[i], now_ms);
        if (result.is_peak) peaks_ms[count++] = result.r_peak_timestamp_ms;
    }
    return count;
}
