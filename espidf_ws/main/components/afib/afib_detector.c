#include "afib_detector.h"

#include <string.h>

#define AFIB_RR_WINDOW_SIZE        16
#define AFIB_MIN_RR_COUNT          12
#define AFIB_MIN_RR_MS             300
#define AFIB_MAX_RR_MS             2000
#define AFIB_PNN50_THRESHOLD       45
#define AFIB_RR_CV_THRESHOLD       10
#define AFIB_POINCARE_THRESHOLD    45
#define AFIB_ENTROPY_THRESHOLD     65
#define AFIB_IRREG_THRESHOLD       8
#define AFIB_SCORE_THRESHOLD       60
#define AFIB_SAMPEN_MIN_TOL_MS     20

typedef struct {
    uint16_t rr_ms[AFIB_RR_WINDOW_SIZE];
    uint8_t index;
    uint8_t count;
    afib_result_t result;
} afib_detector_t;

static afib_detector_t detector;

static uint16_t abs_u16_diff(uint16_t a, uint16_t b)
{
    return a > b ? (uint16_t)(a - b) : (uint16_t)(b - a);
}

static uint8_t clamp_percent_u32(uint32_t value)
{
    return value > 100U ? 100U : (uint8_t)value;
}

static uint8_t clamp_percent_i64(int64_t value)
{
    if (value <= 0) {
        return 0;
    }
    return value > 100 ? 100 : (uint8_t)value;
}

static uint16_t afib_detector_rr_at(uint8_t chronological_index)
{
    uint8_t buffer_index = chronological_index;

    if (detector.count == AFIB_RR_WINDOW_SIZE) {
        buffer_index = (uint8_t)((detector.index + chronological_index)
                                 % AFIB_RR_WINDOW_SIZE);
    }

    return detector.rr_ms[buffer_index];
}

static uint8_t afib_detector_sample_entropy_proxy(uint16_t tolerance_ms)
{
    uint16_t matches_m = 0;
    uint16_t matches_m1 = 0;

    /*
     * Lightweight Sample Entropy proxy for embedded use:
     * - compare RR templates of length 2
     * - count how many still match when extended to length 3
     * - high entropy => low continuation ratio
     *
     * Returned scale:
     *   0   = very regular
     *   100 = highly irregular / no repeated pattern
     */
    if (detector.count < 4) {
        return 0;
    }

    for (uint8_t i = 0; i + 2U < detector.count; i++) {
        for (uint8_t j = i + 1U; j + 2U < detector.count; j++) {
            if (abs_u16_diff(afib_detector_rr_at(i), afib_detector_rr_at(j))
                    <= tolerance_ms
                && abs_u16_diff(afib_detector_rr_at(i + 1U),
                                afib_detector_rr_at(j + 1U))
                       <= tolerance_ms) {
                matches_m++;
                if (abs_u16_diff(afib_detector_rr_at(i + 2U),
                                 afib_detector_rr_at(j + 2U))
                    <= tolerance_ms) {
                    matches_m1++;
                }
            }
        }
    }

    if (matches_m == 0) {
        return 100;
    }

    return clamp_percent_u32(100U - (((uint32_t)matches_m1 * 100U) / matches_m));
}

