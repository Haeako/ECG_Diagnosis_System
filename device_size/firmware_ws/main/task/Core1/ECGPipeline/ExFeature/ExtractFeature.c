#include "ExtractFeature.h"
#include "components/qrs_detector/fast_qrs_port.h"
#include "components/filter/SBfilter.h"
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

typedef struct {
    float b0, b1, b2, a1, a2;
    float z1, z2;
} biquad_t;

static SBfilterType notch_filter;
static biquad_t highpass, lowpass;
static fast_qrs_workspace_t *workspace;
static float context_samples[ECG_WINDOW_SAMPLES];
static uint16_t peaks[FAST_QRS_MAX_PEAKS];
static size_t samples;
static bool ready;
static uint16_t bpm;
static ecg_signal_window_t completed;

static float biquad_push(biquad_t *f, float x)
{
    float y = f->b0*x + f->z1;
    f->z1 = f->b1*x - f->a1*y + f->z2;
    f->z2 = f->b2*x - f->a2*y;
    return y;
}

static int16_t clamp(float x)
{
    if (x > 32767.0f) return INT16_MAX;
    if (x < -32768.0f) return INT16_MIN;
    return (int16_t)x;
}

static void ecg_signal_reset(uint32_t now_ms)
{
    samples = 0; ready = false; bpm = 0;
    memset(&completed, 0, sizeof(completed));
    completed.samples = context_samples;
    completed.peaks = peaks;
    completed.start_ms = now_ms;
    SBfilter_reset(&notch_filter);
    /* Retain the existing diagnostic QRS-band waveform for SD output. */
    highpass = (biquad_t){.b0=.940156963896f, .b1=-1.88031392779f,
        .b2=.940156963896f, .a1=-1.87672952684f, .a2=.883898328745f};
    lowpass = (biquad_t){.b0=.0200833655642f, .b1=.0401667311284f,
        .b2=.0200833655642f, .a1=-1.5610180758f, .a2=.641351538058f};
}

void ecg_signal_init(uint32_t now_ms)
{
    /* ESP32's static DRAM segment is smaller than its internal 8-bit heap.
     * Allocate once before BLE starts; no per-window allocation or fallback. */
    if (!workspace) {
#ifdef ESP_PLATFORM
        workspace = heap_caps_calloc(1,sizeof(*workspace),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
#else
        workspace = calloc(1,sizeof(*workspace));
#endif
        if (!workspace) abort();
    }
    SBfilter_init(&notch_filter);
    ecg_signal_reset(now_ms);
}

const ecg_signal_window_t *ecg_signal_completed_window(void)
{
    return ready ? &completed : NULL;
}

ecg_record_t ecg_signal_process(int raw_adc, uint32_t now_ms)
{
    if (ready) {
        memmove(context_samples, context_samples + ECG_HOP_SAMPLES,
                ECG_HOP_SAMPLES * sizeof(float));
        samples = ECG_HOP_SAMPLES;
        completed.start_ms += 5000U;
        ready = false;
    }
    int16_t centered = (int16_t)(raw_adc - ADC_BASELINE);
    float notch_input = (float)centered;
    SBfilter_writeInput(&notch_filter, notch_input);
    int16_t notched = clamp(SBfilter_readOutput(&notch_filter));
    int16_t filtered = clamp(biquad_push(&lowpass, biquad_push(&highpass, notched)));
    context_samples[samples++] = (float)notched / 200.0f;
    if (samples == ECG_WINDOW_SAMPLES) {
        memset(workspace, 0, sizeof(*workspace));
        completed.peak_count = fast_qrs_detect_360(context_samples,
            ECG_WINDOW_SAMPLES, peaks, FAST_QRS_MAX_PEAKS, workspace);
        completed.sequence++;
        uint32_t sum = 0; size_t intervals = 0;
        for (size_t j = 1; j < completed.peak_count; ++j) {
            if (peaks[j] >= ECG_HOP_SAMPLES) {
                sum += peaks[j] - peaks[j-1];
                intervals++;
            }
        }
        bpm = sum ? (uint16_t)(21600U * intervals / sum) : 0;
        ready = true;
    }
    /* Peaks are emitted separately, with their retrospective timestamps.
     * The former heuristic MA/threshold fields are reserved and stay zero. */
    return (ecg_record_t){.timestamp_ms=now_ms, .raw_adc=(uint16_t)raw_adc,
        .raw_centered=centered, .notch_49_51hz=notched, .corrected=notched,
        .filtered=filtered, .bpm=bpm};
}
