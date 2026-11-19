/* Executable contracts use explicit checks, including in Release builds. */
#include <stdio.h>
#include <stdlib.h>
#include "module_api.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "Failed: %s at %d\n", #x, __LINE__); return 1; } } while (0)
int main(void)
{
#if ECG_MODULE == 1
    ecg_signal_init(0);
    unsigned peaks = 0;
    ecg_record_t r = {0};
    for (unsigned i = 0; i < 36000; ++i) {
        unsigned phase = i % 288;
        int adc = 2048 + (phase == 108 ? 1000 : (phase == 107 || phase == 109 ? -250 : 0));
        r = ecg_signal_process(adc, (i * 1000 + 180) / 360);
        peaks += r.is_peak;
    }
    CHECK(peaks >= 120 && peaks <= 126);
    CHECK(r.bpm >= 74 && r.bpm <= 76);
    ecg_signal_reset(0);
    for (unsigned i = 0; i < 3600; ++i) {
        r = ecg_signal_process(2048, (i * 1000 + 180) / 360);
        CHECK(!r.is_peak && !r.bpm);
    }
#elif ECG_MODULE == 2
    sqi_runtime_reset(0);
    ecg_sqi_result_t r = sqi_runtime_push(0, false, 0, 0);
    CHECK(r.status == ECG_SQI_INSUFFICIENT);
    CHECK(r.score == ECG_SQI_INSUFFICIENT_SCORE);
    for (unsigned i = 1; i < 7200; ++i)
        r = sqi_runtime_push(0, false, 0, (i * 1000 + 180) / 360);
    CHECK(r.window_id > 0 && r.status == ECG_SQI_UNUSABLE);
    sqi_runtime_reset(0);
    for (unsigned i = 0; i < 7200; ++i) {
        unsigned phase = i % 288;
        int value = phase == 108 ? 1000 : (phase == 107 || phase == 109 ? -250 : 0);
        unsigned t = (i * 1000 + 180) / 360;
        r = sqi_runtime_push(value, phase == 108, t, t);
    }
    CHECK(r.window_id > 0 && r.status == ECG_SQI_USABLE);
    CHECK(r.heart_rate_bpm >= 74 && r.heart_rate_bpm <= 76);
    sqi_runtime_reset(0);
    CHECK(sqi_runtime_push(0, false, 0, 0).status == ECG_SQI_INSUFFICIENT);
#else
    af_runtime_reset();
    CHECK(!af_runtime_latest().valid);
    af_runtime_result_t r = {0};
    for (unsigned i = 1; i <= 100; ++i) r = af_runtime_push_peak(i * 800);
    CHECK(r.valid && r.window_id > 0);
    float features[5];
    af_runtime_last_features(features);
    CHECK(features[0] == 0.0f); /* pNN50 */
    CHECK(features[4] == 800.0f); /* median RR */
    CHECK(!r.raw_prediction);
    af_runtime_reset();
    CHECK(!af_runtime_latest().valid);
    for (unsigned i = 1; i <= 30; ++i) r = af_runtime_push_peak(i * 800);
    CHECK(!r.valid);
    r = af_runtime_push_peak(31 * 800);
    CHECK(r.valid && r.new_window);
    r = af_runtime_push_peak(31 * 800 + 3000);
    CHECK(!r.valid);
#endif
    return 0;
}
