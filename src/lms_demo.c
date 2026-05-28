/*
 * Phase 3: LMS 自适应噪声抵消 — WAV 文件版
 *
 * 信号流:
 *   WAV 文件 → x[n] ──→ 声学通路(plant) ──→ d[n] (不开 ANC 时耳朵听到的)
 *                      │
 *                      └──→ LMS 自适应 FIR ──→ y[n] ≈ d[n]
 *                                                 │
 *                                                -y[n] = 反噪声
 *                                 d[n] + (-y[n]) = 残留 e[n] → 0
 *
 * LMS 公式:  w[k] = leak * w[k] + mu * error * x[n-k]
 *
 * 输出:
 *   xxx_noise.wav     — 经过声学通路的噪声 (不开 ANC)
 *   xxx_antinoise.wav — LMS 生成的反噪声
 *   xxx_residual.wav  — 叠加后残留 (理想趋近静音)
 *   error.csv         — 误差收敛曲线
 *
 * 用法:
 *   ./lms_demo my_audio.wav    (用你自己的 WAV)
 *   ./lms_demo --gen           (自动生成白噪声测试 WAV)
 *
 * 只支持 16-bit PCM WAV，采样率不限 (但会按文件的真实采样率处理)
 *
 * 编译: gcc lms_demo.c -o lms_demo -lm
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ================================================================
 *  一、可调参数
 * ================================================================ */
#define LMS_TAPS   64      /* LMS 滤波器阶数 */
#define PLANT_TAPS 13      /* 模拟声学通路的 FIR 阶数 */
#define MU         0.005f  /* 步长: 太大会发散, 太小收敛慢 */
#define LEAK       0.9999f /* 泄漏因子: 防止系数漂移 */

/* ================================================================
 *  二、WAV 文件读写 (16-bit PCM, 单/双声道)
 * ================================================================ */
typedef struct {
    int      sample_rate;
    int      channels;
    int      bits_per_sample;
    int      data_size;   /* PCM 数据字节数 */
    int16_t *data;        /* PCM 采样值 (交错排列) */
    int      num_samples; /* 采样总数 (= data_size / (channels*2)) */
} WavFile;

