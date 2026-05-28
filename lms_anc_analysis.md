# LMS 自适应噪声抵消 — 代码解析笔记

> 对话日期: 2026-05-28
> 工程: ANC 主动降噪学习项目 (Phase 3)
> 硬件: 树莓派 4B + WM8960 Audio HAT
> 文件: `src/lms_anc_demo.c`

---

## 一、整体信号流

```
白噪声 x[n] ──→ 声学通路(PlantFIR) ──→ d[n] (耳朵听到的噪声)
                 │
                 └──→ LMS 自适应 FIR ──→ y[n] ≈ d[n]
                                            │
                           残留 e[n] = d[n] - y[n] → 0 (静音！)
```

LMS 核心公式:

```
w[k] = leak * w[k] + mu * error * x[n-k]
```

---

## 二、PlantFIR — 模拟声学通路

### 为什么需要它

LMS 需要一个"目标"来追赶。在真实 ANC 场景中:

```
喇叭播放声音 → 空气传播(延迟+衰减+混响) → 耳朵/误差麦收到声音
```

PlantFIR 用数学模拟这段物理路径。

### 结构体

```c
typedef struct {
    float h[PLANT_TAPS];   // 固定 FIR 系数 (13 个)
    float buf[PLANT_TAPS]; // 环形缓冲区，存历史输入
    int   head;            // 写指针
} PlantFIR;
```

### 三个成员的作用

| 成员 | 类比 | 作用 |
|---|---|---|
| `h[]` | 声学路径的"指纹" | 描述延迟、衰减、混响特性 |
| `buf[]` | 最近 13 个采样点的"记忆" | 存 x[n], x[n-1], ..., x[n-12] |
| `head` | 闹钟指针 | 当前写到 buf 的哪个位置 |

---

## 三、plant_init — 初始化声学通路系数

```c
static void plant_init(PlantFIR *p) {
    float raw[] = {
        0.00f, 0.00f, 0.00f, 0.00f,  // 4 采样延迟 (~0.25ms @16kHz)
        0.30f, 0.50f, 0.70f, 0.85f, 1.00f,  // 主脉冲上升沿
        0.60f, 0.30f, 0.15f, 0.05f           // 衰减尾音
    };
    // 归一化: sum(h[i]) = 1, 保证信号能量不变
    float sum = 0.0f;
    for (int i = 0; i < PLANT_TAPS; i++) sum += raw[i];
    for (int i = 0; i < PLANT_TAPS; i++) {
        p->h[i]   = raw[i] / sum;
        p->buf[i] = 0.0f;
    }
    p->head = 0;
}
```

系数形状:

```
幅值
1.0 ┤         ██
    ┤       ██  ██
0.5 ┤     ██      ██
    ┤   ██          ██
0.0 ┤██                ██  ██
    └──┬──┬──┬──┬──┬──┬──┬──→ 时间
      延迟4采样  主脉冲     衰减尾音
```

### 为什么要归一化

FIR 的本质是加权求和。如果不归一化，sum(h) ≠ 1，信号会被整体放大或缩小。归一化保证输入什么幅度，输出还是什么幅度。

---

## 四、plant_process — FIR 滤波核心运算

```c
static float plant_process(PlantFIR *p, float x) {
    p->buf[p->head] = x;          // ① 写入新采样
    float y = 0.0f;
    for (int i = 0; i < PLANT_TAPS; i++) {
        int idx = (p->head - i + PLANT_TAPS) % PLANT_TAPS;  // ② 从最新往旧读
        y += p->h[i] * p->buf[idx];                         // ③ 乘积累加
    }
    p->head = (p->head + 1) % PLANT_TAPS;  // ④ head 前进一格
    return y;
}
```

### 具体例子 (假设 PLANT_TAPS=4, h=[0.1,0.3,0.5,0.1], head=2)

写入后 buf = [x[n-2], x[n-1], **x[n]**, x[n-3]], head=2

取模 `(head - i + 4) % 4` 从最新往回读:

```
i=0: idx=2 → buf[2]=x[n]     × h[0]=0.1
i=1: idx=1 → buf[1]=x[n-1]   × h[1]=0.3
i=2: idx=0 → buf[0]=x[n-2]   × h[2]=0.5
i=3: idx=3 → buf[3]=x[n-3]   × h[3]=0.1
```

```
y = 0.1*x[n] + 0.3*x[n-1] + 0.5*x[n-2] + 0.1*x[n-3]
```

这就是 Phase 2 学的 FIR 公式，和 `fir_demo.c` 里的 `FIR_Update` 完全一样。

---

## 五、运行结果 (mu=0.005, LMS_TAPS=64)

| 指标 | 值 |
|---|---|
| 噪声 RMS | 0.2272 |
| 残留 RMS | 0.0192 |
| 衰减 | **-21.5 dB** |

残留约是原噪声的 1/12，耳朵听明显小声很多。

---

## 六、可调参数

代码顶部 `#define` 直接改:

- **MU**: 0.001 (更稳但慢) / 0.01 (更快但可能发散)
- **LMS_TAPS**: 32 或 128，影响抵消效果
- **DURATION**: 改秒数，更长 = LMS 学得更充分
- **raw[] 数组**: 改声学通路的延迟/衰减，模拟不同环境

---

## 七、权限问题备忘

QEMU 用户态环境通过 VS Code Remote SSH 连接到 `/home/omo/...`，文件 owner 是 `omo`。
Cowork 直接写 `Z:\anc` 时用的是不同的凭据，会导致文件 owner 变化，VS Code 无法保存。

**解决方法**: 在 QEMU 里跑:

```bash
sudo chown omo:omo <文件路径>
```

**以后的工作流程**: Cowork 在 VM outputs 目录生成文件 → 告诉内容 → 用户自己决定是否拷进 QEMU。
