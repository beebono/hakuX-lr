/* Copyright (C) 2026-present PortareOS */
/*
 * hakuX native core — the voices' band-limited resampler (resample.h's
 * sinc): a Kaiser-windowed sinc interpolator in float, its taps through
 * NEON. A quality setting, not a claim about the Xbox's undocumented
 * interpolation.
 *
 * At a ratio of 1 or more (a voice at or below the output's 48 kHz) the
 * filter is 32 taps, its cutoff at the input's Nyquist frequency, from a
 * table of 128 phases interpolated between: a 22.05 kHz voice keeps 10 kHz
 * within 1 dB, and at phase 0 the output is the input sample itself (unity
 * passes exactly). Below 1 (a voice pitched past 48 kHz) the filter is
 * stretched to the output's Nyquist, 32 / ratio taps, the ratio rounded
 * down to 1/128ths, at most 8 times (256 taps); past that a pitch-up
 * aliases, its rate still the one asked for (the VP's go to 1/512). The input is kept planar with 131 frames of history and read
 * 16 frames ahead of the output (up to 132 for the stretched filter), its
 * buffer 4 KB a channel pair; nothing is allocated after vp_sinc_new.
 */
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "resample_sinc.h"

#ifdef VP_HAVE_SINC
#include <arm_neon.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define HALF       16          /* zero crossings each side at a ratio of 1 or more */
#define TAPS       (2 * HALF)
#define PHASES     128         /* table phases a frame, interpolated between */
#define BETA       6.0         /* the Kaiser window's */
#define STRETCH    8           /* the filter stretched at most this much (ratio 1/8) */
#define MAX_HALF   (HALF * STRETCH)
#define HISTORY    (MAX_HALF + 4)  /* the stretched filter's reach, read 4 frames at a time */
#define BUF_FRAMES 512         /* a channel's input: history, lookahead and refills */

struct VpSinc {
    int channels;
    int len;                    /* frames in buf */
    double pos;                 /* the next output's position in buf, in input frames */
    double reach;               /* the last read's: frames before pos an output reads */
    float buf[2][BUF_FRAMES];   /* each channel's input */
};

/* The filter h(t) = sinc(t) w(t), |t| < HALF: by phase for a ratio of 1 or
 * more, g_phase[p][k] = h(k - (HALF - 1) - p / PHASES), each row summing to
 * 1 (DC passes exactly); by position for the stretched filter, g_fine[i] =
 * h(i / PHASES), then zeros for the 4 taps it reads at a time. */
static float g_phase[PHASES + 1][TAPS];
static float g_fine[HALF * PHASES + 4 * PHASES];
static pthread_once_t g_tables_once = PTHREAD_ONCE_INIT;

static double bessel_i0(double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 64 && term > sum * 1e-17; k++) {
        term *= (x / (2 * k)) * (x / (2 * k));
        sum += term;
    }
    return sum;
}

static double kernel(double t)
{
    if (fabs(t) >= HALF) {
        return 0.0;
    }
    if (t == 0.0) {
        return 1.0;
    }
    if (t == floor(t)) {
        return 0.0;             /* the zero crossings exactly */
    }
    const double w = bessel_i0(BETA * sqrt(1.0 - (t / HALF) * (t / HALF))) / bessel_i0(BETA);
    return sin(M_PI * t) / (M_PI * t) * w;
}

static void tables_init(void)
{
    for (int p = 0; p <= PHASES; p++) {
        double row[TAPS], sum = 0.0;
        for (int k = 0; k < TAPS; k++) {
            row[k] = kernel(k - (HALF - 1) - (double)p / PHASES);
            sum += row[k];
        }
        for (int k = 0; k < TAPS; k++) {
            g_phase[p][k] = (float)(row[k] / sum);
        }
    }
    for (int i = 0; i <= HALF * PHASES; i++) {
        g_fine[i] = (float)kernel((double)i / PHASES);
    }
}

VpSinc *vp_sinc_new(int channels)
{
    if (channels < 1 || channels > 2) {
        return NULL;
    }
    pthread_once(&g_tables_once, tables_init);
    VpSinc *s = calloc(1, sizeof(*s));
    if (s) {
        s->channels = channels;
        vp_sinc_reset(s);
    }
    return s;
}

