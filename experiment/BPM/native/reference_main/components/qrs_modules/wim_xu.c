#include "wim_xu.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

#define PI_F 3.14159265358979323846
#define SHANNON_WINDOW 49U
#define BOX_HALF 24U
#define LOWPASS_PAD 6U

/* scipy.signal.cheby1(3, 1, [8,25], btype="band", fs=360, output="ba"). */
static const double cheby_b[7] = {
    0.0013933791080476177, 0.0, -0.0041801373241428528,
    0.0, 0.0041801373241428528, 0.0, -0.0013933791080476177,
};
static const double cheby_a[7] = {
    1.0, -5.4742464455879452, 12.700720573039145,
    -15.986065461775173, 11.514501297634054,
    -4.5009400815315379, 0.74622356172292215,
};
static const double low_b[2] = {0.017155612596462977, 0.017155612596462977};
static const double low_a[2] = {1.0, -0.96568877480707405};

static void lfilter_cheby(const double *input, double *output, size_t n)
{
    double state[6] = {0};
    for (size_t i = 0U; i < n; ++i) {
        const double x = input[i];
        const double y = cheby_b[0] * x + state[0];
        for (size_t j = 0U; j < 5U; ++j)
            state[j] = cheby_b[j + 1U] * x - cheby_a[j + 1U] * y + state[j + 1U];
        state[5] = cheby_b[6] * x - cheby_a[6] * y;
        output[i] = y;
    }
}

static void lfilter_lowpass(double *data, size_t n, double initial)
{
    const double zi = (low_b[1] - low_a[1] * low_b[0]) / (low_a[0] + low_a[1]);
    double state = zi * initial;
    for (size_t i = 0U; i < n; ++i) {
        const double x = data[i];
        const double y = low_b[0] * x + state;
        state = low_b[1] * x - low_a[1] * y;
        data[i] = y;
    }
}

static bool lowpass_filtfilt(const double *input, double *output, size_t n,
                             wim_xu_workspace_t *ws)
{
    if (n <= LOWPASS_PAD) return false;
    const size_t length = n + 2U * LOWPASS_PAD;
    double *extended = ws->scratch;
    for (size_t i = 0U; i < LOWPASS_PAD; ++i) {
        extended[i] = 2.0 * input[0] - input[LOWPASS_PAD - i];
        extended[LOWPASS_PAD + n + i] =
            2.0 * input[n - 1U] - input[n - 2U - i];
    }
    memcpy(extended + LOWPASS_PAD, input, n * sizeof(double));
    lfilter_lowpass(extended, length, extended[0]);
    for (size_t i = 0U; i < length / 2U; ++i) {
        const double tmp = extended[i];
        extended[i] = extended[length - 1U - i];
        extended[length - 1U - i] = tmp;
    }
    lfilter_lowpass(extended, length, extended[0]);
    for (size_t i = 0U; i < length / 2U; ++i) {
        const double tmp = extended[i];
        extended[i] = extended[length - 1U - i];
        extended[length - 1U - i] = tmp;
    }
    memcpy(output, extended + LOWPASS_PAD, n * sizeof(double));
    return true;
}

static double derivative_at(const double *x, size_t i, size_t n)
{
    return i + 1U < n ? x[i + 1U] - x[i] : x[n - 1U] - x[n - 2U];
}

/* Mixed-radix Cooley-Tukey FFT, factors 2/3/5 (3600 = 2^4*3^2*5^2).
 * Child transforms occupy disjoint contiguous output ranges. Each butterfly
 * gathers <=5 values before overwriting them. No allocation or quadratic DFT. */
