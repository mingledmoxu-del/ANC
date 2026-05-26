/*
 * Phase 3: LMS 自适应滤波器 — 系统辨识 (WAV 文件版)
 *
 * 做什么:
 *   读入一个 WAV 文件，把它通过一个"未知系统"(模拟的声学路径),
 *   LMS 滤波器观察未知系统的输出, 自动调整自己的系数去逼近它。
 *   输出: lms_output.wav (LMS 的估计输出) 和 lms_error.wav (残余误差)
 *
 * 怎么理解:
 *   未知系统 = 声音从喇叭传到耳朵/麦克风的物理路径
 *   LMS 学习 = 你的 ANC 系统在"弄清楚"这个路径长什么样
 *   误差趋近于零 = LMS 学会了, 可以精准产生反噪声
 *
 * 用法:
 *   有 WAV 文件:  ./lms_demo my_audio.wav
 *   没有的话:    ./lms_demo --gen    (自动生成一段测试音频)
 *
 * 编译:
 *   gcc lms_demo.c -o lms_demo -lm
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================
 *  一、WAV 文件读写 (只支持 16-bit PCM, 单/双声道)
 * ================================================================ */
typedef struct {
    int    sample_rate;
    int    channels;
    int    bits_per_sample;
    int    data_size;   /* PCM 数据字节数 */
    short *data;        /* PCM 采样值 (交错排列) */
    int    num_samples; /* 采样总数 (= data_size / (channels*2)) */
} WavFile;

int wav_read(const char *filename, WavFile *wav) {
    FILE *fp = fopen(filename, "rb");
    if (!fp)
        return -1;

    /* 跳过前 22 字节 (RIFF header), 读声道数 */
    fseek(fp, 22, SEEK_SET);
    int16_t ch;
    fread(&ch, 2, 1, fp);
    wav->channels = ch;

    /* 读采样率 */
    int32_t sr;
    fread(&sr, 4, 1, fp);
    wav->sample_rate = sr;

    /* 跳到 34 字节, 读位深 */
    fseek(fp, 34, SEEK_SET);
    int16_t bps;
    fread(&bps, 2, 1, fp);
    wav->bits_per_sample = bps;

    /* 找 "data" chunk */
    char    tag[5] = {0};
    int32_t chunk_size;
    while (1) {
        if (fread(tag, 1, 4, fp) != 4) {
            fclose(fp);
            return -1;
        }
        fread(&chunk_size, 4, 1, fp);
        if (strncmp(tag, "data", 4) == 0)
            break;
        fseek(fp, chunk_size, SEEK_CUR); /* 跳过非 data chunk */
    }

    if (bps != 16) {
        fprintf(stderr, "只支持 16-bit WAV\n");
        fclose(fp);
        return -1;
    }

    wav->data_size   = chunk_size;
    wav->num_samples = chunk_size / (wav->channels * 2);
    wav->data        = malloc(chunk_size);
    fread(wav->data, 1, chunk_size, fp);
    fclose(fp);
    return 0;
}

int wav_write(const char *filename, const WavFile *wav) {
    FILE *fp = fopen(filename, "wb");
    if (!fp)
        return -1;

    int byte_rate = wav->sample_rate * wav->channels * 2;
    int data_size = wav->num_samples * wav->channels * 2;
    int riff_size = 36 + data_size;

    /* RIFF header */
    fwrite("RIFF", 1, 4, fp);
    fwrite(&riff_size, 4, 1, fp);
    fwrite("WAVE", 1, 4, fp);

    /* fmt chunk */
    fwrite("fmt ", 1, 4, fp);
    int32_t fmt_size = 16;
    fwrite(&fmt_size, 4, 1, fp);
    int16_t audio_fmt = 1; /* PCM */
    fwrite(&audio_fmt, 2, 1, fp);
    int16_t ch = wav->channels;
    fwrite(&ch, 2, 1, fp);
    int32_t sr = wav->sample_rate;
    fwrite(&sr, 4, 1, fp);
    fwrite(&byte_rate, 4, 1, fp);
    int16_t block_align = wav->channels * 2;
    fwrite(&block_align, 2, 1, fp);
    int16_t bps = 16;
    fwrite(&bps, 2, 1, fp);

    /* data chunk */
    fwrite("data", 1, 4, fp);
    fwrite(&data_size, 4, 1, fp);
    fwrite(wav->data, 1, data_size, fp);
    fclose(fp);
    return 0;
}

void wav_free(WavFile *wav) {
    free(wav->data);
    wav->data = NULL;
}

/* ================================================================
 *  二、FIR 滤波器 (和 Phase 2 一样)
 * ================================================================ */
