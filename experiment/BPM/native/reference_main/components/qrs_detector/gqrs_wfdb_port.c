#include "gqrs_wfdb_port.h"

#include <math.h>
#include <string.h>

enum { GQRS_LEARNING = 0, GQRS_RUNNING = 1, GQRS_CLEANUP = 2 };

static int64_t smv_at(const gqrs_wfdb_workspace_t *ws, int32_t t)
{
    return ws->smv[(uint32_t)t & (GQRS_RING_LENGTH - 1U)];
}

static void smv_put(gqrs_wfdb_workspace_t *ws, int32_t t, int64_t value)
{
    ws->smv[(uint32_t)t & (GQRS_RING_LENGTH - 1U)] = value;
}

static int64_t qfv_at(const gqrs_wfdb_workspace_t *ws, int32_t t)
{
    return ws->qfv[(uint32_t)t & (GQRS_RING_LENGTH - 1U)];
}

static void qfv_put(gqrs_wfdb_workspace_t *ws, int32_t t, int64_t value)
{
    ws->qfv[(uint32_t)t & (GQRS_RING_LENGTH - 1U)] = value;
}

static int32_t sample_at(gqrs_wfdb_workspace_t *ws, int32_t t)
{
    if (t < 0) {
        ws->sample_valid = 1;
        return (int32_t)nearbyint((double)ws->signal_mv[0] * ws->adc_gain
                                  + (double)ws->adc_baseline);
    }
    if (t >= ws->sample_count) {
        ws->sample_valid = 0;
        const size_t last = (size_t)ws->sample_count - 1U;
        return (int32_t)nearbyint((double)ws->signal_mv[last] * ws->adc_gain
                                  + (double)ws->adc_baseline);
    }
    ws->sample_valid = 1;
    return (int32_t)nearbyint((double)ws->signal_mv[t] * ws->adc_gain
                              + (double)ws->adc_baseline);
}

static int64_t smooth_at(gqrs_wfdb_workspace_t *ws, int32_t at_t)
{
    int32_t t = (int32_t)ws->smt;
    const int32_t dt = ws->smdt;
    int64_t v = 0;
    while (at_t > t) {
        ++t;
        if (t > ws->smt0) {
            const int64_t tmp = smv_at(ws, t - 1)
                + sample_at(ws, t + dt) + sample_at(ws, t + dt - 1)
                - sample_at(ws, t - dt) - sample_at(ws, t - dt - 1);
            smv_put(ws, t, tmp);
        } else {
            v = sample_at(ws, t);
            for (int32_t j = 1; j < dt; ++j) {
                const int64_t right = sample_at(ws, t + j);
                const int64_t left = sample_at(ws, t - j);
                v += right + left;
                smv_put(ws, t, (v << 1)
                    + sample_at(ws, t + j + 1)
                    + sample_at(ws, t - j - 1)
                    - ((int64_t)ws->smdt << 2) * 0);
            }
        }
    }
    ws->smt = t;
    return smv_at(ws, at_t);
}

static void qrs_filter(gqrs_wfdb_workspace_t *ws)
{
    int64_t dv2 = smooth_at(ws, ws->t + ws->dt4) - smv_at(ws, ws->t - ws->dt4);
    const int64_t dv1 = smv_at(ws, ws->t + ws->dt) - smv_at(ws, ws->t - ws->dt);
    int64_t dv = dv1 << 1;
    dv -= smv_at(ws, ws->t + ws->dt2) - smv_at(ws, ws->t - ws->dt2);
    dv = (dv << 1) + dv1;
    dv -= smv_at(ws, ws->t + ws->dt3) - smv_at(ws, ws->t - ws->dt3);
    dv = (dv << 1) + dv2;
    ws->v1 += dv;
    const int64_t v0 = (int64_t)((double)ws->v1 / (double)ws->v1norm);
    qfv_put(ws, ws->t, v0 * v0);
}