/* A new sound: silence for its history, its first frame next */
void vp_sinc_reset(VpSinc *s)
{
    memset(s->buf[0], 0, sizeof(float) * HISTORY);
    memset(s->buf[1], 0, sizeof(float) * HISTORY);
    s->len = HISTORY;
    s->pos = HISTORY;
    s->reach = HALF - 1;
}

double vp_sinc_ahead(const VpSinc *s)
{
    return s->len - s->pos;
}

double vp_sinc_reach(const VpSinc *s)
{
    return s->reach;
}

void vp_sinc_delete(VpSinc *s)
{
    free(s);
}

/* The callback's next frames into buf, first dropping what the filter's
 * history no longer reaches from frame n when the buffer is near full;
 * false when it gives nothing */
static bool refill(VpSinc *s, vp_resample_cb cb, void *cb_data, long n)
{
    if (s->len > BUF_FRAMES - 64) {
        long drop = n - (HISTORY - 1);
        drop = drop > s->len ? s->len : drop;
        if (drop > 0) {
            for (int c = 0; c < 2; c++) {
                memmove(s->buf[c], s->buf[c] + drop, sizeof(float) * (size_t)(s->len - drop));
            }
            s->len -= (int)drop;
            s->pos -= (double)drop;
        }
    }
    float *in = NULL;
    long got = cb(cb_data, &in);
    if (got <= 0 || !in) {
        return false;
    }
    got = got > BUF_FRAMES - s->len ? BUF_FRAMES - s->len : got;   /* the VP's are 32 */
    float *l = s->buf[0] + s->len, *r = s->buf[1] + s->len;
    if (s->channels == 2) {
        long i = 0;
        for (; i + 4 <= got; i += 4) {
            const float32x4x2_t v = vld2q_f32(in + 2 * i);
            vst1q_f32(l + i, v.val[0]);
            vst1q_f32(r + i, v.val[1]);
        }
        for (; i < got; i++) {
            l[i] = in[2 * i];
            r[i] = in[2 * i + 1];
        }
    } else {
        memcpy(l, in, sizeof(float) * (size_t)got);
    }
    s->len += (int)got;
    return got > 0;
}

/* An output frame at a ratio of 1 or more: the 32 taps around frame n at
 * phase frac, between table rows p and p + 1 */
static void out_taps(const VpSinc *s, long n, float frac, float *out)
{
    const float ph = frac * PHASES;
    int p = (int)ph;
    p = p < PHASES ? p : PHASES - 1;
    const float32x4_t f = vdupq_n_f32(ph - (float)p);
    const float *c0 = g_phase[p], *c1 = g_phase[p + 1];
    const float *xl = s->buf[0] + n - (HALF - 1), *xr = s->buf[1] + n - (HALF - 1);
    float32x4_t l0 = vdupq_n_f32(0.0f), l1 = l0, r0 = l0, r1 = l0;
    for (int k = 0; k < TAPS; k += 8) {
        const float32x4_t a0 = vld1q_f32(c0 + k), a1 = vld1q_f32(c0 + k + 4);
        const float32x4_t k0 = vfmaq_f32(a0, vsubq_f32(vld1q_f32(c1 + k), a0), f);
        const float32x4_t k1 = vfmaq_f32(a1, vsubq_f32(vld1q_f32(c1 + k + 4), a1), f);
        l0 = vfmaq_f32(l0, k0, vld1q_f32(xl + k));
        l1 = vfmaq_f32(l1, k1, vld1q_f32(xl + k + 4));
        r0 = vfmaq_f32(r0, k0, vld1q_f32(xr + k));
        r1 = vfmaq_f32(r1, k1, vld1q_f32(xr + k + 4));
    }
    out[0] = vaddvq_f32(vaddq_f32(l0, l1));
    if (s->channels == 2) {
        out[1] = vaddvq_f32(vaddq_f32(r0, r1));
    }
}