typedef struct {
    float *coeffs;
    float *buffer;
    int    len;
    int    idx;
} FIRFilter;

FIRFilter fir_create(int len) {
    FIRFilter f;
    f.len    = len;
    f.coeffs = calloc(len, sizeof(float));
    f.buffer = calloc(len, sizeof(float));
    f.idx    = 0;
    return f;
}

void fir_destroy(FIRFilter *f) {
    free(f->coeffs);
    free(f->buffer);
}

/* 设定系数 (外部传入, 用于"未知系统") */
void fir_set_coeffs(FIRFilter *f, const float *coeffs) {
    memcpy(f->coeffs, coeffs, f->len * sizeof(float));
}

float fir_step(FIRFilter *f, float x) {
    f->buffer[f->idx] = x;
    float y           = 0.0f;
    for (int i = 0; i < f->len; i++) {
        int idx = (f->idx - i + f->len) % f->len;
        y += f->buffer[idx] * f->coeffs[i];
    }
    f->idx = (f->idx + 1) % f->len;
    return y;
}

/* ================================================================
 *  三、LMS 自适应滤波器
 *  w[i] = w[i] + mu * error * x[n-i]   — 核心! 只有这一行公式
 * ================================================================ */
typedef struct {
    float *w;      /* 自适应系数 (LMS 自动调的) */
    float *buffer; /* 历史输入 */
    int    len;
    int    idx;
    float  mu;   /* 步长 */
    float  leak; /* 泄漏因子 */
} LMSFilter;

LMSFilter lms_create(int len, float mu, float leak) {
    LMSFilter l;
    l.len    = len;
    l.mu     = mu;
    l.leak   = leak;
    l.w      = calloc(len, sizeof(float));
    l.buffer = calloc(len, sizeof(float));
    l.idx    = 0;
    return l;
}

void lms_destroy(LMSFilter *l) {
    free(l->w);
    free(l->buffer);
}

/* 输入 x, 期望 d, 返回输出 y (同时自动更新系数 w) */
float lms_step(LMSFilter *l, float x, float d) {
    /* 1. 先算 FIR 输出 y = w · x_history */
    l->buffer[l->idx] = x;
    float y           = 0.0f;
    for (int i = 0; i < l->len; i++) {
        int bi = (l->idx - i + l->len) % l->len;
        y += l->buffer[bi] * l->w[i];
    }

    /* 2. 算误差 */
    float e = d - y;

    /* 3. 更新系数: w[i] = leak*w[i] + mu * e * x[n-i] */
    for (int i = 0; i < l->len; i++) {
        int bi  = (l->idx - i + l->len) % l->len;
        l->w[i] = l->leak * l->w[i] + l->mu * e * l->buffer[bi];
    }

    l->idx = (l->idx + 1) % l->len;

    /* 返回误差, 让外部可以观测收敛情况 */
    return e;
}

/* ================================================================
 *  四、生成测试 WAV (440Hz 正弦 + 白噪声)
 * ================================================================ */
void generate_test_wav(const char *filename, int sr, int seconds) {
    int    nsamples = sr * seconds;
    short *data     = malloc(nsamples * sizeof(short)); /* mono */

    for (int i = 0; i < nsamples; i++) {
        float t   = (float)i / sr;
        float val = 0.5f * sinf(2.0f * M_PI * 440.0f * t) + 0.15f * ((float)rand() / RAND_MAX - 0.5f);
        if (val > 1.0f)
            val = 1.0f;
        if (val < -1.0f)
            val = -1.0f;
        data[i] = (short)(val * 32767);
    }

    WavFile wav = {.sample_rate     = sr,
                   .channels        = 1,
                   .bits_per_sample = 16,
                   .num_samples     = nsamples,
                   .data_size       = nsamples * 2,
                   .data            = data};
    wav_write(filename, &wav);
    free(data);
    printf("生成测试文件: %s (%dHz, %ds, mono)\n", filename, sr, seconds);
}

/* ================================================================
 *  五、主程序
 * ================================================================ */
/* 模拟"未知系统"的系数 — 一个低通 FIR, 模拟声音穿过介质后的变化 */
#define MYSTERY_LEN 32

