//
// 由 pitersk 创建于 27.05.18。
//

#include "../Headers/playback.h"
#include <iostream>

void init_playback(snd_pcm_t        **handle,
                   unsigned int      *play_freq,
                   snd_pcm_uframes_t *play_period_size,
                   snd_pcm_uframes_t *play_buffer_size,
                   unsigned int       number_of_channels,
                   const std::string  playback_device_name) {

    snd_pcm_hw_params_t *params;
    int                  rc;
    int                  dir;

    /* 打开 PCM 设备进行录音（捕获）。 */
    rc = snd_pcm_open(handle, playback_device_name.c_str(), SND_PCM_STREAM_PLAYBACK, 0);
    if (rc < 0) {
        fprintf(stderr, "无法打开 pcm 设备: %s\n", snd_strerror(rc));
        exit(1);
    }

    /* 分配硬件参数对象。 */
    snd_pcm_hw_params_malloc(&params);

    /* 使用默认值填充。 */
    snd_pcm_hw_params_any(*handle, params);

    /* 设置所需的硬件参数。 */

    /* 交错模式 */
    snd_pcm_hw_params_set_access(*handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);

    /* 有符号 16 位小端格式 */
    snd_pcm_hw_params_set_format(*handle, params, SND_PCM_FORMAT_S32_LE);

    /* 双声道（立体声） */
    snd_pcm_hw_params_set_channels(*handle, params, number_of_channels);

    /* 44100 位/秒采样率（CD 质量） */

    snd_pcm_hw_params_set_rate_near(*handle, params, play_freq, &dir);
    snd_pcm_uframes_t play_min_period_size_to_set = 1;
    snd_pcm_hw_params_set_period_size_min(*handle, params, &play_min_period_size_to_set, &dir);
    snd_pcm_hw_params_set_buffer_size_near(*handle, params, play_buffer_size);
    snd_pcm_hw_params_set_period_size_near(*handle, params, play_period_size, &dir);
    /* 将参数写入驱动程序 */
    rc = snd_pcm_hw_params(*handle, params);
    if (rc < 0) {
        fprintf(stderr, "无法设置硬件参数: %s\n", snd_strerror(rc));
        exit(1);
    }

    snd_pcm_hw_params_get_rate(params, play_freq, &dir);
    snd_pcm_hw_params_get_period_size(params, play_period_size, &dir);
    snd_pcm_hw_params_get_buffer_size(params, play_buffer_size);

    std::cerr << "播放参数: " << "播放频率: " << *play_freq << " 周期大小: " << *play_period_size
              << " 最小播放周期大小: " << play_min_period_size_to_set << " 缓冲区大小: " << *play_buffer_size
              << std::endl;

    unsigned int      val, val2;
    snd_pcm_uframes_t frames;

    snd_pcm_hw_params_get_access(params, (snd_pcm_access_t *)&val);
    printf("访问类型 = %s\n", snd_pcm_access_name((snd_pcm_access_t)val));

    snd_pcm_hw_params_get_subformat(params, (snd_pcm_subformat_t *)&val);
    printf("子格式 = '%s' (%s)\n",
           snd_pcm_subformat_name((snd_pcm_subformat_t)val),
           snd_pcm_subformat_description((snd_pcm_subformat_t)val));

    snd_pcm_hw_params_get_channels(params, &val);
    printf("声道数 = %d\n", val);

    snd_pcm_hw_params_get_rate(params, &val, &dir);
    printf("采样率 = %d bps\n", val);

    snd_pcm_hw_params_get_period_time(params, &val, &dir);
    printf("周期时间 = %d us\n", val);

    snd_pcm_hw_params_get_period_size(params, &frames, &dir);
    printf("周期大小 = %d 帧\n周期大小 = %d 字节\n", (int)frames, (int)snd_pcm_frames_to_bytes(*handle, frames));

    snd_pcm_hw_params_get_buffer_time(params, &val, &dir);
    printf("缓冲区时间 = %d us\n", val);

    snd_pcm_hw_params_get_buffer_size(params, (snd_pcm_uframes_t *)&val);
    printf("缓冲区大小 = %d 帧\n", val);

    snd_pcm_hw_params_get_periods(params, &val, &dir);
    printf("每个缓冲区的周期数 = %d 帧\n", val);

    snd_pcm_hw_params_get_period_size_min(params, &frames, &dir);
    printf(" 最小周期大小 = %d 帧\n", (int)frames);

    snd_pcm_hw_params_get_buffer_size_min(params, &frames);
    printf(" 最小缓冲区大小 = %d 帧\n", (int)frames);

    snd_pcm_hw_params_get_rate_numden(params, &val, &val2);
    printf("准确采样率 = %d/%d bps\n", val, val2);

    val = snd_pcm_hw_params_get_sbits(params);
    printf("有效位数 = %d\n", val);

    val = snd_pcm_hw_params_is_batch(params);
    printf("是否为批处理 = %d\n", val);

    val = snd_pcm_hw_params_is_block_transfer(params);
    printf("是否为块传输 = %d\n", val);

    val = snd_pcm_hw_params_is_double(params);
    printf("是否为双缓冲 = %d\n", val);

    val = snd_pcm_hw_params_is_half_duplex(params);
    printf("是否为半双工 = %d\n", val);

    val = snd_pcm_hw_params_is_joint_duplex(params);
    printf("是否为联合双工 = %d\n", val);

    val = snd_pcm_hw_params_can_overrange(params);
    printf("是否可以超出范围 = %d\n", val);

    val = snd_pcm_hw_params_can_mmap_sample_resolution(params);
    printf("是否支持 mmap = %d\n", val);

    val = snd_pcm_hw_params_can_pause(params);
    printf("是否支持暂停 = %d\n", val);

    val = snd_pcm_hw_params_can_resume(params);
    printf("是否支持恢复 = %d\n", val);

    val = snd_pcm_hw_params_can_sync_start(params);
    printf("是否支持同步启动 = %d\n", val);

    if (*play_period_size != PLAY_FRAMES_PER_PERIOD) {
        std::cout << "每个周期的播放帧数 ( " << *play_period_size << " ) 与配置 ( " << PLAY_FRAMES_PER_PERIOD
                  << " ) 不符" << std::endl;
        exit(1);
    } else if (*play_buffer_size != PLAY_FRAMES_PER_PERIOD * PLAY_PERIODS_PER_BUFFER) {
        std::cout << "每个设备缓冲区的播放帧数 ( " << *play_buffer_size << " ) 与配置 ( "
                  << PLAY_FRAMES_PER_PERIOD * PLAY_PERIODS_PER_BUFFER << " ) 不符" << std::endl;
        exit(1);
    }

    snd_pcm_hw_params_free(params);
    snd_pcm_prepare(*handle);
}

void playback(snd_pcm_t *play_handle, fixed_sample_type *play_buffer, snd_pcm_uframes_t frames_in_period) {
    ssize_t rc;
    rc = snd_pcm_writei(play_handle, play_buffer, frames_in_period);
    if (rc == -EPIPE) {
        /* EPIPE 意味着欠载 */
        fprintf(stderr, "播放发生欠载\n");
        snd_pcm_prepare(play_handle);
    } else if (rc == -EAGAIN) {
        // fprintf(stderr, "EAGAIN writei 不可用\n");
    } else if (rc < 0) {
        fprintf(stderr, "来自 writei 的播放错误: %s\n", snd_strerror(rc));
    } else if (rc != (int)frames_in_period) {
        fprintf(stderr, "播放短写入，写入了 %ld 帧\n", rc);
    }
}