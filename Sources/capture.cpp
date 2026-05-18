//
// 由 pitersk 于 18.05.27 创建。
//

#include "../Headers/capture.h"
#include <iostream>

void init_capture(snd_pcm_t        **cap_handle,
                  unsigned int      *cap_freq,
                  snd_pcm_uframes_t *cap_period_size,
                  snd_pcm_uframes_t *cap_buffer_size,
                  unsigned int       number_of_channels,
                  const std::string  capture_device_name) {

    snd_pcm_hw_params_t *params;
    int                  rc;
    int                  dir;

    /* 打开 PCM 设备进行录音（捕获）。 */
    rc = snd_pcm_open(cap_handle, capture_device_name.c_str(), SND_PCM_STREAM_CAPTURE, 0);
    if (rc < 0) {
        fprintf(stderr, "无法打开 pcm 设备: %s\n", snd_strerror(rc));
        exit(1);
    }

    /* 分配硬件参数对象。 */
    snd_pcm_hw_params_malloc(&params);

    /* 使用默认值填充。 */
    snd_pcm_hw_params_any(*cap_handle, params);

    /* 设置所需的硬件参数。 */

    /* 交错模式 */
    snd_pcm_hw_params_set_access(*cap_handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);

    /* 有符号 16 位小端格式 */
    snd_pcm_hw_params_set_format(*cap_handle, params, SND_PCM_FORMAT_S32_LE);

    /* 双声道（立体声） */
    snd_pcm_hw_params_set_channels(*cap_handle, params, number_of_channels);

    /* 44100 位/秒采样率（CD 质量） */

    snd_pcm_hw_params_set_rate_near(*cap_handle, params, cap_freq, &dir);

    //    snd_pcm_hw_params_set_buffer_size_near (cap_handle, params, &frames);
    snd_pcm_uframes_t min_period_size_to_set = 2;

    snd_pcm_hw_params_set_period_size_min(*cap_handle, params, &min_period_size_to_set, &dir);
    snd_pcm_hw_params_set_period_size_near(*cap_handle, params, cap_period_size, NULL);
    snd_pcm_hw_params_set_buffer_size_near(*cap_handle, params, cap_buffer_size);
    /* 将参数写入驱动程序 */
    rc = snd_pcm_hw_params(*cap_handle, params);
    if (rc < 0) {
        fprintf(stderr, "无法设置硬件参数: %s\n", snd_strerror(rc));
        exit(1);
    }

    snd_pcm_hw_params_get_rate(params, cap_freq, &dir);
    snd_pcm_hw_params_get_period_size(params, cap_period_size, &dir);

    std::cerr << "捕获参数: "
              << "采样率: " << *cap_freq << " 周期大小: " << *cap_period_size
              << " 最小周期大小: " << min_period_size_to_set << std::endl;

    if (*cap_period_size != CAP_FRAMES_PER_PERIOD) {
        std::cout << "周期捕获帧数 ( " << *cap_period_size << " ) 与配置 ( "
                  << CAP_FRAMES_PER_PERIOD << " ) 不匹配" << std::endl;
        exit(1);
    } else if (*cap_buffer_size != CAP_FRAMES_PER_PERIOD * CAP_PERIODS_PER_BUFFER) {
        std::cout << "设备缓冲区的播放帧数 ( " << *cap_buffer_size << " ) 与配置 ( "
                  << CAP_FRAMES_PER_PERIOD * CAP_PERIODS_PER_BUFFER << " ) 不匹配" << std::endl;
        exit(1);
    }

    snd_pcm_hw_params_free(params);
    snd_pcm_prepare(*cap_handle);
}

void capture(snd_pcm_t *cap_handle, fixed_sample_type *cap_buffer, snd_pcm_uframes_t frames_in_cap_period) {
    long int rc;
    rc = snd_pcm_readi(cap_handle, cap_buffer, frames_in_cap_period);
    if (rc == -EPIPE) {
        /* EPIPE 意味着溢出 */
        fprintf(stderr, "捕获发生溢出\n");
        snd_pcm_prepare(cap_handle);
    } else if (rc < 0) {
        fprintf(stderr, "读取出错: %s\n", snd_strerror(rc));
    } else if (rc != (int)frames_in_cap_period) {
        fprintf(stderr, "短读取，读取了 %ld 帧\n", rc);
    }
}