void make_mystery_coeffs(float *c, int len) {
    float fc  = 1200.0f / 16000.0f; /* 1.2kHz 截止 (归一化) */
    int   mid = len / 2;
    for (int i = 0; i < len; i++) {
        if (i == mid)
            c[i] = 2.0f * fc;
        else {
            float x = (float)(i - mid);
            c[i]    = sinf(2.0f * M_PI * fc * x) / (M_PI * x);
        }
    }
}

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

    /* 1. 读 WAV (只取第一个声道) */
    WavFile wav;
    if (wav_read(infile, &wav) != 0) {
        fprintf(stderr, "无法读取 %s\n", infile);
        return 1;
    }
    printf("读入: %s  sr=%d ch=%d samples=%d\n", infile, wav.sample_rate, wav.channels, wav.num_samples);

    int N = wav.num_samples;

    /* 提取左声道到 float */
    float *input_f = malloc(N * sizeof(float));
    for (int i = 0; i < N; i++)
        input_f[i] = wav.data[i * wav.channels] / 32768.0f;

    /* 2. 构造"未知系统"(模拟声学路径) */
    float mystery_coeffs[MYSTERY_LEN];
    make_mystery_coeffs(mystery_coeffs, MYSTERY_LEN);
    FIRFilter mystery = fir_create(MYSTERY_LEN);
    fir_set_coeffs(&mystery, mystery_coeffs);

    /* 3. 生成"期望信号" = 输入通过未知系统 */
    float *desired = malloc(N * sizeof(float));
    for (int i = 0; i < N; i++)
        desired[i] = fir_step(&mystery, input_f[i]);
    fir_destroy(&mystery);

    /* 4. LMS 系统辨识 */
    LMSFilter lms   = lms_create(MYSTERY_LEN, 0.01f, 0.9999f);
    float    *error = malloc(N * sizeof(float));
    FILE     *csv   = fopen("lms_error.csv", "w");
    fprintf(csv, "n,error_dB\n");

    float mse_sum = 0.0f;
    int   mse_cnt = 0;

    for (int i = 0; i < N; i++) {
        error[i] = lms_step(&lms, input_f[i], desired[i]);

        /* 每 100 个采样记录一次误差 (dB) */
        if (i % 100 == 0) {
            float e_db = 20.0f * log10f(fabsf(error[i]) + 1e-10f);
            fprintf(csv, "%d,%.2f\n", i, e_db);
        }

        /* 最后 20% 的样本算稳态 MSE */
        if (i > N * 0.8) {
            mse_sum += error[i] * error[i];
            mse_cnt++;
        }
    }
    fclose(csv);

    /* 5. 输出统计 */
    printf("\n===== LMS 系统辨识结果 =====\n");
    printf("未知系统系数 (模拟的声学路径):\n");
    for (int i = 0; i < MYSTERY_LEN; i++)
        printf("  h[%2d] = % 8.5f\n", i, mystery_coeffs[i]);

    printf("\nLMS 学习到的系数:\n");
    for (int i = 0; i < MYSTERY_LEN; i++)
        printf("  w[%2d] = % 8.5f\n", i, lms.w[i]);

    float mse = mse_sum / mse_cnt;
    printf("\n稳态 MSE (最后20%%): %.6f\n", mse);
    printf("稳态误差 RMS: %.4f (%.1f dB)\n", sqrtf(mse), 10.0f * log10f(mse + 1e-10f));
    printf("收敛曲线: lms_error.csv\n");

    /* 6. 输出 WAV 文件 */
    short *out_lms = malloc(N * sizeof(short));
    short *out_err = malloc(N * sizeof(short));

    for (int i = 0; i < N; i++) {
        /* LMS 输出 (估计信号) = desired - error */
        float lms_y = desired[i] - error[i];
        if (lms_y > 1.0f)
            lms_y = 1.0f;
        if (lms_y < -1.0f)
            lms_y = -1.0f;
        out_lms[i] = (short)(lms_y * 32767);

        /* 误差 */
        float e = error[i];
        if (e > 1.0f)
            e = 1.0f;
        if (e < -1.0f)
            e = -1.0f;
        out_err[i] = (short)(e * 32767);
    }

    WavFile lms_wav = {.sample_rate     = wav.sample_rate,
                       .channels        = 1,
                       .bits_per_sample = 16,
                       .num_samples     = N,
                       .data_size       = N * 2,
                       .data            = out_lms};
    WavFile err_wav = lms_wav;
    err_wav.data    = out_err;

    wav_write("lms_output.wav", &lms_wav);
    wav_write("lms_error.wav", &err_wav);

    printf("\n输出文件:\n");
    printf("  lms_output.wav — LMS 估计信号 (收敛后应该接近目标)\n");
    printf("  lms_error.wav — 残余误差 (应该逐渐变小到几乎无声)\n");

    /* 7. 清理 */
    wav_free(&wav);
    free(input_f);
    free(desired);
    free(error);
    free(out_lms);
    free(out_err);
    lms_destroy(&lms);

    printf("\n提示: 用耳机对比听 input.wav 和 lms_error.wav\n");
    return 0;
}