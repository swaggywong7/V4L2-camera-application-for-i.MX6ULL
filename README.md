# v4l2_camera

基于 Linux V4L2 的嵌入式摄像头应用，运行于 **NXP i.MX6ULL（Cortex-A7）** 开发板。
无 Qt / 无 GUI 框架，纯 C++17 + 标准 POSIX + OpenCV，直接操作 V4L2 与 Linux Framebuffer。

---

## 功能特性

| 功能 | 说明 |
|------|------|
| **实时预览** | 采集帧通过 `/dev/fb0` Framebuffer 直接显示，2 fps 门控节省 sys% |
| **图像处理** | 7 种处理模式可实时切换（见下表） |
| **拍照保存** | JPEG 格式自动命名，支持列表浏览与加载 |
| **NEON 加速** | 手写 ARM NEON YUV→BGR 色彩转换，CPU 占用降低 90% |
| **静态编译** | 仅依赖 libc / libpthread，无需在板端安装任何运行库 |

### 图像处理模式

| Mode | 算法 | 说明 |
|------|------|------|
| 0 | None | 原始输出（基线） |
| 1 | Gaussian Blur | 高斯模糊（kernel 15×15） |
| 2 | Edge Detect | Canny 边缘检测 |
| 3 | Grayscale | 灰度化 |
| 4 | Sharpen | 锐化 |
| 5 | Emboss | 浮雕效果 |
| 6 | Cartoon | 卡通化 |

---

## 项目结构

```
v4l2_camera/
├── src/
│   ├── main.cpp              # 入口，信号处理
│   ├── camera_app.cpp        # 主控制器，命令行交互循环
│   ├── v4l2_device.cpp       # V4L2 设备 RAII 封装（USERPTR 模式）
│   ├── capture_thread.cpp    # 采集线程（std::thread + 回调）
│   ├── frame_buffer.cpp      # Linux Framebuffer 显示封装
│   ├── image_processor.cpp   # 图像处理线程（生产者-消费者）
│   └── photo_manager.cpp     # 照片保存与管理
├── include/                  # 对应头文件
├── tests/                    # 单元测试 + 压力测试
├── CMakeLists.txt            # 构建脚本
├── Makefile                  # 快捷操作（build / push / camera）
├── toolchain-imx6ull.cmake   # ARM 交叉编译工具链配置
└── run_perf.sh               # 自动化性能对比测试脚本
```

---

## 架构设计

```
                    ┌─────────────────────────────────────┐
                    │            CameraApp                 │
                    │         (主控制器 / 协调者)           │
                    └──────┬──────────────┬───────────────┘
                           │              │
           ┌───────────────▼──┐      ┌───▼──────────────┐
           │   CaptureThread   │      │  ImageProcessor   │
           │  (采集线程)        │─────▶│  (处理线程)       │
           │  std::thread      │ 回调 │  生产者-消费者     │
           └───────┬───────────┘      └───────┬───────────┘
                   │                          │ 策略模式
           ┌───────▼───────────┐      ┌───────▼───────────┐
           │    V4L2Device     │      │  ProcessAlgorithm  │
           │  (RAII 封装)      │      │  Blur/Edge/...    │
           └───────────────────┘      └───────────────────┘
                   │
           ┌───────▼───────────┐    ┌─────────────────────┐
           │    FrameBuffer    │    │    PhotoManager      │
           │  (Framebuffer 显示)│   │  (照片存取管理)      │
           └───────────────────┘    └─────────────────────┘
```

**设计模式：**
- **RAII**：V4L2Device / FrameBuffer 资源自动管理，异常安全
- **策略模式**：图像处理算法封装为独立类，运行时热切换，符合开闭原则
- **生产者-消费者**：CaptureThread → ImageProcessor，`condition_variable` 替代轮询，避免忙等
- **观察者（回调）**：采集线程通过 `std::function` 回调通知主控制器，解耦组件

---

## 性能优化

针对 Cortex-A7 @ 528 MHz 的资源受限环境，完成三项关键优化：

| 优化项 | CPU 降低 | 说明 |
|--------|----------|------|
| YUYV 格式替代 MJPEG | −9.1 pp | 省去板端 JPEG 软解码 |
| NEON 手写 YUV→BGR | −60.9 pp | 向量化并行处理，贡献 77% |
| 500 ms Framebuffer 门控 | −9.2 pp | 限制显示刷新至 ~2 fps |
| **累计效果** | **−79.2 pp** | **87.7% → 8.5%，降低 90%** |