static void add_peak(gqrs_wfdb_workspace_t *ws, int32_t time, int64_t amp)
{
    const uint8_t index = ws->peak_ring[ws->current_peak].next;
    gqrs_peak_t *p = &ws->peak_ring[index];
    p->time = time;
    p->amp = amp;
    p->type = 0;
    ws->current_peak = index;
    ws->peak_ring[p->next].amp = 0;
}

static int peak_type(gqrs_wfdb_workspace_t *ws, uint8_t index)
{
    gqrs_peak_t *p = &ws->peak_ring[index];
    if (p->type != 0) return p->type;

    const int64_t amp = p->amp;
    int32_t t0 = p->time - (int32_t)ws->rrmin;
    const int32_t t1 = p->time + (int32_t)ws->rrmin;
    if (t0 < 0) t0 = 0;

    uint8_t q = p->prev;
    while (t0 < ws->peak_ring[q].time
           && ws->peak_ring[q].time < ws->peak_ring[ws->peak_ring[q].next].time) {
        if (ws->peak_ring[q].amp == 0) break;
        if (amp < ws->peak_ring[q].amp && peak_type(ws, q) == 1) {
            p->type = 2;
            return p->type;
        }
        q = ws->peak_ring[q].prev;
    }

    q = p->next;
    while (ws->peak_ring[q].time < t1
           && ws->peak_ring[q].time > ws->peak_ring[ws->peak_ring[q].prev].time) {
        if (ws->peak_ring[q].amp == 0) break;
        if (amp < ws->peak_ring[q].amp && peak_type(ws, q) == 1) {
            p->type = 2;
            return p->type;
        }
        q = ws->peak_ring[q].next;
    }
    p->type = 1;
    return p->type;
}

static int find_missing(gqrs_wfdb_workspace_t *ws, uint8_t r, uint8_t p)
{
    int32_t minrrerr = ws->peak_ring[p].time - ws->peak_ring[r].time;
    int selected = -1;
    uint8_t q = ws->peak_ring[r].next;
    while (ws->peak_ring[q].time < ws->peak_ring[p].time) {
        if (peak_type(ws, q) == 1) {
            const int32_t rr = ws->peak_ring[q].time - ws->peak_ring[r].time;
            int32_t error = rr - (int32_t)ws->rrmean;
            if (error < 0) error = -error;
            if (error < minrrerr) {
                minrrerr = error;
                selected = q;
            }
        }
        q = ws->peak_ring[q].next;
    }
    return selected;
}

static void save_annotation(gqrs_wfdb_workspace_t *ws, uint16_t *peaks,
                            size_t peak_capacity, int32_t time)
{
    if (ws->result_count < peak_capacity && time >= 0
        && (uint32_t)time < (uint32_t)ws->sample_count) {
        peaks[ws->result_count++] = (uint16_t)time;
    }
    ws->annot_time = time;
}

