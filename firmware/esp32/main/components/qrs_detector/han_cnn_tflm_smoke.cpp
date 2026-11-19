#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_task_wdt.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/kernels/kernel_util.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

extern const unsigned char _binary_han_cnn_int8_tflite_start[] asm("_binary_han_cnn_int8_tflite_start");
extern const unsigned char _binary_han_cnn_int8_tflite_end[] asm("_binary_han_cnn_int8_tflite_end");

namespace {
constexpr size_t kArenaBytes = 260U * 1024U;
static uint8_t *tensor_arena;
static const char *TAG = "HAN_CNN_TFLM";

TfLiteStatus TilePrepare(TfLiteContext *context, TfLiteNode *node)
{
    const TfLiteEvalTensor *input = tflite::micro::GetEvalInput(context, node, 0);
    const TfLiteEvalTensor *multiples = tflite::micro::GetEvalInput(context, node, 1);
    TfLiteEvalTensor *output = tflite::micro::GetEvalOutput(context, node, 0);
    if (!input || !multiples || !output || !input->dims || !output->dims ||
        input->dims->size != output->dims->size || multiples->dims->size != 1 ||
        multiples->dims->data[0] != input->dims->size || input->type != output->type) {
        if (context->ReportError) context->ReportError(context, "Han TFLM TILE: invalid tensor shapes");
        return kTfLiteError;
    }
    for (int axis = 0; axis < input->dims->size; ++axis) {
        const int32_t multiple = multiples->data.i32[axis];
        if (multiple < 1 || output->dims->data[axis] != input->dims->data[axis] * multiple) {
            if (context->ReportError) context->ReportError(context, "Han TFLM TILE: invalid repeat factor");
            return kTfLiteError;
        }
    }
    return kTfLiteOk;
}

TfLiteStatus TileEval(TfLiteContext *context, TfLiteNode *node)
{
    const TfLiteEvalTensor *input = tflite::micro::GetEvalInput(context, node, 0);
    TfLiteEvalTensor *output = tflite::micro::GetEvalOutput(context, node, 0);
    size_t element_bytes = 0;
    switch (input->type) {
    case kTfLiteInt8: case kTfLiteUInt8: element_bytes = 1; break;
    case kTfLiteInt16: element_bytes = 2; break;
    case kTfLiteInt32: case kTfLiteFloat32: element_bytes = 4; break;
    default:
        if (context->ReportError) context->ReportError(context, "Han TFLM TILE: unsupported element type");
        return kTfLiteError;
    }

    size_t output_elements = 1;
    for (int axis = 0; axis < output->dims->size; ++axis) {
        output_elements *= static_cast<size_t>(output->dims->data[axis]);
    }
    for (size_t output_index = 0; output_index < output_elements; ++output_index) {
        size_t remainder = output_index;
        size_t input_index = 0;
        size_t input_stride = 1;
        for (int axis = output->dims->size - 1; axis >= 0; --axis) {
            const size_t output_dim = static_cast<size_t>(output->dims->data[axis]);
            const size_t input_dim = static_cast<size_t>(input->dims->data[axis]);
            const size_t coordinate = remainder % output_dim;
            remainder /= output_dim;
            input_index += (coordinate % input_dim) * input_stride;
            input_stride *= input_dim;
        }
        memcpy(output->data.raw + output_index * element_bytes,
               input->data.raw + input_index * element_bytes, element_bytes);
    }
    return kTfLiteOk;
}

const TFLMRegistration kTileRegistration = {
    nullptr, nullptr, TilePrepare, TileEval, nullptr,
    tflite::BuiltinOperator_TILE, nullptr
};

// This ESP-IDF TFLM release has no public AddTile() helper. Expose our small
// builtin implementation through the resolver interface instead of calling
// MicroMutableOpResolver's private AddBuiltin(). TILE has no builtin options.
class HanOpResolver : public tflite::MicroMutableOpResolver<6> {
public:
    const TFLMRegistration *FindOp(tflite::BuiltinOperator op) const override
    {
        if (op == tflite::BuiltinOperator_TILE) return &kTileRegistration;
        return tflite::MicroMutableOpResolver<6>::FindOp(op);
    }

    tflite::TfLiteBridgeBuiltinParseFunction GetOpDataParser(
        tflite::BuiltinOperator op) const override
    {
        if (op == tflite::BuiltinOperator_TILE) return nullptr;
        return tflite::MicroMutableOpResolver<6>::GetOpDataParser(op);
    }
};
} // namespace