/* 4 coefficients of the stretched filter, from g_fine[i], [i + m], [i + 2m]
 * and [i + 3m], each t of the way to the next entry */
static inline float32x4_t stretched4(int i, int m, float32x4_t t)
{
    float32x4_t a = vdupq_n_f32(g_fine[i]), b = vdupq_n_f32(g_fine[i + 1]);
    a = vld1q_lane_f32(g_fine + i + m, a, 1);
    b = vld1q_lane_f32(g_fine + i + m + 1, b, 1);
    a = vld1q_lane_f32(g_fine + i + 2 * m, a, 2);
    b = vld1q_lane_f32(g_fine + i + 2 * m + 1, b, 2);
    a = vld1q_lane_f32(g_fine + i + 3 * m, a, 3);
    b = vld1q_lane_f32(g_fine + i + 3 * m + 1, b, 3);
    return vfmaq_f32(a, vsubq_f32(b, a), t);
}

static inline float32x4_t reversed(float32x4_t v)
{
    v = vrev64q_f32(v);
    return vextq_f32(v, v, 2);
}

/* An output frame below a ratio of 1: the filter stretched by PHASES / m
 * around frame n at phase frac, from g_fine at a stride of m, 4 taps at a
 * time. Frames at or before n are k * m + frac * m into it, those after
 * k * m - frac * m: the fraction between entries is the same along each
 * side. Normalized by the coefficients' sum, so DC passes. */
static void out_stretched(const VpSinc *s, long n, double frac, int m, float *out)
{
    const double a = frac * m;
    const int ia = (int)a;
    const float ta = (float)(a - ia), tb = ta > 0.0f ? 1.0f - ta : 0.0f;
    const float *xl = s->buf[0], *xr = s->buf[1];
    float32x4_t l = vdupq_n_f32(0.0f), r = l, sum = l;
    const float32x4_t vta = vdupq_n_f32(ta), vtb = vdupq_n_f32(tb);
    long j = n;
    for (int i = ia; i < HALF * PHASES; i += 4 * m, j -= 4) {     /* n, n - 1, ... */
        const float32x4_t c = stretched4(i, m, vta);
        l = vfmaq_f32(l, c, reversed(vld1q_f32(xl + j - 3)));
        r = vfmaq_f32(r, c, reversed(vld1q_f32(xr + j - 3)));
        sum = vaddq_f32(sum, c);
    }
    j = n + 1;
    for (int i = ta > 0.0f ? m - ia - 1 : m - ia; i < HALF * PHASES; i += 4 * m, j += 4) {
        const float32x4_t c = stretched4(i, m, vtb);
        l = vfmaq_f32(l, c, vld1q_f32(xl + j));
        r = vfmaq_f32(r, c, vld1q_f32(xr + j));
        sum = vaddq_f32(sum, c);
    }
    const float total = vaddvq_f32(sum);
    out[0] = vaddvq_f32(l) / total;
    if (s->channels == 2) {
        out[1] = vaddvq_f32(r) / total;
    }
}

long vp_sinc_read(VpSinc *s, vp_resample_cb cb, void *cb_data, double ratio, long frames, float *data)
{
    const double step = 1.0 / ratio;
    /* the stretched filter's 1/128ths (rounded down: its cutoff at or under
     * the output's Nyquist), and the frames it reads past n */
    int m = ratio < 1.0 ? (int)(ratio * PHASES) : PHASES;
    m = m < PHASES / STRETCH ? PHASES / STRETCH : m;
    const long ahead = m < PHASES ? HALF * PHASES / m + 4 : HALF;
    s->reach = m < PHASES ? (double)HALF * PHASES / m : HALF - 1;
    for (long i = 0; i < frames; i++) {
        long n = (long)s->pos;
        while (n + ahead >= s->len) {
            if (!refill(s, cb, cb_data, n)) {
                return i;
            }
            n = (long)s->pos;
        }
        const double frac = s->pos - (double)n;
        float *out = data + i * s->channels;
        if (m < PHASES) {
            out_stretched(s, n, frac, m, out);
        } else {
            out_taps(s, n, (float)frac, out);
        }
        s->pos += step;
    }
    return frames;
}

#endif