static void run_gqrs(gqrs_wfdb_workspace_t *ws, int32_t from_sample,
                     int32_t to_sample, uint16_t *peaks, size_t peak_capacity)
{
    int32_t last_peak = from_sample;
    int32_t last_qrs = from_sample;
    uint8_t r = 0U;
    int have_r = 0;
    int64_t q0, q1, q2;

    while (ws->t <= to_sample + 360) {
        if (ws->countdown < 0) {
            if (ws->sample_valid) {
                qrs_filter(ws);
            } else {
                ws->countdown = 360;
                ws->state = GQRS_CLEANUP;
            }
        } else {
            --ws->countdown;
            if (ws->countdown < 0) break;
        }

        q0 = qfv_at(ws, ws->t);
        q1 = qfv_at(ws, ws->t - 1);
        q2 = qfv_at(ws, ws->t - 2);
        if (q1 > ws->pthr && q2 < q1 && q1 >= q0 && ws->t > ws->dt4) {
            add_peak(ws, ws->t - 1, q1);
            last_peak = ws->t - 1;
            uint8_t p = ws->peak_ring[ws->current_peak].next;
            while (ws->peak_ring[p].time < ws->t - ws->rtmax) {
                if (ws->peak_ring[p].time >= ws->annot_time + ws->rrmin
                    && peak_type(ws, p) == 1) {
                    if (ws->peak_ring[p].amp > ws->qthr) {
                        int64_t rr = ws->peak_ring[p].time - ws->annot_time;
                        int q = have_r ? find_missing(ws, r, p) : -1;
                        if (rr > ws->rrmean + 2 * ws->rrdev
                            && rr > 2 * (ws->rrmean - ws->rrdev) && q >= 0) {
                            p = (uint8_t)q;
                            rr = ws->peak_ring[p].time - ws->annot_time;
                        }
                        int64_t rrd = rr - ws->rrmean;
                        if (rrd < 0) rrd = -rrd;
                        ws->rrdev += (rrd - ws->rrdev) >> 3;
                        if (rrd > ws->rrinc) rrd = ws->rrinc;
                        if (rr > ws->rrmean) ws->rrmean += rrd;
                        else ws->rrmean -= rrd;
                        if (ws->peak_ring[p].amp > ws->qthr * 4) ++ws->qthr;
                        else if (ws->peak_ring[p].amp < ws->qthr) --ws->qthr;
                        if (ws->qthr > ws->pthr * 20) ws->qthr = ws->pthr * 20;
                        last_qrs = ws->peak_ring[p].time;

                        if (ws->state == GQRS_RUNNING) {
                            save_annotation(ws, peaks, peak_capacity,
                                            ws->peak_ring[p].time - ws->dt2);
                            ws->annot_time += ws->dt2;
                        }

                        int tw = -1;
                        int64_t rtdmin = ws->rtmean;
                        uint8_t qpeak = ws->peak_ring[p].next;
                        while (ws->peak_ring[qpeak].time > ws->annot_time) {
                            const int64_t rt = ws->peak_ring[qpeak].time
                                             - ws->annot_time - ws->dt2;
                            if (rt < ws->rtmin) {
                                qpeak = ws->peak_ring[qpeak].next;
                                continue;
                            }
                            if (rt > ws->rtmax) break;
                            int64_t rtd = rt - ws->rtmean;
                            if (rtd < 0) rtd = -rtd;
                            if (rtd < rtdmin) {
                                rtdmin = rtd;
                                tw = qpeak;
                            }
                            qpeak = ws->peak_ring[qpeak].next;
                        }
                        if (tw >= 0) {
                            const int32_t tw_time = ws->peak_ring[tw].time - ws->dt2;
                            const int64_t rt = tw_time - ws->annot_time;
                            ws->rtmean += (rt - ws->rtmean) >> 4;
                            if (ws->rtmean > ws->rtmax) ws->rtmean = ws->rtmax;
                            else if (ws->rtmean < ws->rtmin) ws->rtmean = ws->rrmin;
                            ws->peak_ring[tw].type = 2;
                        }
                        r = p;
                        have_r = 1;
                    } else if (ws->t - last_qrs > ws->rrmax
                               && ws->qthr > ws->qthmin) {
                        ws->qthr -= ws->qthr >> 4;
                    }
                }
                p = ws->peak_ring[p].next;
            }
        } else if (ws->t - last_peak > ws->rrmax && ws->pthr > ws->pthmin) {
            ws->pthr -= ws->pthr >> 4;
        }
        ++ws->t;
    }

    if (ws->state == GQRS_LEARNING) return;
    uint8_t p = ws->peak_ring[ws->peak_ring[ws->current_peak].next].next;
    /* The first next above is the oldest buffered peak; visit the ring once. */
    p = ws->peak_ring[ws->current_peak].next;
    for (uint32_t guard = 0U; guard < GQRS_PEAK_RING; ++guard) {
        const uint8_t next = ws->peak_ring[p].next;
        if (ws->peak_ring[p].time >= ws->peak_ring[next].time) break;
        if (ws->peak_ring[p].time >= ws->annot_time + ws->rrmin
            && ws->peak_ring[p].time < ws->tf && peak_type(ws, p) == 1) {
            save_annotation(ws, peaks, peak_capacity, ws->peak_ring[p].time);
        }
        p = next;
    }
}