extern "C" void app_main(void)
{
    (void)esp_task_wdt_deinit(); // isolated benchmark only; prevent long CNN calls starving IDLE watchdog
    const size_t heap_before_arena = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    tensor_arena = static_cast<uint8_t *>(heap_caps_malloc(
        kArenaBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!tensor_arena) {
        ESP_LOGE(TAG, "internal tensor arena allocation failed bytes=%u free_internal=%u",
                 static_cast<unsigned>(kArenaBytes), static_cast<unsigned>(heap_before_arena));
        return;
    }
    const size_t model_bytes = static_cast<size_t>(_binary_han_cnn_int8_tflite_end -
                                                   _binary_han_cnn_int8_tflite_start);
    ESP_LOGI(TAG, "MODEL bytes=%u arena_bytes=%u free_internal_before=%u free_internal_after=%u",
             static_cast<unsigned>(model_bytes), static_cast<unsigned>(kArenaBytes),
             static_cast<unsigned>(heap_before_arena),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
    const tflite::Model *model = tflite::GetModel(_binary_han_cnn_int8_tflite_start);
    if (!model || model->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "model schema mismatch");
        return;
    }

    static HanOpResolver resolver;
    if (resolver.AddAdd() != kTfLiteOk || resolver.AddConv2D() != kTfLiteOk ||
        resolver.AddLogistic() != kTfLiteOk || resolver.AddMaxPool2D() != kTfLiteOk ||
        resolver.AddMul() != kTfLiteOk || resolver.AddReshape() != kTfLiteOk) {
        ESP_LOGE(TAG, "operator registration failed");
        return;
    }

    static tflite::MicroInterpreter interpreter(model, resolver, tensor_arena, sizeof(tensor_arena));
    const size_t heap_before = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    if (interpreter.AllocateTensors() != kTfLiteOk) {
        ESP_LOGE(TAG, "AllocateTensors failed free_heap=%u", static_cast<unsigned>(heap_before));
        return;
    }
    TfLiteTensor *input = interpreter.input(0);
    TfLiteTensor *output = interpreter.output(0);
    if (!input || !output || input->type != kTfLiteInt8 || output->type != kTfLiteInt8 ||
        input->bytes != 5000U || output->bytes != 5000U) {
        ESP_LOGE(TAG, "unexpected model input/output type or dimensions");
        return;
    }
    for (size_t i = 0; i < input->bytes; ++i) {
        const int centered = static_cast<int>((i * 37U + 19U) % 255U) - 127;
        input->data.int8[i] = static_cast<int8_t>(centered);
    }
    ESP_LOGI(TAG, "ALLOCATED input_bytes=%u output_bytes=%u free_heap_before=%u free_heap_after=%u",
             static_cast<unsigned>(input->bytes), static_cast<unsigned>(output->bytes),
             static_cast<unsigned>(heap_before),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT)));

    uint64_t total_us = 0;
    uint64_t max_us = 0;
    int8_t checksum = 0;
    for (int repeat = 0; repeat < 3; ++repeat) {
        const int64_t start = esp_timer_get_time();
        const TfLiteStatus status = interpreter.Invoke();
        const uint64_t elapsed = static_cast<uint64_t>(esp_timer_get_time() - start);
        if (status != kTfLiteOk) {
            ESP_LOGE(TAG, "Invoke failed repeat=%d elapsed_us=%llu", repeat,
                     static_cast<unsigned long long>(elapsed));
            return;
        }
        total_us += elapsed;
        max_us = std::max(max_us, elapsed);
        checksum = static_cast<int8_t>(checksum + output->data.int8[(repeat * 997) % output->bytes]);
        ESP_LOGI(TAG, "INVOKE repeat=%d elapsed_us=%llu", repeat,
                 static_cast<unsigned long long>(elapsed));
    }
    ESP_LOGI(TAG, "RESULT method=WIM_Han_CNN_TFLM_INT8 repeats=3 p50_us=%llu max_us=%llu "
             "model_bytes=%u arena_bytes=%u free_heap_after=%u checksum=%d",
             static_cast<unsigned long long>(total_us / 3U),
             static_cast<unsigned long long>(max_us), static_cast<unsigned>(model_bytes),
             static_cast<unsigned>(kArenaBytes),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT)), checksum);
}
