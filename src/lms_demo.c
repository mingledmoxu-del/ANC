/*
 * Phase 4: FxLMS — 次级通路补偿
 *
 * 核心: S-path 让反噪声在到达误差麦前被扭曲。
 *   LMS  不补偿 → 梯度方向错 → 效果差甚至发散
 *   FxLMS 用 S_hat 预滤波参考信号 → 补偿延迟 → 效果明显更好
 *
 * 编译: gcc fxlms_demo.c -o fxlms_demo -lm
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LMS_TAPS   64
#define PLANT_TAPS 13
#define SPATH_TAPS 5
#define MU_LMS     0.00002f /* LMS 不补偿 S, 步长必须极小才不爆炸 */
#define MU_FXLMS   0.002f   /* FxLMS 有补偿, 可以大步长 */
#define LEAK       0.9999f
#define FS         16000
#define DURATION   5.0f

/* WAV */
typedef struct {
    int      sr, ch, bps, ds, ns;
    int16_t *data;
} WavFile;
static int wav_read(const char *fn, WavFile *w) {
    FILE *f = fopen(fn, "rb");
    if (!f)
        return -1;
    fseek(f, 22, SEEK_SET);
    int16_t c;
    fread(&c, 2, 1, f);
    w->ch = c;
    int32_t s;
    fread(&s, 4, 1, f);
    w->sr = s;
    fseek(f, 34, SEEK_SET);
    int16_t b;
    fread(&b, 2, 1, f);
    w->bps = b;
    if (b != 16) {
        fclose(f);
        return -1;
    }
    char    t[5] = {0};
    int32_t cs;
    while (1) {
        if (fread(t, 1, 4, f) != 4) {
            fclose(f);
            return -1;
        }
        fread(&cs, 4, 1, f);
        if (!strncmp(t, "data", 4))
            break;
        fseek(f, cs, SEEK_CUR);
    }
    w->ds   = cs;
    w->ns   = cs / (w->ch * 2);
    w->data = malloc(cs);
    fread(w->data, 1, cs, f);
    fclose(f);
    return 0;
}
static int wav_write(const char *fn, int sr, int ch, const int16_t *d, int ns) {
    FILE *f = fopen(fn, "wb");
    if (!f)
        return -1;
    int br = sr * ch * 2, ba = ch * 2, ds = ns * ba, rs = 36 + ds;
    fwrite("RIFF", 1, 4, f);
    fwrite(&rs, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    int32_t f32 = 16;
    fwrite(&f32, 4, 1, f);
    int16_t a16 = 1;
    fwrite(&a16, 2, 1, f);
    int16_t c16 = ch;
    fwrite(&c16, 2, 1, f);
    int32_t s32 = sr;
    fwrite(&s32, 4, 1, f);
    int32_t b32 = br;
    fwrite(&b32, 4, 1, f);
    int16_t ba16 = ba;
    fwrite(&ba16, 2, 1, f);
    int16_t bp16 = 16;
    fwrite(&bp16, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&ds, 4, 1, f);
    fwrite(d, 1, ds, f);
    fclose(f);
    return 0;
}
static void wav_free(WavFile *w) {
    free(w->data);
}

/* FIR 基础 */
typedef struct {
    float *h, *buf;
    int    len, head;
} FIR;
static FIR fir_new(int len, const float *c) {
    FIR f;
    f.len   = len;
    f.h     = malloc(len * sizeof(float));
    f.buf   = calloc(len, sizeof(float));
    f.head  = 0;
    float s = 0;
    for (int i = 0; i < len; i++)
        s += c[i];
    for (int i = 0; i < len; i++)
        f.h[i] = c[i] / s;
    return f;
}
static float fir_step(FIR *f, float x) {
    f->buf[f->head] = x;
    float y         = 0;
    for (int i = 0; i < f->len; i++) {
        int id = (f->head - i + f->len) % f->len;
        y += f->h[i] * f->buf[id];
    }
    f->head = (f->head + 1) % f->len;
    return y;
}
static void fir_free(FIR *f) {
    free(f->h);
    free(f->buf);
}

/* LMS/FxLMS 共用的自适应滤波器 */
typedef struct {
    float *w, *x;
    int    len, head;
    float  mu, leak;
} LMS;
static LMS lms_new(int len, float mu, float leak) {
    LMS l;
    l.len  = len;
    l.mu   = mu;
    l.leak = leak;
    l.head = 0;
    l.w    = calloc(len, sizeof(float));
    l.x    = calloc(len, sizeof(float));
    return l;
}
static void lms_free(LMS *l) {
    free(l->w);
    free(l->x);
}

/* 工具 */
static void gen_wav(const char *fn) {
    int      ns = FS * DURATION;
    int16_t *d  = malloc(2 * ns * sizeof(int16_t));
    for (int i = 0; i < ns; i++) {
        float v = 1.6f * (float)rand() / RAND_MAX - 0.8f;
        if (v > 1)
            v = 1;
        else if (v < -1)
            v = -1;
        int16_t s16  = (int16_t)(v * 32767);
        d[i * 2]     = s16;
        d[i * 2 + 1] = s16;
    }
    wav_write(fn, FS, 2, d, ns);
    free(d);
    printf("生成: %s (%ds 白噪声)\n", fn, (int)DURATION);
}
static void basename_of(const char *p, char *o, int mx) {
    const char *n = p;
    for (const char *c = p; *c; c++)
        if (*c == '/' || *c == '\\')
            n = c + 1;
    int i = 0;
    while (n[i] && i < mx - 1) {
        if (n[i] == '.' && !strcasecmp(n + i, ".wav"))
            break;
        o[i] = n[i];
        i++;
    }
    o[i] = 0;
}

int main(int argc, char *argv[]) {
    const char *in;
    if (argc > 1 && !strcmp(argv[1], "--gen")) {
        gen_wav("input.wav");
        in = "input.wav";
    } else if (argc > 1)
        in = argv[1];
    else {
        printf("用法: %s <wav> 或 --gen\n", argv[0]);
        return 1;
    }

    WavFile wav;
    if (wav_read(in, &wav)) {
        fprintf(stderr, "读失败\n");
        return 1;
    }
    int N = wav.ns, fs = wav.sr, st = wav.ch;
    printf("读入: %s sr=%d ch=%d samples=%d (%.1fs)\n\n", in, fs, wav.ch, N, (float)N / fs);

    float *xn = malloc(N * sizeof(float));
    for (int i = 0; i < N; i++)
        xn[i] = wav.data[i * st] / 32768.0f;

    /* P(z): 噪声源→耳朵 */
    float pc[] = {0, 0, 0, 0, 0.3f, 0.5f, 0.7f, 0.85f, 1.0f, 0.6f, 0.3f, 0.15f, 0.05f};
    /* S(z): 扬声器→误差麦 (比P短,有延迟) */
    float sc[] = {0, 0, 0.4f, 0.7f, 1.0f};

    FIR P     = fir_new(PLANT_TAPS, pc);
    FIR S_lms = fir_new(SPATH_TAPS, sc); /* LMS 的信号通路 */
    FIR S_fx  = fir_new(SPATH_TAPS, sc); /* FxLMS 的信号通路 */
    FIR S_hat = fir_new(SPATH_TAPS, sc); /* FxLMS 的预滤波 (=S) */

    /* d[n] = P(x[n]) */
    float *dn = malloc(N * sizeof(float));
    for (int i = 0; i < N; i++)
        dn[i] = fir_step(&P, xn[i]);

    int skip = LMS_TAPS * 4;

    /* ====== LMS (无S补偿, MU极小) ====== */
    LMS    lm = lms_new(LMS_TAPS, MU_LMS, LEAK);
    float *el = malloc(N * sizeof(float));
    for (int i = 0; i < N; i++) {
        lm.x[lm.head] = xn[i];
        float y       = 0;
        for (int k = 0; k < lm.len; k++) {
            int id = (lm.head - k + lm.len) % lm.len;
            y += lm.w[k] * lm.x[id];
        }
        float ym = fir_step(&S_lms, y);
        float e  = dn[i] - ym;
        el[i]    = e;
        for (int k = 0; k < lm.len; k++) {
            int id  = (lm.head - k + lm.len) % lm.len;
            lm.w[k] = LEAK * lm.w[k] + MU_LMS * e * lm.x[id];
        }
        lm.head = (lm.head + 1) % lm.len;
    }
    float rl = 0;
    for (int i = skip; i < N; i++)
        rl += el[i] * el[i];
    rl = sqrtf(rl / (N - skip));

    /* ====== FxLMS (有S补偿) ====== */
    LMS    fx = lms_new(LMS_TAPS, MU_FXLMS, LEAK);
    float *ef = malloc(N * sizeof(float));
    /* xf_buf: 预滤波参考信号历史 (FxLMS 专用) */
    float *xf_buf = calloc(LMS_TAPS, sizeof(float));
    for (int i = 0; i < N; i++) {
        float xf        = fir_step(&S_hat, xn[i]); /* 预滤波 */
        xf_buf[fx.head] = xf;                      /* 存历史 */

        fx.x[fx.head] = xn[i];
        float y       = 0;
        for (int k = 0; k < fx.len; k++) {
            int id = (fx.head - k + fx.len) % fx.len;
            y += fx.w[k] * fx.x[id];
        }
        float ym = fir_step(&S_fx, y);
        float e  = dn[i] - ym;
        ef[i]    = e;

        /* FxLMS: 用 xf_buf (预滤波历史) 替代 x_buf */
        for (int k = 0; k < fx.len; k++) {
            int id  = (fx.head - k + fx.len) % fx.len;
            fx.w[k] = LEAK * fx.w[k] + MU_FXLMS * e * xf_buf[id];
        }
        fx.head = (fx.head + 1) % fx.len;
    }
    float rf = 0;
    for (int i = skip; i < N; i++)
        rf += ef[i] * ef[i];
    rf = sqrtf(rf / (N - skip));

    float rd = 0;
    for (int i = skip; i < N; i++)
        rd += dn[i] * dn[i];
    rd = sqrtf(rd / (N - skip));

    printf("===== LMS vs FxLMS (跳过前%d点) =====\n", skip);
    printf("                                    RMS         衰减\n");
    printf(" 噪声 (无ANC):                      %.4f\n", rd);
    printf(" LMS   (mu=%.5f, 无S补偿):         %.4f       %+.1f dB\n", MU_LMS, rl, 20 * log10f(rl / rd));
    printf(
        " FxLMS (mu=%.4f, 有S补偿):         %.4f       %+.1f dB\n", MU_FXLMS, rf, 20 * log10f(rf / rd));
    printf(" FxLMS 相对 LMS 多降:              %.1f dB\n", 20 * log10f(rf / rl));

    /* CSV */
    FILE *csv = fopen("error.csv", "w");
    if (csv) {
        fprintf(csv, "Sample,Time,LMS,FxLMS\n");
        for (int i = 0; i < N; i += 100)
            fprintf(csv, "%d,%.6f,%.6f,%.6f\n", i, (float)i / fs, el[i], ef[i]);
        fclose(csv);
    }

    /* WAV */
    char bn[256];
    basename_of(in, bn, sizeof(bn));
    char fl[512], ff[512];
    snprintf(fl, sizeof(fl), "%s_lms_residual.wav", bn);
    snprintf(ff, sizeof(ff), "%s_fxlms_residual.wav", bn);
    int16_t *bl = malloc(2 * N * sizeof(int16_t)), *bf = malloc(2 * N * sizeof(int16_t));
    for (int i = 0; i < N; i++) {
        float vl = el[i];
        if (vl > 1)
            vl = 1;
        else if (vl < -1)
            vl = -1;
        int16_t sl    = (int16_t)(vl * 32767);
        bl[i * 2]     = sl;
        bl[i * 2 + 1] = sl;
        float vf      = ef[i];
        if (vf > 1)
            vf = 1;
        else if (vf < -1)
            vf = -1;
        int16_t sf    = (int16_t)(vf * 32767);
        bf[i * 2]     = sf;
        bf[i * 2 + 1] = sf;
    }
    wav_write(fl, fs, 2, bl, N);
    wav_write(ff, fs, 2, bf, N);
    printf("\n输出: %s (LMS) / %s (FxLMS) / error.csv\n", fl, ff);

    wav_free(&wav);
    free(xn);
    free(dn);
    free(el);
    free(ef);
    free(bl);
    free(bf);
    free(xf_buf);
    fir_free(&P);
    fir_free(&S_lms);
    fir_free(&S_fx);
    fir_free(&S_hat);
    lms_free(&lm);
    lms_free(&fx);
    return 0;
}