#include "wim_xiang_tflm.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

extern const unsigned char _binary_wim_xiang_int8_tflite_start[] asm("_binary_wim_xiang_int8_tflite_start");
extern const unsigned char _binary_wim_xiang_int8_tflite_end[] asm("_binary_wim_xiang_int8_tflite_end");

namespace {
constexpr size_t kMaxSamples = 3600;
constexpr size_t kArenaBytes = 8 * 1024;
alignas(16) uint8_t arena[kArenaBytes];

bool moving_average(const float *src, float *dst, size_t n, size_t width)
{
    const int left = static_cast<int>((width - 1U) / 2U);
    for (size_t i = 0; i < n; ++i) {
        float sum = 0.0f;
        for (size_t k = 0; k < width; ++k) {
            const int idx = static_cast<int>(i) + left - static_cast<int>(k);
            if (idx >= 0 && static_cast<size_t>(idx) < n) sum += src[idx];
        }
        dst[i] = sum / static_cast<float>(width);
    }
    return true;
}

void normalize(const float *src, size_t n, TfLiteTensor *tensor)
{
    float lo = src[0], hi = src[0];
    for (size_t i = 1; i < n; ++i) { lo = std::min(lo, src[i]); hi = std::max(hi, src[i]); }
    const float scale = tensor->params.scale;
    const int zero = tensor->params.zero_point;
    int8_t *dst = tensor->data.int8;
    const float denom = hi - lo + 1e-7f;
    for (size_t i = 0; i < n; ++i) {
        const float normalized = -1.0f + 2.0f * (src[i] - lo) / denom;
        const int q = static_cast<int>(std::lrintf(normalized / scale)) + zero;
        dst[i] = static_cast<int8_t>(std::max(-128, std::min(127, q)));
    }
}

} // namespace

extern "C" size_t wim_xiang_tflm_detect_360(const float *x, size_t n,
                                               uint16_t *peaks, size_t capacity,
                                               wim_xiang_workspace_t *w,
                                               size_t *arena_bytes)
{
    if (!x || !peaks || !w || n < 360 || n > kMaxSamples || capacity == 0) return 0;
    const auto *model = tflite::GetModel(_binary_wim_xiang_int8_tflite_start);
    if (model->version() != TFLITE_SCHEMA_VERSION) return 0;
    static tflite::MicroMutableOpResolver<8> resolver;
    static bool registered = false;
    if (!registered) {
        if (resolver.AddAdd() != kTfLiteOk || resolver.AddConcatenation() != kTfLiteOk ||
            resolver.AddConv2D() != kTfLiteOk || resolver.AddFullyConnected() != kTfLiteOk ||
            resolver.AddLogistic() != kTfLiteOk || resolver.AddMaxPool2D() != kTfLiteOk ||
            resolver.AddMul() != kTfLiteOk || resolver.AddReshape() != kTfLiteOk) return 0;
        registered = true;
    }
    static tflite::MicroInterpreter interpreter(model, resolver, arena, sizeof(arena));
    static bool allocated = false;
    if (!allocated) {
        if (interpreter.AllocateTensors() != kTfLiteOk) return 0;
        allocated = true;
    }
    if (arena_bytes) *arena_bytes = sizeof(arena);
    TfLiteTensor *a = interpreter.input(0);
    TfLiteTensor *b = interpreter.input(1);
    TfLiteTensor *part = a->dims->data[1] == 56 ? a : b;
    TfLiteTensor *object = part == a ? b : a;
    TfLiteTensor *output = interpreter.output(0);

    for (size_t i = 0; i + 1U < n; ++i) w->derivative[i] = x[i + 1U] - x[i];
    w->derivative[n - 1U] = w->derivative[n - 2U];
    moving_average(x, w->smoothed_derivative, n, 10U);
    for (size_t i = 0; i + 1U < n; ++i) w->salience[i] = w->smoothed_derivative[i + 1U] - w->smoothed_derivative[i];
    w->salience[n - 1U] = w->salience[n - 2U];

    memset(w->integral, 0, n * sizeof(float));
    for (size_t center = 112; center < n - 168; center += 3) {
        normalize(w->derivative + center - 23, 56, part);
        normalize(w->salience + center - 112, 280, object);
        if (interpreter.Invoke() != kTfLiteOk) return 0;
        const float score = (output->data.int8[0] - output->params.zero_point) * output->params.scale;
        w->integral[center] = score;
    }
    moving_average(w->integral, w->salience, n, 40U);
    float mean = 0.0f;
    for (size_t i = 0; i < n; ++i) mean += w->salience[i];
    mean /= static_cast<float>(n);
    const float threshold = mean * 0.8f;
    size_t count = 0;
    for (size_t i = 1; i + 1U < n && count < capacity; ++i) {
        if (w->salience[i] < threshold || w->salience[i] < w->salience[i - 1] || w->salience[i] <= w->salience[i + 1]) continue;
        if (count && i - peaks[count - 1U] < 98U) {
            if (w->salience[i] > w->salience[peaks[count - 1U]]) peaks[count - 1U] = static_cast<uint16_t>(i);
        } else peaks[count++] = static_cast<uint16_t>(i);
    }
    return count;
}
