#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <locale.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define FILTER_TAP_NUM 33    // 滤波器阶数 (奇数对称)
#define SAMPLE_COUNT   1000  // 采样点数
#define FS             16000 // 采样率 16kHz

typedef struct {
    float h[FILTER_TAP_NUM];
    float x[FILTER_TAP_NUM];
    int head;
} FIRFilter;

// 初始化低通 FIR 滤波器 (Sinc + Hamming 窗)
void FIR_Init(FIRFilter *f, float cutoff_hz) {
    float omega_c = 2.0f * M_PI * cutoff_hz / FS;
    int M = FILTER_TAP_NUM - 1;
    float sum = 0.0f;

    for (int n = 0; n <= M; n++) {
        int k = n - M / 2;
        if (k == 0) {
            f->h[n] = omega_c / M_PI;
        } else {
            f->h[n] = sinf(omega_c * (float)k) / (M_PI * (float)k);
        }
        // Hamming 窗: 0.54 - 0.46 * cos(2*pi*n/M)
        f->h[n] *= (0.54f - 0.46f * cosf(2.0f * M_PI * (float)n / (float)M));
        sum += f->h[n];
    }

    // 归一化增益
    for (int n = 0; n <= M; n++) {
        f->h[n] /= sum;
        f->x[n] = 0.0f;
    }
    f->head = 0;
}

float FIR_Update(FIRFilter *f, float input) {
    f->x[f->head] = input;
    float output = 0.0f;
    for (int i = 0; i < FILTER_TAP_NUM; i++) {
        int idx = (f->head - i + FILTER_TAP_NUM) % FILTER_TAP_NUM;
        output += f->h[i] * f->x[idx];
    }
    f->head = (f->head + 1) % FILTER_TAP_NUM;
    return output;
}

int main() {
    setlocale(LC_ALL, "");
    FIRFilter filter;
    FIR_Init(&filter, 1000.0f); // 1kHz 截止频率

    FILE *fp = fopen("fir_output.csv", "w");
    if (!fp) {
        printf("无法创建输出文件!\n");
        return 1;
    }

    fprintf(fp, "Index,Time(s),Original,Filtered\n");
    printf("正在处理 %d 个采样点...\n", SAMPLE_COUNT);

    float rms_in = 0, rms_out = 0;
    int valid_count = 0;

    srand((unsigned int)time(NULL));

    for (int i = 0; i < SAMPLE_COUNT; i++) {
        float t = (float)i / FS;
        // 混合信号: 440Hz 正弦波 + 3000Hz 干扰噪声
        float sig = sinf(2.0f * M_PI * 440.0f * t);
        float noise = 0.5f * sinf(2.0f * M_PI * 3000.0f * t); 
        float input = sig + noise;
        
        float output = FIR_Update(&filter, input);

        // 写入 CSV
        fprintf(fp, "%d,%.6f,%.6f,%.6f\n", i, t, input, output);

        // 计算 RMS (跳过滤波器填充阶段，即前 FILTER_TAP_NUM 个点)
        if (i >= FILTER_TAP_NUM) {
            rms_in += input * input;
            rms_out += output * output;
            valid_count++;
        }
    }

    fclose(fp);

    rms_in = sqrtf(rms_in / (float)valid_count);
    rms_out = sqrtf(rms_out / (float)valid_count);
    float db = 20.0f * log10f(rms_out / rms_in);

    printf("\nRMS 对比 (跳过前 %d 点填充期):\n", FILTER_TAP_NUM);
    printf("  滤波前 RMS: %.4f\n", rms_in);
    printf("  滤波后 RMS: %.4f\n", rms_out);
    printf("  衰减: %.1f dB\n", db);
    printf("\n输出文件: fir_output.csv\n");
    printf("请使用 Excel 打开绘图 (B列=时间, C列=原始信号, D列=滤波后信号)\n");

    return 0;
}
