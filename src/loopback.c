#include <alsa/asoundlib.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SAMPLE_RATE      16000
#define CHANNEL_NUM      2
#define PERIOD_FRAMES    128
#define PERIODS          4
#define FRAMES_PER_CHUNK PERIOD_FRAMES

/* 播放增益：1/4 是当前较稳的值，再大更容易啸叫 */
#define PLAYBACK_GAIN_NUM 1
#define PLAYBACK_GAIN_DEN 4

/* 语音优化：把双声道混成单声道，再复制回左右声道 */
#define ENABLE_MONO_MIX 1

static volatile int keep_running = 1;

static void signal_handler(int sig) {
    (void)sig;
    keep_running = 0;
    printf("收到退出信号，正在停止...\n");
}

/* 配置硬件参数：采样率、声道数、period 和 buffer 大小 */
static int setup_hw_params(snd_pcm_t *handle, snd_pcm_stream_t dir) {
    snd_pcm_hw_params_t *hw_params;
    unsigned int         rate   = SAMPLE_RATE;
    snd_pcm_uframes_t    period = PERIOD_FRAMES;
    snd_pcm_uframes_t    buffer = PERIOD_FRAMES * PERIODS;
    int                  err;
    const char          *name = dir == SND_PCM_STREAM_CAPTURE ? "录音" : "播放";

    snd_pcm_hw_params_alloca(&hw_params);

    err = snd_pcm_hw_params_any(handle, hw_params);
    if (err < 0) {
        fprintf(stderr, "[%s] 读取硬件参数失败: %s\n", name, snd_strerror(err));
        return err;
    }

    err = snd_pcm_hw_params_set_access(handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    if (err < 0) {
        fprintf(stderr, "[%s] 设置访问方式失败: %s\n", name, snd_strerror(err));
        return err;
    }

    err = snd_pcm_hw_params_set_format(handle, hw_params, SND_PCM_FORMAT_S16_LE);
    if (err < 0) {
        fprintf(stderr, "[%s] 设置采样格式失败: %s\n", name, snd_strerror(err));
        return err;
    }

    err = snd_pcm_hw_params_set_channels(handle, hw_params, CHANNEL_NUM);
    if (err < 0) {
        fprintf(stderr, "[%s] 设置声道数失败: %s\n", name, snd_strerror(err));
        return err;
    }

    err = snd_pcm_hw_params_set_rate_near(handle, hw_params, &rate, NULL);
    if (err < 0) {
        fprintf(stderr, "[%s] 设置采样率失败: %s\n", name, snd_strerror(err));
        return err;
    }

    err = snd_pcm_hw_params_set_period_size_near(handle, hw_params, &period, NULL);
    if (err < 0) {
        fprintf(stderr, "[%s] 设置 period 大小失败: %s\n", name, snd_strerror(err));
        return err;
    }

    buffer = period * PERIODS;
    err    = snd_pcm_hw_params_set_buffer_size_near(handle, hw_params, &buffer);
    if (err < 0) {
        fprintf(stderr, "[%s] 设置 buffer 大小失败: %s\n", name, snd_strerror(err));
        return err;
    }

    err = snd_pcm_hw_params(handle, hw_params);
    if (err < 0) {
        fprintf(stderr, "[%s] 写入硬件参数失败: %s\n", name, snd_strerror(err));
        return err;
    }

    printf("[%s] 配置完成: rate=%u ch=%u period=%lu buffer=%lu\n",
           name,
           rate,
           CHANNEL_NUM,
           (unsigned long)period,
           (unsigned long)buffer);

    err = snd_pcm_prepare(handle);
    if (err < 0) {
        fprintf(stderr, "[%s] prepare 失败: %s\n", name, snd_strerror(err));
        return err;
    }

    return 0;
}

/* 配置软件参数：控制何时启动、何时唤醒读写 */
static int setup_sw_params(snd_pcm_t *handle, snd_pcm_stream_t dir) {
    snd_pcm_sw_params_t *sw_params;
    int                  err;
    const char          *name = dir == SND_PCM_STREAM_CAPTURE ? "录音" : "播放";

    snd_pcm_sw_params_alloca(&sw_params);

    err = snd_pcm_sw_params_current(handle, sw_params);
    if (err < 0) {
        fprintf(stderr, "[%s] 读取软件参数失败: %s\n", name, snd_strerror(err));
        return err;
    }

    err = snd_pcm_sw_params_set_avail_min(handle, sw_params, PERIOD_FRAMES);
    if (err < 0) {
        fprintf(stderr, "[%s] 设置 avail_min 失败: %s\n", name, snd_strerror(err));
        return err;
    }

    if (dir == SND_PCM_STREAM_PLAYBACK) {
        err = snd_pcm_sw_params_set_start_threshold(handle, sw_params, PERIOD_FRAMES * PERIODS);
        if (err < 0) {
            fprintf(stderr, "[播放] 设置 start_threshold 失败: %s\n", snd_strerror(err));
            return err;
        }
    } else {
        err = snd_pcm_sw_params_set_start_threshold(handle, sw_params, 1);
        if (err < 0) {
            fprintf(stderr, "[录音] 设置 start_threshold 失败: %s\n", snd_strerror(err));
            return err;
        }
    }

    err = snd_pcm_sw_params(handle, sw_params);
    if (err < 0) {
        fprintf(stderr, "[%s] 写入软件参数失败: %s\n", name, snd_strerror(err));
        return err;
    }

    return 0;
}

/* 当出现 XRUN 等错误时，尝试恢复音频流 */
static int recover_stream(snd_pcm_t *handle, snd_pcm_stream_t dir, int err) {
    int         rc;
    const char *name = dir == SND_PCM_STREAM_CAPTURE ? "录音" : "播放";

    rc = snd_pcm_recover(handle, err, 1);
    if (rc < 0) {
        fprintf(stderr, "[%s] 恢复失败: %s\n", name, snd_strerror(rc));
        return rc;
    }

    if (dir == SND_PCM_STREAM_CAPTURE) {
        rc = snd_pcm_start(handle);
        if (rc < 0) {
            fprintf(stderr, "[录音] 重新启动失败: %s\n", snd_strerror(rc));
            return rc;
        }
    }

    printf("[%s] 已恢复\n", name);
    return 0;
}

/* 把左右声道混成同一个单声道，提升语音可懂度 */
static void mix_to_mono(short *buffer, snd_pcm_sframes_t frames) {
    for (snd_pcm_sframes_t i = 0; i < frames; i++) {
        int   left        = buffer[i * 2];
        int   right       = buffer[i * 2 + 1];
        short mono        = (short)((left + right) / 2);
        buffer[i * 2]     = mono;
        buffer[i * 2 + 1] = mono;
    }
}

/* 对回放数据做简单软件衰减，用于降低啸叫风险 */
static void apply_playback_gain(short *buffer, snd_pcm_sframes_t frames) {
    long samples = (long)frames * CHANNEL_NUM;

    for (long i = 0; i < samples; i++) {
        int scaled = ((int)buffer[i] * PLAYBACK_GAIN_NUM) / PLAYBACK_GAIN_DEN;
        buffer[i]  = (short)scaled;
    }
}

int main(void) {
    const char *cap_device   = "hw:2,0";
    const char *pb_device    = "hw:2,0";
    snd_pcm_t  *cap_handle   = NULL;
    snd_pcm_t  *pb_handle    = NULL;
    short      *buffer       = NULL;
    int         frame_size   = CHANNEL_NUM * 2;
    long        total_frames = 0;
    int         err;

    signal(SIGINT, signal_handler);

    /*---------------- 1. 打开录音设备 ----------------*/
    err = snd_pcm_open(&cap_handle, cap_device, SND_PCM_STREAM_CAPTURE, 0);
    if (err < 0) {
        fprintf(stderr, "无法打开录音设备 %s: %s\n", cap_device, snd_strerror(err));
        return 1;
    }
    printf("============打开录音设备成功！============\n\n");

    /*---------------- 2. 打开播放设备 ----------------*/
    err = snd_pcm_open(&pb_handle, pb_device, SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        fprintf(stderr, "无法打开播放设备 %s: %s\n", pb_device, snd_strerror(err));
        snd_pcm_close(cap_handle);
        return 1;
    }
    printf("============打开播放设备成功！============\n\n");

    /*---------------- 3. 配置录音设备 ----------------*/
    err = setup_hw_params(cap_handle, SND_PCM_STREAM_CAPTURE);
    if (err < 0) {
        goto cleanup;
    }
    err = setup_sw_params(cap_handle, SND_PCM_STREAM_CAPTURE);
    if (err < 0) {
        goto cleanup;
    }

    /*---------------- 4. 配置播放设备 ----------------*/
    err = setup_hw_params(pb_handle, SND_PCM_STREAM_PLAYBACK);
    if (err < 0) {
        goto cleanup;
    }
    err = setup_sw_params(pb_handle, SND_PCM_STREAM_PLAYBACK);
    if (err < 0) {
        goto cleanup;
    }

    /*---------------- 5. 分配音频缓冲区 ----------------*/
    buffer = malloc(FRAMES_PER_CHUNK * frame_size);
    if (buffer == NULL) {
        fprintf(stderr, "buffer 分配失败\n");
        err = 1;
        goto cleanup;
    }

    printf("============开始执行============\n\n");

    /*---------------- 6. 预填充播放缓冲区 ----------------*/
    printf("预填充播放缓冲区 (%d 个 period)...\n", PERIODS);
    printf("当前播放增益: %d/%d\n", PLAYBACK_GAIN_NUM, PLAYBACK_GAIN_DEN);
#if ENABLE_MONO_MIX
    printf("语音优化模式: 单声道混音已开启\n");
#else
    printf("语音优化模式: 单声道混音已关闭\n");
#endif

    memset(buffer, 0, FRAMES_PER_CHUNK * frame_size);
    for (int i = 0; i < PERIODS; i++) {
        snd_pcm_sframes_t written = snd_pcm_writei(pb_handle, buffer, FRAMES_PER_CHUNK);
        if (written < 0) {
            fprintf(stderr, "预填充播放失败: %s\n", snd_strerror(written));
            err = (int)written;
            goto cleanup;
        }
    }
    printf("预填充完成\n\n");

    /*---------------- 7. 显式启动录音流 ----------------*/
    err = snd_pcm_start(cap_handle);
    if (err < 0) {
        fprintf(stderr, "启动录音失败: %s\n", snd_strerror(err));
        goto cleanup;
    }

    /*---------------- 8. 录音直通播放主循环 ----------------*/
    while (keep_running) {
        snd_pcm_sframes_t frames = snd_pcm_readi(cap_handle, buffer, FRAMES_PER_CHUNK);
        if (frames < 0) {
            fprintf(stderr, "录音错误 (code=%ld): %s\n", (long)frames, snd_strerror(frames));
            err = recover_stream(cap_handle, SND_PCM_STREAM_CAPTURE, (int)frames);
            if (err < 0) {
                break;
            }
            continue;
        }

#if ENABLE_MONO_MIX
        mix_to_mono(buffer, frames);
#endif

        /* 先做软件衰减，再送到播放设备，降低啸叫风险 */
        apply_playback_gain(buffer, frames);

        frames = snd_pcm_writei(pb_handle, buffer, frames);
        if (frames < 0) {
            fprintf(stderr, "播放错误 (code=%ld): %s\n", (long)frames, snd_strerror(frames));
            err = recover_stream(pb_handle, SND_PCM_STREAM_PLAYBACK, (int)frames);
            if (err < 0) {
                break;
            }

            frames = snd_pcm_writei(pb_handle, buffer, FRAMES_PER_CHUNK);
            if (frames < 0) {
                fprintf(stderr, "重试写入失败: %s\n", snd_strerror(frames));
                err = (int)frames;
                break;
            }
        }

        total_frames += frames;
    }

cleanup:
    /*---------------- 9. 清理资源 ----------------*/
    printf("\n总共处理 %ld 帧 (%.1f 秒)\n", total_frames, (double)total_frames / SAMPLE_RATE);

    if (pb_handle != NULL) {
        snd_pcm_drain(pb_handle);
    }
    if (cap_handle != NULL) {
        snd_pcm_close(cap_handle);
    }
    if (pb_handle != NULL) {
        snd_pcm_close(pb_handle);
    }
    free(buffer);

    printf("============退出============\n");
    return err < 0 ? 1 : 0;
}
