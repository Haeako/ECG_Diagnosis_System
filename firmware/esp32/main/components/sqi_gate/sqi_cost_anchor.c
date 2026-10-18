#include "sqi_cost_anchor.h"

#include <stdint.h>

static volatile float cost_sink;

#ifdef SQI_COST_ANCHOR_BSQI
#include "sqi_bsqi.h"
#include "sqi_xqrs.h"
static sqi_bsqi_state_t cost_state;
static sqi_xqrs_workspace_t xqrs_cost_workspace;
#endif

#ifdef SQI_COST_ANCHOR_LOGISTIC
#include "sqi_logistic_runtime.h"
static sqi_logistic_runtime_t logistic_cost_state;
#endif

#ifdef SQI_COST_ANCHOR_LOGISTIC_FULL
#include "sqi_logistic_runtime.h"
#include "sqi_xqrs.h"
static sqi_logistic_runtime_t logistic_full_cost_state;
static sqi_xqrs_workspace_t logistic_full_xqrs_workspace;
#endif

#ifdef SQI_COST_ANCHOR_ZHAO
#include "sqi_zhao.h"
static sqi_zhao_state_t zhao_cost_state;
#endif

#ifdef SQI_COST_ANCHOR_KRAMER
#include "sqi_kramer.h"
static sqi_kramer_state_t kramer_cost_state;
#endif

#ifdef SQI_COST_ANCHOR_KRAMER_FULL
#include "sqi_kramer_full.h"
static sqi_kramer_full_t kramer_full_cost_state;
#endif

void sqi_cost_anchor_run(void)
{
#ifdef SQI_COST_ANCHOR_BSQI
    sqi_bsqi_reset(&cost_state);
    (void)sqi_bsqi_push_primary(&cost_state, 1000U);
    (void)sqi_bsqi_push_secondary(&cost_state, 1000U);
    size_t detected = sqi_xqrs_detect_250(xqrs_cost_workspace.filtered, 100U,
                                          xqrs_cost_workspace.filtered_candidates,
                                          SQI_XQRS_MAX_PEAKS,
                                          &xqrs_cost_workspace);
    cost_sink = sqi_bsqi_finalize(&cost_state, 50U) + (float)detected
              + (float)sqi_xqrs_static_bytes();
#endif
#ifdef SQI_COST_ANCHOR_LOGISTIC
    sqi_logistic_reset(&logistic_cost_state);
    for (uint32_t i = 0U; i < 8U; ++i) (void)sqi_logistic_push(&logistic_cost_state, (float)i);
    sqi_logistic_result_t result = sqi_logistic_finalize(&logistic_cost_state, 360U);
    cost_sink = result.probability + (float)sqi_logistic_static_bytes();
#endif
#ifdef SQI_COST_ANCHOR_LOGISTIC_FULL
    sqi_logistic_reset(&logistic_full_cost_state);
    for (uint32_t i = 0U; i < 100U; ++i)
        (void)sqi_logistic_push(&logistic_full_cost_state, (float)i);
    size_t full_detected = sqi_xqrs_detect_250(
        logistic_full_xqrs_workspace.filtered, 100U,
        logistic_full_xqrs_workspace.filtered_candidates,
        SQI_XQRS_MAX_PEAKS, &logistic_full_xqrs_workspace);
    sqi_logistic_result_t full_result = sqi_logistic_finalize(
        &logistic_full_cost_state, SQI_XQRS_FS);
    cost_sink = full_result.probability + (float)full_detected
              + (float)sqi_xqrs_static_bytes();
#endif
#ifdef SQI_COST_ANCHOR_ZHAO
    sqi_zhao_reset(&zhao_cost_state);
    for (uint32_t i = 0U; i < 8U; ++i) {
        (void)sqi_zhao_push(&zhao_cost_state, (float)i);
    }
    sqi_zhao_result_t result = sqi_zhao_finalize(&zhao_cost_state, 360U);
    cost_sink = result.score + (float)sqi_zhao_static_bytes();
#endif
#ifdef SQI_COST_ANCHOR_KRAMER
    sqi_kramer_reset(&kramer_cost_state);
    sqi_kramer_push(&kramer_cost_state, 1.0f);
    sqi_kramer_result_t result = sqi_kramer_finalize(&kramer_cost_state, 10U, false);
    cost_sink = result.score + (float)sqi_kramer_static_bytes();
#endif
#ifdef SQI_COST_ANCHOR_KRAMER_FULL
    sqi_kramer_full_reset(&kramer_full_cost_state);
    (void)sqi_kramer_full_push(&kramer_full_cost_state, 1.0f);
    uint16_t kramer_full_peaks = 0U;
    float kramer_full_ratio = 0.0f;
    sqi_kramer_result_t kramer_full_result = sqi_kramer_full_finalize(
        &kramer_full_cost_state, &kramer_full_peaks, &kramer_full_ratio);
    cost_sink = kramer_full_result.score + kramer_full_ratio
              + (float)kramer_full_peaks + (float)sqi_kramer_full_static_bytes();
#endif
}
