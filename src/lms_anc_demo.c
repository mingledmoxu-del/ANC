/*
 * Phase 3b: LMS 自适应噪声抵消 — 白噪声版
 *
 * 和 lms_demo.c（系统辨识）的区别:
 *   lms_demo:   LMS 学习"匹配"一个未知 FIR → 看系数是否收敛到目标
 *   lms_anc:    LMS 学习"抵消"经过声学通路的噪声 → 叠加后是否静音
 *
 * 信号流:
 *   白噪声 x[n] ──→ 声学通路(plant) ──→ d[n] (耳朵听到的噪声)
 *                    │
 *                    └──→ LMS 自适应 FIR ──→ y[n] ≈ d[n]
 *                                               │
 *                              残留 e[n] = d[n] - y[n] → 0
 *
 * LMS 公式:  w[k] = leak*w[k] + mu * e[n] * x[n-k]
 *
 * 输出:
 *   noise.wav     — 经过声学通路的噪声 (不开 ANC 时听到的)
 *   antinoise.wav — LMS 生成的反噪声 -y[n]
 *   residual.wav  — 叠加后残留 e[n] = d[n] - y[n] (理想趋近静音)
 *   error.csv     — 误差收敛曲线 (Excel 画折线图)
 *
 * 编译: gcc lms_anc_demo.c -o lms_anc_demo -lm
 */

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ========== 可调参数 ========== */
#define FS            16000 /* 采样率 Hz */
#define DURATION      3.0f  /* 噪声秒数 */
#define TOTAL_SAMPLES ((int)(FS * DURATION))

#define LMS_TAPS   64      /* LMS 滤波器阶数 */
#define PLANT_TAPS 13      /* 模拟声学通路的 FIR 阶数 */
#define MU         0.005f  /* 步长: 太大会发散, 太小收敛慢 */
#define LEAK       0.9999f /* 泄漏因子: 防止系数漂移 */

/* ========== 固定 FIR (模拟声学通路) ========== */
typedef struct {
    float h[PLANT_TAPS];
    float buf[PLANT_TAPS];
    int   head;
} PlantFIR;

static void plant_init(PlantFIR *p) {
    /* 模拟声学通路: 延迟 + 衰减包络
     * 物理含义: 声音从喇叭到耳朵需要传播时间(延迟),
     *          途中能量逐渐衰减(下降沿)。 */
    float raw[] = {
        0.00f,
        0.00f,
        0.00f,
        0.00f, /* 4 采样延迟 ~0.25ms @16k */
        0.30f,
        0.50f,
        0.70f,
        0.85f,
        1.00f, /* 主脉冲上升 */
        0.60f,
        0.30f,
        0.15f,
        0.05f /* 衰减尾音 */
    };
    float sum = 0.0f;
    for (int i = 0; i < PLANT_TAPS; i++)
        sum += raw[i];
    for (int i = 0; i < PLANT_TAPS; i++) {
        p->h[i]   = raw[i] / sum;
        p->buf[i] = 0.0f;
    }
    p->head = 0;
}

static float plant_process(PlantFIR *p, float x) {
    p->buf[p->head] = x;
    float y         = 0.0f;
    for (int i = 0; i < PLANT_TAPS; i++) {
        int idx = (p->head - i + PLANT_TAPS) % PLANT_TAPS;
        y += p->h[i] * p->buf[idx];
    }
    p->head = (p->head + 1) % PLANT_TAPS;
    return y;
}

/* ========== LMS 自适应 FIR ========== */
typedef struct {
    float w[LMS_TAPS]; /* 自适应系数 (初始全 0) */
    float x[LMS_TAPS]; /* 参考信号历史 */
    int   head;
} LMSFilter;

static void lms_init(LMSFilter *f) {
    for (int i = 0; i < LMS_TAPS; i++) {
        f->w[i] = 0.0f;
        f->x[i] = 0.0f;
    }
    f->head = 0;
}