static int wav_read(const char *filename, WavFile *wav) {
    FILE *fp = fopen(filename, "rb");
    if (!fp)
        return -1;

    /* 跳过前 22 字节 (RIFF header), 读声道数 */
    fseek(fp, 22, SEEK_SET);
    int16_t ch;
    if (fread(&ch, 2, 1, fp) != 1) {
        fclose(fp);
        return -1;
    }
    wav->channels = ch;

    /* 读采样率 */
    int32_t sr;
    if (fread(&sr, 4, 1, fp) != 1) {
        fclose(fp);
        return -1;
    }
    wav->sample_rate = sr;

    /* 跳到 34 字节, 读位深 */
    fseek(fp, 34, SEEK_SET);
    int16_t bps;
    if (fread(&bps, 2, 1, fp) != 1) {
        fclose(fp);
        return -1;
    }
    wav->bits_per_sample = bps;

    if (bps != 16) {
        fprintf(stderr, "只支持 16-bit WAV, 当前 %d-bit\n", bps);
        fclose(fp);
        return -1;
    }

    /* 找 "data" chunk */
    char    tag[5] = {0};
    int32_t chunk_size;
    while (1) {
        if (fread(tag, 1, 4, fp) != 4) {
            fclose(fp);
            return -1;
        }
        if (fread(&chunk_size, 4, 1, fp) != 1) {
            fclose(fp);
            return -1;
        }
        if (strncmp(tag, "data", 4) == 0)
            break;
        fseek(fp, chunk_size, SEEK_CUR); /* 跳过非 data chunk */
    }

    wav->data_size   = chunk_size;
    wav->num_samples = chunk_size / (wav->channels * 2);
    wav->data        = malloc(chunk_size);
    if (!wav->data) {
        fclose(fp);
        return -1;
    }
    if (fread(wav->data, 1, chunk_size, fp) != (size_t)chunk_size) {
        free(wav->data);
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return 0;
}

static int wav_write(const char *filename, int sr, int ch, const int16_t *data, int num_samples) {
    FILE *fp = fopen(filename, "wb");
    if (!fp)
        return -1;

    int byte_rate   = sr * ch * 2;
    int block_align = ch * 2;
    int data_size   = num_samples * block_align;
    int riff_size   = 36 + data_size;

    /* RIFF header */
    fwrite("RIFF", 1, 4, fp);
    fwrite(&riff_size, 4, 1, fp);
    fwrite("WAVE", 1, 4, fp);

    /* fmt chunk */
    fwrite("fmt ", 1, 4, fp);
    int32_t fmt32 = 16;
    fwrite(&fmt32, 4, 1, fp);
    int16_t af16 = 1;
    fwrite(&af16, 2, 1, fp);
    int16_t ch16 = ch;
    fwrite(&ch16, 2, 1, fp);
    int32_t sr32 = sr;
    fwrite(&sr32, 4, 1, fp);
    int32_t br32 = byte_rate;
    fwrite(&br32, 4, 1, fp);
    int16_t ba16 = block_align;
    fwrite(&ba16, 2, 1, fp);
    int16_t bp16 = 16;
    fwrite(&bp16, 2, 1, fp);

    /* data chunk */
    fwrite("data", 1, 4, fp);
    fwrite(&data_size, 4, 1, fp);
    fwrite(data, 1, data_size, fp);
    fclose(fp);
    return 0;
}

static void wav_free(WavFile *wav) {
    free(wav->data);
    wav->data = NULL;
}

/* ================================================================
 *  三、固定 FIR: 模拟声学通路 (Plant)
 *
 *  声音从喇叭到耳朵之间有延迟和衰减, plant 用 FIR 来模拟这段物理路径。
 *  系数是固定的, 不会被 LMS 修改。
 * ================================================================ */
typedef struct {
    float h[PLANT_TAPS];   /* 固定系数 (模拟路径特性) */
    float buf[PLANT_TAPS]; /* 环形缓冲区 (历史输入) */
    int   head;            /* 写指针 */
} PlantFIR;

static void plant_init(PlantFIR *p) {
    /* 模拟声学通路脉冲响应: 延迟 + 主脉冲 + 衰减尾音 */
    float raw[] = {
        0.00f,
        0.00f,
        0.00f,
        0.00f, /* 4 采样延迟 ~0.25ms @16k */
        0.30f,
        0.50f,
        0.70f,
        0.85f,
        1.00f, /* 主脉冲上升沿 */
        0.60f,
        0.30f,
        0.15f,
        0.05f /* 衰减尾音 */
    };

    /* 归一化: sum(h[i]) = 1, 保证信号能量不放大不缩小 */
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
    p->buf[p->head] = x; /* ① 新采样写入环形缓冲区 */

    float y = 0.0f;
    for (int i = 0; i < PLANT_TAPS; i++) {
        /* ② 从最新往旧读: (head-i+TAPS)%TAPS */
        int idx = (p->head - i + PLANT_TAPS) % PLANT_TAPS;
        y += p->h[i] * p->buf[idx]; /* ③ 乘积累加 */
    }

    p->head = (p->head + 1) % PLANT_TAPS; /* ④ 写指针前进 */
    return y;
}

/* ================================================================
 *  四、LMS 自适应滤波器
 *
 *  w[k] = leak * w[k] + mu * error * x[n-k]
 *
 *  lms_step 一步完成: ① FIR 滤波输出 y  ② 算误差  ③ 更新系数
 *  返回误差 e, 让外部可以观测收敛情况。
 * ================================================================ */
typedef struct {
    float *w;    /* 自适应系数 (LMS 自己调的) */
    float *x;    /* 参考信号环形缓冲区 */
    int    len;  /* 滤波器阶数 */
    int    head; /* 写指针 */
    float  mu;   /* 步长 */
    float  leak; /* 泄漏因子 */
} LMSFilter;

static LMSFilter lms_create(int len, float mu, float leak) {
    LMSFilter l;
    l.len  = len;
    l.mu   = mu;
    l.leak = leak;
    l.w    = calloc(len, sizeof(float));
    l.x    = calloc(len, sizeof(float));
    l.head = 0;
    return l;
}

static void lms_destroy(LMSFilter *l) {
    free(l->w);
    free(l->x);
}

/*
 * 输入 x (参考信号), 输入 d (期望信号 = 耳朵听到的噪声)
 * 返回 e = d - y (误差 = 残留)
 *
 * 内部自动:
 *   1. y = sum(w[k] * x[n-k])          — FIR 滤波
 *   2. e = d - y                       — 算误差
 *   3. w[k] = leak*w[k] + mu*e*x[n-k]  — 更新系数
 */
static float lms_step(LMSFilter *l, float x, float d) {
    /* ① 存参考信号 */
    l->x[l->head] = x;

    /* ② FIR 滤波: y = w · x_history */
    float y = 0.0f;
    for (int k = 0; k < l->len; k++) {
        int idx = (l->head - k + l->len) % l->len;
        y += l->w[k] * l->x[idx];
    }

    /* ③ 误差 */
    float e = d - y;

    /* ④ LMS 系数更新: w[k] += mu * e * x[n-k] */
    for (int k = 0; k < l->len; k++) {
        int idx = (l->head - k + l->len) % l->len;
        l->w[k] = l->leak * l->w[k] + l->mu * e * l->x[idx];
    }

    l->head = (l->head + 1) % l->len;
    return e;
}

/* ================================================================
 *  五、生成测试 WAV (白噪声, 方便没有文件时跑起来看效果)
 * ================================================================ */
static void generate_test_wav(const char *filename, int sr, int seconds) {
    int      nsamples = sr * seconds;
    int16_t *data     = malloc(2 * nsamples * sizeof(int16_t));

    for (int i = 0; i < nsamples; i++) {
        /* 白噪声 [-1, 1], 幅度砍到 0.8 留一点 headroom */
        float val = 1.6f * (float)rand() / (float)RAND_MAX - 0.8f;
        if (val > 1.0f)
            val = 1.0f;
        if (val < -1.0f)
            val = -1.0f;
        int16_t v16     = (int16_t)(val * 32767);
        data[i * 2]     = v16;
        data[i * 2 + 1] = v16;
    }

    wav_write(filename, sr, 2, data, nsamples);
    free(data);
    printf("生成测试文件: %s (%dHz, %ds, 白噪声, 双声道)\n", filename, sr, seconds);
}

/* ================================================================
 *  六、提取输入文件名 (去掉目录和扩展名)
 * ================================================================ */
static void get_basename(const char *path, char *out, int maxlen) {
    /* 找到最后一个 '/' 或 '\' */
    const char *name = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\')
            name = p + 1;
    }

    /* 拷贝到 out, 去掉 .wav/.WAV 扩展名 */
    int i = 0;
    while (name[i] && i < maxlen - 1) {
        if (name[i] == '.' && (strcasecmp(name + i, ".wav") == 0))
            break;
        out[i] = name[i];
        i++;
    }
    out[i] = '\0';
}