static void fft(double *out_r, double *out_i, const double *in_r,
                const double *in_i, size_t stride, size_t n, int sign)
{
    if (n == 1U) { out_r[0]=in_r[0]; out_i[0]=in_i ? in_i[0] : 0.0; return; }
    const size_t radix = n%2U==0U ? 2U : (n%3U==0U ? 3U : 5U);
    const size_t m=n/radix;
    for(size_t q=0;q<radix;++q) {
        if(m==1U) { out_r[q]=in_r[q*stride]; out_i[q]=in_i ? in_i[q*stride] : 0.0; }
        else fft(out_r+q*m,out_i+q*m,in_r+q*stride,in_i ? in_i+q*stride : NULL,
            stride*radix,m,sign);
    }
    double roots_r[5],roots_i[5];
    static const double r3[]={1.0,-0.5,-0.5}, i3[]={0.0,0.86602540378443864676,-0.86602540378443864676};
    static const double r5[]={1.0,0.30901699437494742410,-0.80901699437494742410,-0.80901699437494742410,0.30901699437494742410};
    static const double i5[]={0.0,0.95105651629515357212,0.58778525229247312917,-0.58778525229247312917,-0.95105651629515357212};
    for(size_t j=0;j<radix;++j) {
        roots_r[j]=radix==2U ? (j==0U ? 1.0 : -1.0) : (radix==3U ? r3[j] : r5[j]);
        roots_i[j]=sign*(radix==2U ? 0.0 : (radix==3U ? i3[j] : i5[j]));
    }
    const double base_angle=sign*2.0*PI_F/(double)n;
    const double base_r=m==1U ? 1.0 : cos(base_angle),base_i=m==1U ? 0.0 : sin(base_angle);
    double step_r=1.0,step_i=0.0;
    for(size_t k=0;k<m;++k) {
        double r[5],im[5];
        double tw_r=1.0,tw_i=0.0;
        for(size_t q=0;q<radix;++q) {
            double ar=out_r[q*m+k],ai=out_i[q*m+k];
            r[q]=ar*tw_r-ai*tw_i;im[q]=ar*tw_i+ai*tw_r;
            const double next_r=tw_r*step_r-tw_i*step_i;
            tw_i=tw_r*step_i+tw_i*step_r;tw_r=next_r;
        }
        if(radix==2U) {
            out_r[k]=r[0]+r[1];out_i[k]=im[0]+im[1];
            out_r[m+k]=r[0]-r[1];out_i[m+k]=im[0]-im[1];
        } else {
        for(size_t j=0;j<radix;++j) {
            double sum_r=0.0,sum_i=0.0,wr=1.0,wi=0.0;
            for(size_t q=0;q<radix;++q) {
                sum_r+=r[q]*wr-im[q]*wi;sum_i+=r[q]*wi+im[q]*wr;
                const double next_r=wr*roots_r[j]-wi*roots_i[j];
                wi=wr*roots_i[j]+wi*roots_r[j];wr=next_r;
            }
            out_r[j*m+k]=sum_r;out_i[j*m+k]=sum_i;
        }
        }
        const double next_step=step_r*base_r-step_i*base_i;
        step_i=step_r*base_i+step_i*base_r;step_r=next_step;
    }
}

static void discrete_hilbert(wim_xu_workspace_t *ws, size_t n)
{
    fft(ws->input,ws->fft_imag,ws->filtered,NULL,1,n,-1);
    for(size_t k=0;k<n;++k) {
        const double re=ws->input[k],im=ws->fft_imag[k];
        if(k==0 || k==n/2) { ws->input[k]=0.0;ws->fft_imag[k]=0.0; }
        else if(k<n/2) { ws->input[k]=im;ws->fft_imag[k]=-re; }
        else { ws->input[k]=-im;ws->fft_imag[k]=re; }
    }
    fft(ws->filtered,ws->scratch,ws->input,ws->fft_imag,1,n,1);
    for(size_t k=0;k<n;++k) ws->filtered[k]/=(double)n;
}

size_t wim_xu_detect_360(const float *input_samples, size_t count,
                         uint16_t *peaks, size_t peak_capacity,
                         wim_xu_workspace_t *ws)
{
    if (input_samples == NULL || peaks == NULL || ws == NULL || count != WIM_XU_MAX_SAMPLES || peak_capacity == 0U) return 0U;
    for (size_t j=0; j<count; ++j) ws->input[j]=input_samples[j];
    const double *samples=ws->input;


    lfilter_cheby(samples, ws->filtered, count);
    double max_abs = 0.0;
    for (size_t i = 0U; i < count; ++i) {
        const double value = derivative_at(ws->filtered, i, count);
        const double magnitude = fabs(value);
        if (magnitude > max_abs) max_abs = magnitude;
    }
    if (!(max_abs > 0.0)) return 0U;

    for (size_t i = 0U; i < count; ++i) {
        const double normalized = derivative_at(ws->filtered, i, count) / max_abs;
        const double squared = normalized * normalized;
        ws->scratch[i] = -squared * log(squared);
    }
    for (size_t i = 0U; i < count; ++i) {
        double sum = 0.0;
        const size_t start = i > BOX_HALF ? i - BOX_HALF : 0U;
        const size_t end = i + BOX_HALF + 1U < count ? i + BOX_HALF + 1U : count;
        for (size_t j = start; j < end; ++j) sum += ws->scratch[j];
        ws->filtered[i] = sum / (double)SHANNON_WINDOW;
    }
    if (!lowpass_filtfilt(ws->filtered, ws->filtered, count, ws)) return 0U;

    discrete_hilbert(ws, count);

    size_t found = 0U;
    /* sign(angle(real+i*Hilbert)) is sign(Hilbert); no atan2 required. */
    double previous_phase = ws->filtered[0];
    for (size_t i = 1U; i < count && found < peak_capacity; ++i) {
        const double phase = ws->filtered[i];
        if (previous_phase < 0.0 && phase >= 0.0) peaks[found++] = (uint16_t)i;
        previous_phase = phase;
    }
    return found;
}