/* 输出 y[n] = sum(w[k] * x[n-k]) */
static float lms_output(LMSFilter *f, float xn) {
    f->x[f->head] = xn;
    float yn      = 0.0f;
    for (int k = 0; k < LMS_TAPS; k++) {
        int idx = (f->head - k + LMS_TAPS) % LMS_TAPS;
        yn += f->w[k] * f->x[idx];
    }
    f->head = (f->head + 1) % LMS_TAPS;
    return yn;
}

/* w[k] = leak*w[k] + mu * error * x[n-k] */
static void lms_update(LMSFilter *f, float err) {
    int prev = (f->head - 1 + LMS_TAPS) % LMS_TAPS;
    for (int k = 0; k < LMS_TAPS; k++) {
        int idx = (prev - k + LMS_TAPS) % LMS_TAPS;
        f->w[k] = LEAK * f->w[k] + MU * err * f->x[idx];
    }
}

/* ========== WAV 写入 (16-bit mono PCM) ========== */
static int write_wav(const char *fn, const float *smp, int n) {
    FILE *f = fopen(fn, "wb");
    if (!f) {
        fprintf(stderr, "无法创建 %s\n", fn);
        return -1;
    }

    int sr = FS, ch = 1, bps = 16;
    int byte_rate   = sr * ch * bps / 8;
    int block_align = ch * bps / 8;
    int data_size   = n * block_align;
    int riff_size   = 36 + data_size;

    fwrite("RIFF", 1, 4, f);
    fwrite(&riff_size, 4, 1, f);
    fwrite("WAVE", 1, 4, f);

    fwrite("fmt ", 1, 4, f);
    int fmt32 = 16;
    fwrite(&fmt32, 4, 1, f);
    short fmt16 = 1;
    fwrite(&fmt16, 2, 1, f);
    short ch16 = ch;
    fwrite(&ch16, 2, 1, f);
    int sr32 = sr;
    fwrite(&sr32, 4, 1, f);
    int br32 = byte_rate;
    fwrite(&br32, 4, 1, f);
    short ba16 = block_align;
    fwrite(&ba16, 2, 1, f);
    short bp16 = bps;
    fwrite(&bp16, 2, 1, f);

    fwrite("data", 1, 4, f);
    fwrite(&data_size, 4, 1, f);

    for (int i = 0; i < n; i++) {
        float v = smp[i];
        if (v > 1.0f)
            v = 1.0f;
        else if (v < -1.0f)
            v = -1.0f;
        short val = (short)(v * 32767.0f);
        fwrite(&val, 2, 1, f);
    }
    fclose(f);
    return 0;
}