/* ================================================================
 *  七、主程序
 * ================================================================ */
int main(int argc, char *argv[]) {
    const char *infile;

    if (argc > 1 && strcmp(argv[1], "--gen") == 0) {
        generate_test_wav("input.wav", 16000, 3);
        infile = "input.wav";
    } else if (argc > 1) {
        infile = argv[1];
    } else {
        printf("用法: %s <wav文件>  或  %s --gen\n", argv[0], argv[0]);
        return 1;
    }

    /*----- 1. 读 WAV (只取第一个声道) -----*/
    WavFile wav;
    if (wav_read(infile, &wav) != 0) {
        fprintf(stderr, "无法读取 %s\n", infile);
        return 1;
    }
    printf("读入: %s  sr=%d ch=%d samples=%d (%.1f秒)\n",
           infile,
           wav.sample_rate,
           wav.channels,
           wav.num_samples,
           (float)wav.num_samples / wav.sample_rate);

    int N      = wav.num_samples;
    int fs     = wav.sample_rate;
    int stride = wav.channels; /* 双声道时取左声道 */

    /* 提取到 float, 归一化到 [-1, 1] */
    float *input_f = malloc(N * sizeof(float));
    for (int i = 0; i < N; i++)
        input_f[i] = wav.data[i * stride] / 32768.0f;

    /*----- 2. 构造声学通路 (Plant) -----*/
    PlantFIR plant;
    plant_init(&plant);

    /*----- 3. 参考信号通过声学通路 → d[n] (耳朵听到的噪声) -----*/
    float *noise = malloc(N * sizeof(float));
    if (!noise)
        goto cleanup;
    for (int i = 0; i < N; i++)
        noise[i] = plant_process(&plant, input_f[i]);

    /*----- 4. LMS 自适应学习抵消 -----*/
    LMSFilter lms   = lms_create(LMS_TAPS, MU, LEAK);
    float    *error = malloc(N * sizeof(float));
    float    *anti  = malloc(N * sizeof(float)); /* 反噪声 */
    if (!error || !anti)
        goto cleanup;
    FILE *csv = fopen("error.csv", "w");
    if (!csv) {
        fprintf(stderr, "无法创建 error.csv (当前目录无写权限?)\n");
        goto cleanup;
    }

    fprintf(csv, "Sample,Time_s,Error\n");

    float rms_err = 0.0f;
    float rms_dn  = 0.0f;
    int   skip    = LMS_TAPS * 4; /* 跳过前 256 点让滤波器稳定 */

    for (int i = 0; i < N; i++) {
        /* lms_step: 输入参考信号 x 和期望信号 d, 返回误差 */
        error[i] = lms_step(&lms, input_f[i], noise[i]);

        /* 反噪声 = -(d - error) = LMS 输出的负值 */
        /* 因为 error = d - y, 所以 -y = error - d, 保留 error 作为残留 */
        float lms_out = noise[i] - error[i]; /* y = d - e */
        anti[i]       = -lms_out;            /* 反噪声 = -y */

        /* 每 100 采样记录一次误差 */
        if (i % 100 == 0)
            fprintf(csv, "%d,%.6f,%.6f\n", i, (float)i / fs, error[i]);

        /* 稳定期统计 */
        if (i >= skip) {
            rms_err += error[i] * error[i];
            rms_dn += noise[i] * noise[i];
        }
    }
    fclose(csv);
    csv = NULL;

    /*----- 5. 统计 -----*/
    int steady_samples = N - skip;
    rms_err            = sqrtf(rms_err / (float)steady_samples);
    rms_dn             = sqrtf(rms_dn / (float)steady_samples);

    printf("\n===== LMS 噪声抵消结果 =====\n");
    printf("  LMS 阶数:  %d\n", LMS_TAPS);
    printf("  声学通路:  %d 阶\n", PLANT_TAPS);
    printf("  步长 mu:   %.4f\n", MU);
    printf("  泄漏因子:  %.4f\n", LEAK);
    printf("  稳定期 (跳过前 %d 点, 共 %d 点):\n", skip, steady_samples);
    printf("    噪声 RMS:        %.4f\n", rms_dn);
    printf("    残留 RMS:        %.4f\n", rms_err);
    if (rms_dn > 0.0f)
        printf("    衰减:            %.1f dB\n", 20.0f * log10f(rms_err / rms_dn));
    printf("    (理想: 负无穷 dB = 完全静音)\n");
    printf("  误差收敛曲线: error.csv\n");

    /*----- 6. 构建输出文件名 -----*/
    char base[256];
    get_basename(infile, base, sizeof(base));

    char fn_noise[512], fn_anti[512], fn_residual[512];
    snprintf(fn_noise, sizeof(fn_noise), "%s_noise.wav", base);
    snprintf(fn_anti, sizeof(fn_anti), "%s_antinoise.wav", base);
    snprintf(fn_residual, sizeof(fn_residual), "%s_residual.wav", base);

    /*----- 7. float → int16_t, 写 WAV -----*/
    int16_t *buf_noise    = NULL;
    int16_t *buf_anti     = NULL;
    int16_t *buf_residual = NULL;

    buf_noise    = malloc(2 * N * sizeof(int16_t));
    buf_anti     = malloc(2 * N * sizeof(int16_t));
    buf_residual = malloc(2 * N * sizeof(int16_t));

    if (!buf_noise || !buf_anti || !buf_residual) {
        fprintf(stderr,
                "输出缓冲区分配失败 (N=%d, 需 %.1f MB)\n",
                N,
                (3.0 * N * sizeof(int16_t)) / (1024 * 1024));
        goto cleanup;
    }

    for (int i = 0; i < N; i++) {
        /* noise: clamp + 转 int16, 左右声道复制 */
        float vn = noise[i];
        if (vn > 1.0f)
            vn = 1.0f;
        else if (vn < -1.0f)
            vn = -1.0f;
        int16_t vn16         = (int16_t)(vn * 32767);
        buf_noise[i * 2]     = vn16;
        buf_noise[i * 2 + 1] = vn16;

        /* anti: clamp + 转 int16, 左右声道复制 */
        float va = anti[i];
        if (va > 1.0f)
            va = 1.0f;
        else if (va < -1.0f)
            va = -1.0f;
        int16_t va16        = (int16_t)(va * 32767);
        buf_anti[i * 2]     = va16;
        buf_anti[i * 2 + 1] = va16;

        /* residual: clamp + 转 int16, 左右声道复制 */
        float ve = error[i];
        if (ve > 1.0f)
            ve = 1.0f;
        else if (ve < -1.0f)
            ve = -1.0f;
        int16_t ve16            = (int16_t)(ve * 32767);
        buf_residual[i * 2]     = ve16;
        buf_residual[i * 2 + 1] = ve16;
    }

    wav_write(fn_noise, fs, 2, buf_noise, N);
    wav_write(fn_anti, fs, 2, buf_anti, N);
    wav_write(fn_residual, fs, 2, buf_residual, N);

    printf("\n输出文件:\n");
    printf("  %s — 不开 ANC 的噪声\n", fn_noise);
    printf("  %s — LMS 生成的反噪声\n", fn_anti);
    printf("  %s — 叠加后残留\n", fn_residual);
    printf("  error.csv — 误差收敛曲线\n");
    printf("\n试试:\n");
    printf("  1. 播放 %s → 听原始噪声\n", fn_noise);
    printf("  2. 播放 %s → 残留应该小声很多\n", fn_residual);
    printf("  3. Excel 打开 error.csv 画误差曲线\n");
    printf("  4. 改 LMS_TAPS / MU / plant_init 的 raw[] 看效果变化\n\n");

cleanup:
    /*----- 8. 清理 -----*/
    if (csv)
        fclose(csv);
    wav_free(&wav);
    free(input_f);
    free(noise);
    free(error);
    free(anti);
    free(buf_noise);
    free(buf_anti);
    free(buf_residual);
    lms_destroy(&lms);

    return 0;
}
