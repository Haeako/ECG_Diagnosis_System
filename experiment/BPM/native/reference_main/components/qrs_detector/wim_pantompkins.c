#include "wim_pantompkins.h"

#include <stdbool.h>
#include <string.h>

#define PT_SECTIONS 6U
#define PT_MIN_DISTANCE 98U /* ceil(0.272 * 360), as scipy.find_peaks does */

/* scipy.signal.butter(6, [10.5/(360/2), 24/(360/2)], btype="band", output="sos"). */
static const float band_sos[PT_SECTIONS][6] = {
    {1.74147024033e-06f, 3.48294048066e-06f, 1.74147024033e-06f, 1.0f, -1.68736237720f, 0.773717867753f},
    {1.0f, 2.0f, 1.0f, 1.0f, -1.67862940033f, 0.806137977136f},
    {1.0f, 2.0f, 1.0f, 1.0f, -1.76211535030f, 0.816833992030f},
    {1.0f, -2.0f, 1.0f, 1.0f, -1.84999156958f, 0.888971731676f},
    {1.0f, -2.0f, 1.0f, 1.0f, -1.75793904161f, 0.920352354364f},
    {1.0f, -2.0f, 1.0f, 1.0f, -1.92910068677f, 0.962723633955f},
};

static void band_filter(float *x, size_t n, float scale)
{
    float section_scale = scale;
    for (size_t s = 0U; s < PT_SECTIONS; ++s) {
        const float *c = band_sos[s];
        const float dc = (c[0] + c[1] + c[2]) / (c[3] + c[4] + c[5]);
        const float zi0 = (c[1] - c[4] * c[0] + c[2] - c[5] * c[0]) /
                          (c[3] + c[4] + c[5]);
        const float zi1 = c[2] - c[5] * c[0] - c[5] * zi0;
        float z1 = zi0 * section_scale;
        float z2 = zi1 * section_scale;
        for (size_t i = 0U; i < n; ++i) {
            const float in = x[i];
            const float out = c[0] * in + z1;
            z1 = c[1] * in - c[4] * out + z2;
            z2 = c[2] * in - c[5] * out;
            x[i] = out;
        }
        section_scale *= dc;
    }
}

static bool band_filtfilt(const float *input, float *output, size_t n,
                          wim_pt_workspace_t *ws)
{
    const size_t pad = 39U; /* scipy sosfiltfilt default for six SOS sections */
    if (n <= pad || pad > WIM_PT_PAD) return false;
    const size_t length = n + 2U * pad;
    float *forward = ws->work_a;
    float *reverse = ws->work_b;
    for (size_t i = 0U; i < pad; ++i) {
        forward[i] = 2.0f * input[0] - input[pad - i];
        forward[pad + n + i] = 2.0f * input[n - 1U] - input[n - 2U - i];
    }
    memcpy(forward + pad, input, n * sizeof(float));
    band_filter(forward, length, forward[0]);
    for (size_t i = 0U; i < length; ++i) reverse[i] = forward[length - 1U - i];
    band_filter(reverse, length, reverse[0]);
    for (size_t i = 0U; i < n; ++i) output[i] = reverse[length - 1U - (pad + i)];
    return true;
}

static void derivative_filter(float *x, size_t n, float scale)
{
    /* scipy's [0,-1,-2,2,1] / (8/360) derivative FIR. */
    static const float b[5] = {0.0f, -45.0f, -90.0f, 90.0f, 45.0f};
    float z1 = 0.0f, z2 = 0.0f, z3 = 0.0f, z4 = 0.0f;
    z1 = (b[1] + b[2] + b[3] + b[4]) * scale;
    z2 = (b[2] + b[3] + b[4]) * scale;
    z3 = (b[3] + b[4]) * scale;
    z4 = b[4] * scale;
    for (size_t i = 0U; i < n; ++i) {
        const float in = x[i];
        const float out = b[0] * in + z1;
        z1 = b[1] * in + z2;
        z2 = b[2] * in + z3;
        z3 = b[3] * in + z4;
        z4 = b[4] * in;
        x[i] = out;
    }
}