/* ========== main ========== */
int main(void) {
    PlantFIR  plant;
    LMSFilter lms;

    float *xn       = malloc(TOTAL_SAMPLES * sizeof(float));
    float *dn       = malloc(TOTAL_SAMPLES * sizeof(float));
    float *yn       = malloc(TOTAL_SAMPLES * sizeof(float));
    float *anti     = malloc(TOTAL_SAMPLES * sizeof(float));
    float *residual = malloc(TOTAL_SAMPLES * sizeof(float));

    if (!xn || !dn || !yn || !anti || !residual) {
        fprintf(stderr, "内存分配失败\n");
        return 1;
    }

    srand((unsigned int)time(NULL));
    plant_init(&plant);
    lms_init(&lms);

    printf("========================================\n");
    printf(" Phase 3b: LMS 噪声抵消 — 白噪声版\n");
    printf("========================================\n");
    printf(" 采样率:    %d Hz\n", FS);
    printf(" 时长:      %.1f 秒 (%d 采样)\n", DURATION, TOTAL_SAMPLES);
    printf(" LMS 阶数:  %d\n", LMS_TAPS);
    printf(" 声学通路:  %d 阶\n", PLANT_TAPS);
    printf(" 步长 mu:   %.4f\n", MU);
    printf(" 泄漏因子:  %.4f\n\n", LEAK);

    /* 1. 生成白噪声 [-1, 1] */
    printf("[1/4] 生成白噪声...");
    fflush(stdout);
    for (int i = 0; i < TOTAL_SAMPLES; i++) {
        xn[i] = 2.0f * (float)rand() / (float)RAND_MAX - 1.0f;
    }
    printf(" 完成\n");

    /* 2. 白噪声通过声学通路 → dn */
    printf("[2/4] 模拟声学通路...");
    fflush(stdout);
    for (int i = 0; i < TOTAL_SAMPLES; i++) {
        dn[i] = plant_process(&plant, xn[i]);
    }
    printf(" 完成\n");

    /* 3. LMS 自适应学习抵消 */
    printf("[3/4] LMS 在线学习...");
    fflush(stdout);

    FILE *csv = fopen("error.csv", "w");
    fprintf(csv, "Sample,Time_s,Error\n");

    float rms_err = 0.0f;
    int   skip    = LMS_TAPS * 4; /* 跳过前 256 点让滤波器稳下来 */

    for (int i = 0; i < TOTAL_SAMPLES; i++) {
        float y   = lms_output(&lms, xn[i]); /* LMS 对 dn 的估计 */
        float err = dn[i] - y;               /* 误差 = 真实噪声 - 估计 */
        lms_update(&lms, err);               /* 用误差更新系数 */

        yn[i]       = y;
        anti[i]     = -y;  /* 反噪声 = 和噪声反相 */
        residual[i] = err; /* 残留 = dn + (-y) = dn - y */

        if (i % 100 == 0)
            fprintf(csv, "%d,%.6f,%.6f\n", i, (float)i / FS, err);

        if (i >= skip)
            rms_err += err * err;
    }
    fclose(csv);
    printf(" 完成\n");

    /* 统计 */
    rms_err      = sqrtf(rms_err / (float)(TOTAL_SAMPLES - skip));
    float rms_dn = 0.0f;
    for (int i = skip; i < TOTAL_SAMPLES; i++)
        rms_dn += dn[i] * dn[i];
    rms_dn = sqrtf(rms_dn / (float)(TOTAL_SAMPLES - skip));

    printf("\n 稳定期 RMS (跳过前 %d 点):\n", skip);
    printf("   噪声 RMS:        %.4f\n", rms_dn);
    printf("   残留 RMS:        %.4f\n", rms_err);
    printf("   衰减:            %.1f dB\n", 20.0f * log10f(rms_err / rms_dn));
    printf("   (理想: 负无穷 dB, 残留=0 = 完全静音)\n");

    /* 4. 写出 WAV */
    printf("\n[4/4] 写出文件...");
    fflush(stdout);
    write_wav("noise.wav", dn, TOTAL_SAMPLES);
    write_wav("antinoise.wav", anti, TOTAL_SAMPLES);
    write_wav("residual.wav", residual, TOTAL_SAMPLES);
    printf(" 完成\n");

    printf("\n========================================\n");
    printf(" 输出文件:\n");
    printf("   noise.wav     — 不开 ANC 的噪声\n");
    printf("   antinoise.wav — LMS 生成的\"反噪声\"\n");
    printf("   residual.wav  — noise+antinoise 叠加结果\n");
    printf("   error.csv     — 误差收敛曲线\n");
    printf("========================================\n");
    printf("\n试试:\n");
    printf("  1. 播放 noise.wav → 听原始噪声\n");
    printf("  2. 播放 residual.wav → 残留应该小很多\n");
    printf("  3. Excel 打开 error.csv 画误差曲线\n");
    printf("  4. 改 MU=%.4f 看收敛速度变化\n", MU);
    printf("  5. 改 LMS_TAPS=%d 看阶数影响\n\n", LMS_TAPS);

    free(xn);
    free(dn);
    free(yn);
    free(anti);
    free(residual);
    return 0;
}
