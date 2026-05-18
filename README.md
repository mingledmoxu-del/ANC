# RpiANC

基于 Raspberry Pi 平台的 Active Noise Control（主动降噪）算法实现。

这是论文《Adaptive Active Noise Cancelling System for Headphones on Raspberry Pi Platform》（Raspberry Pi 平台下头戴式耳机自适应主动降噪系统）的配套代码，该论文已在 2020 年信号处理研讨会（Signal Processing Workshop 2020）上发表 https://mrweek.org/spw/

文章链接：https://ieeexplore.ieee.org/document/9259141


## 使用的硬件
* Raspberry Pi 3 Model A+

* 两个微型 MEMS 麦克风 - https://www.adafruit.com/product/3421

* 通过 3.5 毫米插孔连接的头戴式耳机


![组装好的系统](docs/rpi_anc_system.png)

![系统原理图](docs/anc-system-schematic.png)


## 编译和运行命令

示例：
```
mkdir build && cd build
cmake ../ && make all
```

执行前馈（feedforward）主动降噪的主二进制文件：
```
./ffANC
```

在 matplotlib 图表中展示 LMS 和 FxLMS 如何衰减模拟噪声的简单测试：
```
./lmstest
./fxlmstest
```

## 构建依赖项 

已在 Ubuntu 18.04 和 Raspbian 发行版上测试构建：
```
Cmake >= 3.7
Alsa 库，即相关包如：libasound2, libasound2-dev。
Python2.7 库（matplotlibcpp 依赖），即相关包如：python-dev
OpenMP 指令（你的编译器很可能已支持）
```


## 仓库和代码结构

Cmake 和 make 命令会构建几个二进制文件。主要的是 `ffANC` 二进制文件，它执行来自 [feedforward_anc.cpp](Mains/feedforward_anc.cpp) 文件的主函数。在这个文件中，你可以定义或移除 `DEPLOYED_ON_RPI` 宏，以更改用于采集和回放的设备。定义 `CAP_MEASUREMENTS` 宏可以采集样本值并将它们保存到文件中。

`ffANC` 目标的主函数执行指定次数的循环迭代。在每次迭代期间有三个主要操作：并发执行新样本的采集、样本处理和计算样本的回放，采用 fork-join 模型。首先，这 3 个操作都在各自独立的线程中执行。然后，交换各个操作的输出样本。最近计算出的输出样本将被移动到回放函数的输入数组中，而新采集的样本将被移动到信号处理函数的输入数组中。


在 [constants.h](Headers/constants.h) 文件中，你可以找到在 Raspberry Pi 上使用的设备名称以及其他用于信号处理的常量。

所有的信号处理代码都在 [Headers](Headers) 目录下的头文件和 [processing.cpp](Sources/processing.cpp) 源文件中。

[Scripts](Scripts) 目录包含 python 和 bash 脚本，用于自动化测量、数据收集、部署代码等。

## 结果

对比所创建的 ANC 系统与森海塞尔（Sennheiser）HR 4.50 BTNC 耳机对恒定频率噪声信号的衰减效果。

![衰减对比](docs/attenuation_comp.png)


免责声明：此实现仅在特定硬件设置下工作，并衰减了特定类型（恒定频率）的噪声。不保证它能在不同的设置下开箱即用。我在 github 上发布了这些代码，希望当其他人想要实现类似的功能时，这个代码库能作为一个起点或提供一些灵感。


## 实用链接、文章等

* [麦克风接线和连接指南](https://learn.adafruit.com/adafruit-i2s-mems-microphone-breakout/raspberry-pi-wiring-test#wiring-for-stereo-mic-3061608-5)
* [ALSA 声音编程基础](https://www.linuxjournal.com/article/6735)
* [使用 ALSA 音频 API 教程](http://equalarea.com/paul/alsa-audio.html)
* [Rpi I2S 讨论帖](https://www.raspberrypi.org/forums/viewtopic.php?t=91237)
* [C 语言中 FIR 滤波器的实现](https://sestevenson.wordpress.com/implementation-of-fir-filtering-in-c-part-1/)


## 包含的第三方库

本软件使用了 matplotlibcpp - 参见 matplotlibcpp-license.txt
