/*
 * 最小播放测试: 只测 WM8960 播放通路
 * 生成 440Hz 正弦波，持续播放 3 秒
 * 编译: gcc pb_test.c -o pb_test -lasound -lm
 */
#include <alsa/asoundlib.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define RATE    16000
#define CHANNELS 2
#define DURATION 3  /* 秒 */

int main() {
    const char *dev = "hw:2,0";
    snd_pcm_t *handle;
    int err;
    unsigned int rate = RATE;

    snd_pcm_open(&handle, dev, SND_PCM_STREAM_PLAYBACK, 0);

    /* 配参数 */
    snd_pcm_hw_params_t *hw;
    snd_pcm_hw_params_alloca(&hw);
    snd_pcm_hw_params_any(handle, hw);
    snd_pcm_hw_params_set_access(handle, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(handle, hw, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(handle, hw, CHANNELS);
    snd_pcm_hw_params_set_rate_near(handle, hw, &rate, NULL);

    snd_pcm_uframes_t period = 256;
    snd_pcm_hw_params_set_period_size_near(handle, hw, &period, NULL);
    snd_pcm_uframes_t buffer = period * 4;
    snd_pcm_hw_params_set_buffer_size_near(handle, hw, &buffer);
    snd_pcm_hw_params(handle, hw);

    printf("rate=%u period=%lu buffer=%lu\n", rate, (unsigned long)period, (unsigned long)buffer);

    /* prepare */
    err = snd_pcm_prepare(handle);
    printf("prepare: %s\n", err < 0 ? snd_strerror(err) : "OK");

    /* 生成 440Hz 正弦波，写 3 秒 */
    int total = RATE * DURATION;
    short buf[period * CHANNELS];
    int written = 0;

    while (written < total) {
        int n = (total - written < (int)period) ? (total - written) : (int)period;
        for (int i = 0; i < n; i++) {
            float t = (float)(written + i) / RATE;
            short val = (short)(sinf(2.0f * M_PI * 440.0f * t) * 16000);
            buf[i * 2]     = val;  /* L */
            buf[i * 2 + 1] = val;  /* R */
        }
        snd_pcm_sframes_t f = snd_pcm_writei(handle, buf, n);
        if (f < 0) {
            printf("write 失败 (code=%ld): %s\n", (long)f, snd_strerror(f));
            f = snd_pcm_recover(handle, f, 1);
            if (f < 0) { printf("recover 失败\n"); break; }
            printf("恢复\n");
            continue;
        }
        written += f;
    }

    snd_pcm_drain(handle);
    snd_pcm_close(handle);
    printf("done, written=%d\n", written);
}
