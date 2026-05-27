# ANC 项目进度记录

> 2026-05-27 更新

## 已完成的代码

### Phase 1: 音频环回验证

**状态**: 基本跑通，解决了多个坑

**代码**:
- `D:\Code\ANC\phase1_loopback\loopback.c` — v1 版 (`snd_pcm_set_params`)
- `D:\Code\ANC\phase1_loopback\loopback_v2.c` — v2 版 (手动 `hw_params`)
- `Z:\anc\src\loopback.c` — 树莓派上实际在跑的版本 (手动配参 + 预填充 + link)

**关键踩坑记录**:
1. `void(sig)` → `(void)sig` 语法问题
2. `snd_pcm_set_params` 自动算的 period/buffer WM8960 不认 → 改用手动 `snd_pcm_hw_params_*` 一步步配
3. 播放持续 `-EPIPE` → 加 `snd_pcm_link` 链接录音和播放流 (WM8960 是全双工 codec，共用 I2S)
4. 预填充播放缓冲区避免 DMA 启动后立刻饿死

**设备信息**:
- 录音: `hw:2,0` (wm8960soundcard)
- 播放: `hw:2,0`
- 采样率: 16000 Hz, 2ch, S16_LE
- 全双工: 需要 `snd_pcm_link` 同步两个方向

---

### Phase 2: FIR 滤波器仿真

**状态**: 已完成，用户自己调整了代码

**代码**: `D:\Code\ANC\phase2_fir\fir_demo.c`

**学到的**:
- FIR 结构体: 系数数组 + 环形缓冲区 + 写指针
- 核心运算: 乘积累加 (Multiply-Accumulate)
- 低通滤波: 440Hz 保留, 3kHz 滤掉
- 输出 CSV 画图对比

---

### Phase 3: LMS 自适应滤波器

**状态**: 代码已写好，待用户运行

**代码**: `D:\Code\ANC\phase3_lms\lms_demo.c`

**功能**: 读 WAV → LMS 系统辨识 → 输出 lms_error.wav + 收敛曲线 CSV

---

## Phase 4-5: 待开始

| Phase | 内容 | 状态 |
|---|---|---|
| 4 FxLMS | 加次级通路补偿 | 待开始 |
| 5 实时 ANC | 树莓派上闭环运行 | 待开始 |
