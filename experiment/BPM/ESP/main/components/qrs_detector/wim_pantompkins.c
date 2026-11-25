/* CONDITIONING REMOVED: latency/memory variant, not the audited accuracy implementation. */
#include "wim_pantompkins.h"

#include <stdbool.h>
#include <string.h>

/* Latency-only variant: conditioning stages are intentionally excluded. */
static size_t latency_copy_stage(const float *input, float *output, size_t count)
{
    if (input != output) memmove(output, input, count * sizeof(*output));
    return count;
}

#define PT_SECTIONS 6U
#define PT_MIN_DISTANCE 98U /* ceil(0.272 * 360), as scipy.find_peaks does */


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
    if (!latency_copy_stage(samples, ws->energy, count) ||
        !latency_copy_stage(ws->energy, ws->energy, count)) return 0U;

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