static bool derivative_filtfilt(const float *input, float *output, size_t n,
                                wim_pt_workspace_t *ws)
{
    const size_t pad = 15U;
    if (n <= pad || pad > WIM_PT_PAD) return false;
    const size_t length = n + 2U * pad;
    float *forward = ws->work_a;
    float *reverse = ws->work_b;
    for (size_t i = 0U; i < pad; ++i) {
        forward[i] = 2.0f * input[0] - input[pad - i];
        forward[pad + n + i] = 2.0f * input[n - 1U] - input[n - 2U - i];
    }
    memcpy(forward + pad, input, n * sizeof(float));
    derivative_filter(forward, length, forward[0]);
    for (size_t i = 0U; i < length; ++i) reverse[i] = forward[length - 1U - i];
    derivative_filter(reverse, length, reverse[0]);
    for (size_t i = 0U; i < n; ++i) output[i] = reverse[length - 1U - (pad + i)];
    return true;
}

static int compare_height(uint16_t a, uint16_t b, const float *energy)
{
    if (energy[a] < energy[b]) return -1;
    if (energy[a] > energy[b]) return 1;
    return (a > b) - (a < b);
}

static void sift_down(uint16_t *items, size_t root, size_t end, const float *energy)
{
    while (root * 2U + 1U < end) {
        size_t child = root * 2U + 1U;
        if (child + 1U < end && compare_height(items[child], items[child + 1U], energy) < 0) ++child;
        if (compare_height(items[root], items[child], energy) >= 0) return;
        const uint16_t tmp = items[root]; items[root] = items[child]; items[child] = tmp;
        root = child;
    }
}

static void sort_by_height(uint16_t *items, size_t n, const float *energy)
{
    for (size_t start = n / 2U; start > 0U; --start) sift_down(items, start - 1U, n, energy);
    for (size_t end = n; end > 1U; --end) {
        const uint16_t tmp = items[0]; items[0] = items[end - 1U]; items[end - 1U] = tmp;
        sift_down(items, 0U, end - 1U, energy);
    }
}

size_t wim_pt_detect_360(const float *samples, size_t count,
                         uint16_t *peaks, size_t peak_capacity,
                         wim_pt_workspace_t *ws)
{
    if (samples == NULL || peaks == NULL || ws == NULL || count < 64U ||
        count > WIM_PT_MAX_SAMPLES || peak_capacity == 0U) return 0U;
    if (!band_filtfilt(samples, ws->energy, count, ws) ||
        !derivative_filtfilt(ws->energy, ws->energy, count, ws)) return 0U;

    float mean_energy = 0.0f;
    for (size_t i = 0U; i < count; ++i) {
        ws->energy[i] *= ws->energy[i];
        mean_energy += ws->energy[i];
    }
    const float threshold = 3.5f * mean_energy / (float)count;

    /* The upstream function also builds moving-integrated and noise arrays,
       but never uses them to form its returned peak list. */
    size_t candidates = 0U;
    size_t i = 1U;
    while (i + 1U < count) {
        if (ws->energy[i] < threshold || ws->energy[i] <= ws->energy[i - 1U]) {
            ++i;
            continue;
        }
        size_t right = i;
        while (right + 1U < count && ws->energy[right + 1U] == ws->energy[i]) ++right;
        if (right + 1U < count && ws->energy[right] > ws->energy[right + 1U] &&
            candidates < WIM_PT_MAX_CANDIDATES) {
            const uint16_t peak = (uint16_t)((i + right) / 2U);
            if (ws->energy[peak] >= threshold) ws->candidates[candidates++] = peak;
        }
        i = right + 1U;
    }
    if (candidates == 0U) return 0U;
    sort_by_height(ws->candidates, candidates, ws->energy);
    memset(ws->blocked, 0, count * sizeof(ws->blocked[0]));
    size_t selected = 0U;
    for (size_t k = candidates; k > 0U && selected < peak_capacity &&
         selected < WIM_PT_MAX_PEAKS; --k) {
        const size_t pos = ws->candidates[k - 1U];
        if (ws->blocked[pos] != 0U) continue;
        ws->selected[selected++] = (uint16_t)pos;
        const size_t start = pos + 1U > PT_MIN_DISTANCE ? pos + 1U - PT_MIN_DISTANCE : 0U;
        const size_t end = pos + PT_MIN_DISTANCE < count ? pos + PT_MIN_DISTANCE : count - 1U;
        for (size_t j = start; j <= end; ++j) ws->blocked[j] = 1U;
    }
    for (size_t a = 1U; a < selected; ++a) {
        const uint16_t value = ws->selected[a];
        size_t b = a;
        while (b > 0U && ws->selected[b - 1U] > value) {
            ws->selected[b] = ws->selected[b - 1U];
            --b;
        }
        ws->selected[b] = value;
    }
    memcpy(peaks, ws->selected, selected * sizeof(peaks[0]));
    return selected;
}