static void afib_detector_update_result(void)
{
    uint32_t rr_sum = 0;
    uint32_t mad_sum = 0;
    uint32_t diff_abs_sum = 0;
    uint64_t rr_var_sum = 0;
    int32_t diff_signed_sum = 0;
    int64_t diff_signed_var_sum = 0;
    uint8_t pnn50_count = 0;
    uint16_t mean_rr;
    int16_t mean_signed_diff;
    uint16_t tolerance_ms;
    uint8_t votes = 0;

    detector.result.rr_count = detector.count;

    if (detector.count < AFIB_MIN_RR_COUNT) {
        detector.result.status = AFIB_STATUS_UNKNOWN;
        detector.result.score = 0;
        detector.result.mean_rr_ms = 0;
        detector.result.pnn50_percent = 0;
        detector.result.rr_cv_percent = 0;
        detector.result.poincare_sd1_sd2_percent = 0;
        detector.result.sample_entropy_percent = 0;
        detector.result.irregularity_percent = 0;
        return;
    }

    for (uint8_t i = 0; i < detector.count; i++) {
        rr_sum += afib_detector_rr_at(i);
    }

    mean_rr = (uint16_t)(rr_sum / detector.count);
    detector.result.mean_rr_ms = mean_rr;

    for (uint8_t i = 0; i < detector.count; i++) {
        uint16_t rr = afib_detector_rr_at(i);

        mad_sum += abs_u16_diff(rr, mean_rr);
        rr_var_sum += (uint64_t)abs_u16_diff(rr, mean_rr)
                      * abs_u16_diff(rr, mean_rr);
        if (i > 0 && abs_u16_diff(rr, afib_detector_rr_at(i - 1U)) > 50) {
            pnn50_count++;
        }
        if (i > 0) {
            uint16_t prev_rr = afib_detector_rr_at(i - 1U);
            uint16_t diff = abs_u16_diff(rr, afib_detector_rr_at(i - 1U));
            int16_t signed_diff = (int16_t)rr - (int16_t)prev_rr;

            diff_signed_sum += signed_diff;
            diff_abs_sum += (uint32_t)(diff * 100U) / rr;
        }
    }

    detector.result.pnn50_percent =
        clamp_percent_u32(((uint32_t)pnn50_count * 100U) / (detector.count - 1U));
    detector.result.rr_cv_percent =
        mean_rr > 0
            ? clamp_percent_u32(((mad_sum / detector.count) * 100U) / mean_rr)
            : 0;
    detector.result.irregularity_percent =
        clamp_percent_u32(diff_abs_sum / (detector.count - 1U));

    mean_signed_diff = (int16_t)(diff_signed_sum / (detector.count - 1U));
    for (uint8_t i = 1; i < detector.count; i++) {
        int16_t signed_diff = (int16_t)afib_detector_rr_at(i)
                              - (int16_t)afib_detector_rr_at(i - 1U);
        int32_t centered_diff = (int32_t)signed_diff - mean_signed_diff;

        diff_signed_var_sum += (int64_t)centered_diff * centered_diff;
    }

    /*
     * Poincare approximation:
     *   SD1^2 = 0.5 * variance(diff RR)
     *   SD2^2 = 2 * variance(RR) - SD1^2
     *
     * To avoid float/sqrt, compare squared values and expose an approximate
     * SD1/SD2 percentage using SD1^2 / SD2^2. It is intentionally conservative.
     */
    {
        int64_t sd1_sq_scaled =
            diff_signed_var_sum / (int64_t)(2U * (detector.count - 1U));
        int64_t sd2_sq_scaled = (int64_t)(2U * rr_var_sum / detector.count)
                                - sd1_sq_scaled;

        detector.result.poincare_sd1_sd2_percent =
            sd2_sq_scaled > 0
                ? clamp_percent_i64((sd1_sq_scaled * 100) / sd2_sq_scaled)
                : 100;
    }

    tolerance_ms = (uint16_t)((mad_sum / detector.count) / 5U);
    if (tolerance_ms < AFIB_SAMPEN_MIN_TOL_MS) {
        tolerance_ms = AFIB_SAMPEN_MIN_TOL_MS;
    }
    detector.result.sample_entropy_percent =
        afib_detector_sample_entropy_proxy(tolerance_ms);

    detector.result.score = clamp_percent_u32(
        ((uint32_t)detector.result.pnn50_percent * 20U) / 60U
        + ((uint32_t)detector.result.rr_cv_percent * 20U) / 20U
        + ((uint32_t)detector.result.poincare_sd1_sd2_percent * 20U) / 80U
        + ((uint32_t)detector.result.sample_entropy_percent * 20U) / 100U
        + ((uint32_t)detector.result.irregularity_percent * 20U) / 20U);

    if (detector.result.pnn50_percent >= AFIB_PNN50_THRESHOLD) {
        votes++;
    }
    if (detector.result.rr_cv_percent >= AFIB_RR_CV_THRESHOLD) {
        votes++;
    }
    if (detector.result.poincare_sd1_sd2_percent >= AFIB_POINCARE_THRESHOLD) {
        votes++;
    }
    if (detector.result.sample_entropy_percent >= AFIB_ENTROPY_THRESHOLD) {
        votes++;
    }
    if (detector.result.irregularity_percent >= AFIB_IRREG_THRESHOLD) {
        votes++;
    }

    detector.result.status =
        detector.result.score >= AFIB_SCORE_THRESHOLD && votes >= 3
            ? AFIB_STATUS_SUSPECTED
            : AFIB_STATUS_NORMAL;
}

void afib_detector_init(void)
{
    afib_detector_reset();
}

void afib_detector_reset(void)
{
    memset(&detector, 0, sizeof(detector));
    detector.result.status = AFIB_STATUS_UNKNOWN;
}

afib_result_t afib_detector_process_rr(uint16_t rr_ms)
{
    if (rr_ms < AFIB_MIN_RR_MS || rr_ms > AFIB_MAX_RR_MS) {
        return detector.result;
    }

    detector.rr_ms[detector.index] = rr_ms;
    detector.index = (detector.index + 1U) % AFIB_RR_WINDOW_SIZE;
    if (detector.count < AFIB_RR_WINDOW_SIZE) {
        detector.count++;
    }

    afib_detector_update_result();
    return detector.result;
}

afib_result_t afib_detector_get_result(void)
{
    return detector.result;
}