size_t gqrs_wfdb_detect_360(const float *signal_mv, size_t count,
                            uint16_t *peaks, size_t peak_capacity,
                            gqrs_wfdb_workspace_t *ws)
{
    if (signal_mv == NULL || peaks == NULL || ws == NULL || count == 0U
        || count > GQRS_MAX_SAMPLES || count + 360U + 24U >= GQRS_RING_LENGTH) {
        return 0U;
    }

    memset(ws, 0, sizeof(*ws));
    float pmin = signal_mv[0];
    float pmax = signal_mv[0];
    for (size_t i = 0U; i < count; ++i) {
        if (!isfinite(signal_mv[i])) return 0U;
        if (signal_mv[i] < pmin) pmin = signal_mv[i];
        if (signal_mv[i] > pmax) pmax = signal_mv[i];
    }
    if (pmax <= pmin) return 0U;

    const double dmin = -8388607.0;
    const double dmax = 8388607.0;
    double adc_gain = (dmax - dmin) / ((double)pmax - (double)pmin);
    double baseline_value = dmin - adc_gain * (double)pmin;
    int64_t baseline = pmin > 0.0f ? (int64_t)ceil(baseline_value)
                                  : (int64_t)floor(baseline_value);
    if (dmin != (double)baseline) adc_gain = (dmin - (double)baseline) / pmin;
    ws->signal_mv = signal_mv;
    ws->adc_gain = adc_gain;
    ws->adc_baseline = baseline;
    ws->sample_count = (int32_t)count;
    ws->tf = (int32_t)count - 1;
    ws->dt = (int32_t)(0.07 * 360.0 / 4.0);
    if (ws->dt < 1) return 0U;
    ws->dt2 = 2 * ws->dt;
    ws->dt3 = 3 * ws->dt;
    ws->dt4 = 4 * ws->dt;
    ws->smdt = ws->dt;
    ws->smt0 = ws->smdt;
    ws->v1norm = (int64_t)ws->smdt * ws->dt * 64;

    ws->rrmean = (int64_t)(60.0 / 75.0 * 360.0);
    ws->rrdev = (int64_t)(0.2 * 360.0);
    ws->rrmin = (int64_t)(0.28 * 360.0);
    ws->rrmax = (int64_t)(2.4 * 360.0);
    ws->rrinc = ws->rrmean / 40;
    if (ws->rrinc < 1) ws->rrinc = 1;
    ws->rtmin = (int64_t)(0.25 * 360.0);
    ws->rtmean = (int64_t)(0.75 * 0.35 * 360.0);
    ws->rtmax = (int64_t)(0.33 * 360.0);

    const double dv = adc_gain * 130.0 * 0.001;
    ws->pthr = (int64_t)(dv * dv / 6.0);
    ws->qthr = ws->pthr << 1;
    ws->pthmin = ws->pthr >> 2;
    ws->qthmin = (ws->pthmin << 2) / 3;

    for (uint32_t i = 0U; i < GQRS_PEAK_RING; ++i) {
        ws->peak_ring[i].next = (uint8_t)((i + 1U) % GQRS_PEAK_RING);
        ws->peak_ring[i].prev = (uint8_t)((i + GQRS_PEAK_RING - 1U) % GQRS_PEAK_RING);
    }
    ws->countdown = -1;
    ws->state = GQRS_LEARNING;
    ws->sample_valid = 0;
    ws->t = -ws->dt4;
    const int32_t tf_learn = ws->tf - ws->dt4;
    run_gqrs(ws, 0, tf_learn, peaks, peak_capacity);

    ws->countdown = -1;
    (void)sample_at(ws, ws->t);
    ws->annot_time = 0;
    for (uint32_t i = 0U; i < GQRS_PEAK_RING; ++i) {
        ws->peak_ring[i].time = 0;
        ws->peak_ring[i].type = 0;
        ws->peak_ring[i].amp = 0;
    }
    ws->current_peak = 0;
    ws->state = GQRS_RUNNING;
    ws->t = -ws->dt4;
    ws->result_count = 0U;
    run_gqrs(ws, 0, ws->tf, peaks, peak_capacity);
    return ws->result_count;
}