> 测试条件：640×480，mode0（仅采集+显示），`top` 采样 60 s 均值

NEON 核心代码思路（每次处理 8 像素）：
```cpp
// YUYV → BGR: 每次读 16 字节（8 像素），批量计算 YCbCr→RGB 公式
// R = Y + 1.403 * Cr, G = Y - 0.344 * Cb - 0.714 * Cr, B = Y + 1.770 * Cb
uint8x8_t  y_vec  = vld1_u8(yuyv);          // 8 个 Y
int16x8_t  cb_vec = vld1q_s16(cb_ptr);      // 4 个 Cb（扩展至 8）
int16x8_t  cr_vec = vld1q_s16(cr_ptr);
// ... vmlaq / vmovn 批量计算后 vst3_u8 写出 BGR
```

编译器层面同时开启：
```cmake
-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -ftree-vectorize -O3
```

---

## 构建与部署

### 环境要求

| 工具 | 版本 |
|------|------|
| CMake | ≥ 3.10 |
| arm-linux-gnueabihf-g++ | 任意支持 C++17 的版本 |
| ADB | 用于推送到开发板 |
| 精简版静态 OpenCV | 放于 `3rdparty/opencv/`（见下方说明） |

### 编译静态 OpenCV（仅首次）

```bash
# 在 x86 宿主机上交叉编译精简版 OpenCV（仅 core + imgproc + imgcodecs）
cmake /path/to/opencv \
  -DCMAKE_TOOLCHAIN_FILE=../toolchain-imx6ull.cmake \
  -DCMAKE_INSTALL_PREFIX=../3rdparty/opencv \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_LIST=core,imgproc,imgcodecs \
  -DWITH_JPEG=ON -DWITH_PNG=ON \
  -DBUILD_TESTS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_DOCS=OFF
make -j$(nproc) && make install
```

### 编译应用

```bash
# 一键配置 + 编译
make build

# 编译 + 推送到开发板（需 ADB 连接）
make push

# 查看板端摄像头信息
make camera

# 清除构建产物
make clean
```

手动 CMake：
```bash
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=../toolchain-imx6ull.cmake \
         -DCMAKE_BUILD_TYPE=Release \
         -DBUILD_TESTS=OFF
make -j$(nproc)
```

---

## 运行

```bash
# 开发板端
/home/swaggywong7/v4l2_camera /dev/video1
```

### 交互命令

```
=== V4L2 Camera Application v1.0 ===
> help

  open            打开摄像头并开始预览
  close           停止预览并关闭摄像头
  capture         拍照
  mode <0-6>      切换图像处理模式
  list            列出已保存的照片
  view <index>    在 Framebuffer 上显示指定照片
  res <width>     修改分辨率（如 res 320 → 320×240）
  bench           性能基准测试（纯采集 CPU 占用）
  help            显示帮助
  quit            退出程序

> open
> mode 2
> capture
> list
> view 0
> quit
```

---

## 性能测试脚本

`run_perf.sh` 自动化运行多场景对比测试：

```bash
# 在开发板端执行
chmod +x run_perf.sh
./run_perf.sh
# 结果保存在 /tmp/perf_test/
```

**测试场景：**
- **场景 A**：640×480，mode 0 / 1 / 2 / 6，各采样 60 s
- **场景 B**：320×240，mode 0 / 6 对比
- **场景 C**：640×480 mode 6，连续 3 分钟内存稳定性监控

---

## 硬件信息

| 项目 | 参数 |
|------|------|
| SoC | NXP i.MX6ULL |
| CPU | ARM Cortex-A7 @ 528 MHz |
| 摄像头 | USB 摄像头，`/dev/video1` |
| 显示 | `/dev/fb0`（RGB565） |
| 内核 | Linux（V4L2 子系统） |

---

## 依赖

| 库 | 版本 | 链接方式 |
|----|------|----------|
| OpenCV | 4.x（精简） | 静态（`.a`） |
| libjpeg-turbo | 随 OpenCV 编译 | 静态 |
| libpng / zlib | 随 OpenCV 编译 | 静态 |
| libpthread | 系统自带 | 动态 |
| libc | 系统自带 | 动态 |

最终二进制仅动态依赖 `libc` 和 `libpthread`，可直接运行于目标板，无需额外安装。

---

## License

MIT
