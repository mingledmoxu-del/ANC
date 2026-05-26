# ANC 主动降噪学习计划

硬件: 树莓派 4B + WM8960 Audio HAT (板载 MEMS 麦克风 + 外接音响)
参考: RpiANC (IEEE SPW 2020) — https://ieeexplore.ieee.org/document/9259141
目标: 从零开始理解 ANC 信号链，逐步在真实硬件上实现前馈式主动降噪

---

## Phase 1: 音频环回验证
**目标**: 验证 WM8960 全双工音频链路 (录音+播放同时工作)

**做什么**:
- 从 WM8960 麦克风采集音频 (`snd_pcm_readi`)
- 原封不动送 WM8960 播放 (`snd_pcm_writei`)
- 对着麦克风说话能从音响听到自己的声音

**学什么**:
- ALSA PCM API 基础: `open / hw_params / readi / writei / close`
- WM8960 设备参数: `hw:2,0`, S16_LE, 16000Hz, 2ch
- xrun (overrun/underrun) 的概念和恢复
- `snd_pcm_link` — 全双工 codec 共享 I2S 总线需要链接两个流

**代码**: `phase1_loopback/loopback.c` (已适配 WM8960 `hw:2,0`)

---

## Phase 2: FIR 滤波器仿真 [PC 端, 不依赖硬件] [当前进行中]

**目标**: 理解 FIR (Finite Impulse Response) 滤波器——整个 ANC 最底层的运算单元

**做什么**:
- 用 C 实现一个 FIR 滤波器结构体 (系数数组 + 环形缓冲区)
- 生成 440Hz 正弦波 + 3kHz 噪声的混合信号
- 设计一个低通 FIR 滤波器 (sinc 函数算系数)
- 对混合信号滤波, 输出原始/滤波后数据到 CSV
- 用 Excel 画折线图对比

**学什么**:
- FIR 核心公式: `y[n] = b0*x[n] + b1*x[n-1] + ... + bN*x[n-N]`
- 环形缓冲区的实现 (避免每步移动整个数组)
- 系数决定频率特性: 选什么系数就滤什么频率
- 乘积累加 (MAC) — 这后面 LMS/FxLMS 里反复出现

**代码**: `phase2_fir/fir_demo.c` (gcc 编译即可, 不依赖 ALSA)

---

## Phase 3: LMS 自适应滤波器仿真 [PC 端]

**目标**: 理解 LMS (Least Mean Square) 自适应算法——系数不再固定, 而是根据误差自动调整

**做什么**:
- 在 Phase 2 的 FIR 基础上, 加入 LMS 系数更新逻辑
- 模拟一个"未知系统" (固定 FIR 当作需要抵消的声学路径)
- LMS 滤波器观察误差信号, 自动学习逼近未知系统
- 输出误差收敛曲线到 CSV, 画图看误差如何逐步减小到零

**学什么**:
- LMS 权值更新公式: `w[n+1] = w[n] + mu * error * x[n]`
- 步长 mu (step size) 的作用: 太大发散, 太小收敛慢
- 泄漏因子 (leak factor): 防止系数漂移发散
- 这是 ANC 的核心: LMS 自动算出"反噪声"需要的滤波器系数

---

## Phase 4: FxLMS 完整仿真 [PC 端]

**目标**: 在 LMS 基础上加入次级通路 (secondary path), 仿真完整的 FxLMS ANC 系统

**做什么**:
- 实现 FxLMS 滤波器 (继承 LMS, 增加次级通路 FIR)
- 模拟完整的 ANC 信号链:
  参考信号 → S-path 滤波 → LMS 自适应 → 反噪声输出
- 用仿真数据验证: 反噪声 + 原始噪声 = 残余误差趋近于零

**学什么**:
- 为什么需要 S-path: 扬声器到误差麦之间有传递函数, LMS 必须知道它
- FxLMS vs LMS: 多了一个固定 FIR 对参考信号做预滤波
- 离线 vs 在线次级通路建模
- 这是 example 工程 `processing_feedforward_anc()` 的核心逻辑

---

## Phase 5: 实时 ANC 闭环 [树莓派硬件]

**目标**: 将 FxLMS 移植到树莓派上, 实现实时主动降噪

**做什么**:
- 在 Phase 1 的音频环回框架中, 把 Phase 4 的 FxLMS 插入处理环节:

  `readi → FxLMS 处理 → writei`

- 适配 WM8960 的物理声学路径 (板载麦克风作参考麦, 外接音响输出反噪声)
- 调整 LMS 步长、滤波器阶数等参数
- 测量实际降噪效果

**学什么**:
- 实时系统的延迟要求
- 参数调优: 步长 vs 收敛速度 vs 稳定性
- 物理声学路径对 ANC 性能的影响
- 完整理解: 论文里的 FxLMS 代码跑在自己硬件上是什么样的

---

## 学习路线图

```
Phase 1 ──→ Phase 2 ──→ Phase 3 ──→ Phase 4 ──→ Phase 5
 (硬件)     (基础)     (自适应)    (完整仿真)    (真机ANC)

  音频      FIR固定     LMS自动     FxLMS+       全部
  环回      滤波        更新系数     S-path       跑通
```

从固定系数到自适应, 从仿真到真机, 每一步都建立在上一阶段的理解之上。
