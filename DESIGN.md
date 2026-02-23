# v4l2_camera 系统设计文档

> **版本**: 1.0.0
> **目标平台**: NXP i.MX6ULL (ARM Cortex-A7, armhf)
> **构建工具**: CMake 3.10+, gcc-arm-linux-gnueabihf
> **依赖库**: OpenCV 4.6, Linux V4L2, Linux Framebuffer

---

## 目录

1. [项目概述](#1-项目概述)
2. [系统架构](#2-系统架构)
3. [模块设计](#3-模块设计)
4. [设计模式](#4-设计模式)
5. [线程模型与并发](#5-线程模型与并发)
6. [数据流](#6-数据流)
7. [错误处理策略](#7-错误处理策略)
8. [内存管理](#8-内存管理)
9. [构建与部署](#9-构建与部署)
10. [测试策略](#10-测试策略)

---

## 1. 项目概述

`v4l2_camera` 是一个运行在 Linux 嵌入式开发板上的摄像头应用程序，实现：

- 通过 V4L2 内核接口采集 MJPEG 视频流
- 将视频帧实时显示到 Linux Framebuffer 屏幕
- 对视频帧应用多种图像处理算法（高斯模糊、边缘检测、卡通化等）
- 拍照并保存为 JPEG 文件，支持照片浏览

**设计核心原则**：
- 不依赖 Qt 等 GUI 框架，直接操作硬件（V4L2 / Framebuffer）
- C++17 标准，使用 RAII、智能指针、标准线程库
- 组件间通过回调函数解耦，避免全局变量

---

## 2. 系统架构

```
┌─────────────────────────────────────────────────────┐
│                     CameraApp                        │
│  （协调者：管理生命周期，路由回调，处理用户命令）      │
└────┬─────────┬───────────┬────────────┬─────────────┘
     │         │           │            │
     ▼         ▼           ▼            ▼
┌─────────┐ ┌──────────┐ ┌───────────┐ ┌────────────┐
│V4L2     │ │Capture   │ │Image      │ │Photo       │
│Device   │ │Thread    │ │Processor  │ │Manager     │
│(硬件抽象)│ │(采集线程)│ │(处理线程) │ │(文件管理)  │
└────┬────┘ └────┬─────┘ └─────┬─────┘ └────────────┘
     │           │             │
     ▼           │             ▼
┌─────────┐      │       ┌───────────┐
│内核 V4L2│      │       │Frame      │
│驱动     │      │       │Buffer     │
└─────────┘      ▼       │(显示输出) │
          ┌────────────┐  └─────┬─────┘
          │MJPEG解码   │        │
          │(imdecode) │        ▼
          └────────────┘  ┌───────────┐
                          │Linux      │
                          │Framebuffer│
                          └───────────┘
```

### 组件职责划分

| 组件 | 职责 | 硬件依赖 |
|------|------|---------|
| `V4L2Device` | V4L2 设备打开/关闭、MMAP 缓冲、帧采集 | `/dev/video*` |
| `CaptureThread` | 独立线程采集帧、MJPEG→Mat 解码、回调通知 | 依赖 V4L2Device |
| `ImageProcessor` | 生产者-消费者模型处理帧，策略模式切换算法 | 无 |
| `FrameBuffer` | 将 cv::Mat 写入 Framebuffer 显存 | `/dev/fb0` |
| `PhotoManager` | 照片保存/列举/加载，文件系统操作 | 文件系统 |
| `CameraApp` | 主控制器，用户命令循环，组件协调 | 无 |

---

## 3. 模块设计

### 3.1 V4L2Device

```
职责: V4L2 摄像头硬件抽象层
模式: RAII（构造申请资源，析构释放资源）
```

**关键接口：**
```cpp
int open(int width, int height, uint32_t pixel_format);  // 打开并初始化
Frame dequeue_buffer();   // 采集一帧（阻塞）
int enqueue_buffer(int index);  // 归还缓冲区
```

**内部缓冲区管理：**
```
┌──────────────────────────────────┐
│  内核空间 (V4L2 驱动)              │
│  Buffer[0] ─── mmap ──► user_buffers_[0]  │
│  Buffer[1] ─── mmap ──► user_buffers_[1]  │
│  Buffer[2] ─── mmap ──► user_buffers_[2]  │
│  Buffer[3] ─── mmap ──► user_buffers_[3]  │
└──────────────────────────────────┘
```
使用 `mmap` 将内核缓冲区直接映射到用户空间，**零拷贝**读取帧数据。

**设计决策：**
- `kBufferCount = 4`：平衡延迟与丢帧率（缓冲区太少容易丢帧，太多增加延迟）
- 禁止拷贝，仅允许移动语义（资源独占）

---

### 3.2 CaptureThread

```
职责: 在独立线程中持续采集帧并通知上层
模式: 观察者（回调）
```

**线程工作流：**
```
capture_loop()
    while running_:
        select(fd, timeout=100ms)    ← 等待帧就绪（避免忙等）
        dequeue_buffer()             ← 取帧
        decode_mjpeg()               ← MJPEG → cv::Mat
        frame_callback_(frame)       ← 通知上层
        enqueue_buffer()             ← 归还给驱动
```

**回调类型：**
```cpp
using FrameCallback = std::function<void(const cv::Mat&)>;
using ErrorCallback = std::function<void(const std::string&)>;
```

**设计决策：**
- 使用 `std::atomic<bool> running_` 停止标志，无需条件变量
- 回调在采集线程中执行，上层回调实现必须足够快（否则丢帧）

---

### 3.3 ImageProcessor

```
职责: 对视频帧进行图像处理（可热切换算法）
模式: 生产者-消费者 + 策略模式 + 简单工厂
```

**内部结构：**
```
主线程 (生产者)                  处理线程 (消费者)
──────────────                  ────────────────
submit_frame(frame)  ──push──►  process_loop()
                      frame_cv_   while running_:
                                    wait_for(frame_cv_)
                                    algo = algorithms_[mode]
                                    result = algo->process(frame)
                                    output_callback_(result)
```

**帧覆盖策略（最新帧优先）：**
```
submit_frame() 每次都覆盖 pending_frame_
如果处理速度 < 采集速度，旧帧自动被丢弃
这是嵌入式实时显示的正确策略（优先显示最新画面）
```

**算法注册表：**
```cpp
std::unordered_map<ProcessMode, std::unique_ptr<ProcessAlgorithm>> algorithms_;
// 在构造时一次性注册全部算法，O(1) 查找
```

---

### 3.4 FrameBuffer

```
职责: 将 cv::Mat 图像渲染到 Linux Framebuffer 设备
支持: RGB565 (16bpp) 和 ARGB8888 (32bpp)
```

**像素格式适配：**

| bpp | 格式 | 写入方式 |
|-----|------|---------|
| 32 | ARGB8888 | BGR→BGRA，`memcpy` 整行（最快）|
| 16 | RGB565 | BGR→RGB565，逐行转换写入 |
| 其他 | 未知 | 逐像素写入（回退路径）|

**缩放逻辑：**
```
scale = min(screen_w / img_w, screen_h / img_h)
→ 保持宽高比缩放，居中显示
→ 使用 INTER_LINEAR 双线性插值
```

---

### 3.5 PhotoManager

```
职责: 照片文件的保存、列举、加载
文件名: photo_YYYYMMDD_HHMMSS_mmm.jpg（时间戳+毫秒）
```

**文件命名防冲突：**
```cpp
generate_filename() = strftime("%Y%m%d_%H%M%S") + "_" + ms
// 毫秒精度足以在正常使用中避免冲突
// 并发场景下文件名可能仍重复（见测试说明）
```

**列表排序：**
```cpp
std::sort(photos, [](a, b){
    return fs::last_write_time(a) > fs::last_write_time(b);
});
// 按修改时间降序排列，最新照片索引为 0
```

---

### 3.6 CameraApp

```
职责: 整个应用的顶层协调者
模式: 命令模式（用户输入→dispatch→handler）
```

**命令表：**

| 输入 | 功能 |
|------|------|
| `o` | 打开摄像头，启动采集和处理线程 |
| `c` | 关闭摄像头 |
| `p` | 拍照（保存 cached_frame_）|
| `0`~`6` | 切换图像处理模式 |
| `l` | 列出所有照片 |
| `v <n>` | 查看第 n 张照片 |
| `q` | 退出 |

**帧缓存（cached_frame_）：**
```
采集线程 → on_frame_captured() → cached_frame_ ← handle_capture()
                                      │
                              frame_cache_mutex_ 保护
```
这样拍照时不需要等待 dequeue，避免与采集线程竞争。

---

## 4. 设计模式

### 4.1 策略模式（Strategy Pattern）

用于图像处理算法热切换。

```
                 《接口》
              ProcessAlgorithm
              + process(Mat) → Mat
              + name() → string
                    △
        ┌───────────┼──────────┐────────────┐
        │           │          │            │
GaussianBlur  EdgeDetect  Grayscale  Cartoon ...

ImageProcessor 通过 ProcessMode 枚举选择算法，
无需修改处理器核心代码即可新增算法（开闭原则）
```

### 4.2 RAII（Resource Acquisition Is Initialization）

`V4L2Device` 和 `FrameBuffer` 均使用 RAII：
- 构造函数打开设备
- 析构函数自动释放（`munmap` + `close`）
- 禁止拷贝，保证资源唯一所有权

### 4.3 生产者-消费者（Producer-Consumer）

`ImageProcessor` 内部实现：
- 生产者：任意线程调用 `submit_frame()`
- 消费者：内部处理线程
- 同步：`std::mutex` + `std::condition_variable`
- 特点：缓冲区大小为 1（最新帧覆盖旧帧）

### 4.4 观察者模式（Observer Pattern）简化版

用回调函数代替完整观察者模式：
```cpp
// 注册阶段
capture->set_frame_callback([this](const cv::Mat& f){ on_frame_captured(f); });
processor->set_output_callback([this](const cv::Mat& f){ on_frame_processed(f); });

// 触发阶段（无需知道观察者列表）
frame_callback_(frame);
```

### 4.5 依赖注入（Dependency Injection）

`CaptureThread` 通过构造函数引用注入 `V4L2Device`：
```cpp
explicit CaptureThread(V4L2Device& device);  // 引用，不拥有
```
避免硬编码依赖，便于测试时替换。

---

## 5. 线程模型与并发

### 线程清单

| 线程 | 名称 | 职责 | 终止方式 |
|------|------|------|---------|
| 主线程 | main/CameraApp | 命令循环、信号处理 | 用户输入 `q` 或 SIGINT |
| 采集线程 | CaptureThread | 持续采集V4L2帧 | `running_.store(false)` |
| 处理线程 | ImageProcessor | 消费帧、执行算法 | `running_.store(false)` + `notify_all()` |

### 共享数据与同步

```
共享数据                  保护机制
──────────────────────────────────────────────
CameraApp::cached_frame_  frame_cache_mutex_
CameraApp 显示操作        display_mutex_
CaptureThread::callbacks  callback_mutex_
ImageProcessor::pending   frame_mutex_ + frame_cv_
ImageProcessor::callbacks callback_mutex_
ImageProcessor::mode      atomic<ProcessMode> (无锁)
CaptureThread::running    atomic<bool> (无锁)
ImageProcessor::running   atomic<bool> (无锁)
```

### 线程停止流程

```
用户按 q
   │
   ▼
CameraApp::handle_close()
   │
   ├─► capture_->stop()
   │       running_ = false
   │       thread_.join()    ← 等待采集线程自然退出
   │
   └─► processor_->stop()
           running_ = false
           frame_cv_.notify_all()   ← 唤醒等待中的处理线程
           thread_.join()
```

---

## 6. 数据流

### 正常采集→显示流程

```
/dev/video0 (MJPEG)
    │  VIDIOC_DQBUF
    ▼
V4L2Device::dequeue_buffer()
    │  返回 Frame{data, size, index}
    ▼
CaptureThread::capture_loop()
    │  cv::imdecode(MJPEG → BGR Mat)
    ▼
frame_callback_(mat)          ← CaptureThread 调用
    │
    ├──► CameraApp::on_frame_captured()
    │       cached_frame_ = mat.clone()      （供拍照）
    │       processor_->submit_frame(mat)    （送处理）
    │
    └──► ImageProcessor::submit_frame()
             pending_frame_ = frame.clone()
             has_new_frame_ = true
             frame_cv_.notify_one()
                 │
                 ▼  (处理线程唤醒)
             algo->process(frame) → result
                 │
                 ▼
             output_callback_(result)
                 │
                 ▼
         CameraApp::on_frame_processed()
             display_->display(result)       → /dev/fb0
```

### 拍照流程

```
用户按 p
    │
    ▼
CameraApp::handle_capture()
    │  lock(frame_cache_mutex_)
    │  cached_frame_.clone()
    ▼
photos_->save_mat(frame)
    │  cv::imwrite(./photo/photo_YYYYMMDD_HHmmss_mmm.jpg)
    ▼
打印保存路径
```

---

## 7. 错误处理策略

| 错误场景 | 处理方式 | 恢复手段 |
|---------|---------|---------|
| V4L2 设备不存在 | `open()` 返回 -1 + `perror` | 用户重新执行 `o` 命令 |
| V4L2 dequeue 超时 | `select()` 超时继续循环 | 自动重试 |
| V4L2 dequeue 错误 | 调用 `error_callback_` | CameraApp 检测后关闭设备 |
| MJPEG 解码失败 | `imdecode` 返回空 Mat，跳过该帧 | 不影响后续帧 |
| OpenCV 处理异常 | `process_loop` 捕获 `cv::Exception` | 打印 stderr，继续下一帧 |
| Framebuffer 不可用 | `open()` 返回负数 | 应用降级运行（不显示）|
| 照片目录不可创建 | `ensure_directory` 打印 stderr | 后续保存失败返回空字符串 |
| 信号 SIGINT/SIGTERM | `CameraApp::request_stop()` | 优雅停止所有线程 |

**设计原则：**
- 错误不应导致程序崩溃（除非内存不足等不可恢复错误）
- 硬件错误应上报给用户，软件错误（如解码失败）静默丢帧

---

## 8. 内存管理

### 所有权模型

```
CameraApp (unique_ptr 拥有所有组件)
    ├─► unique_ptr<V4L2Device>       设备资源
    ├─► unique_ptr<FrameBuffer>      显存映射
    ├─► unique_ptr<CaptureThread>    采集线程
    ├─► unique_ptr<ImageProcessor>   处理线程
    └─► unique_ptr<PhotoManager>     文件管理
```

### cv::Mat 拷贝策略

| 位置 | 操作 | 原因 |
|------|------|------|
| `on_frame_captured()` | `mat.clone()` → `cached_frame_` | 采集线程会很快归还缓冲区 |
| `submit_frame()` | `frame.clone()` → `pending_frame_` | 解耦生产者和消费者的生命周期 |
| 算法 `process()` | 返回新 Mat | 不修改输入帧 |

**内存热点：** `submit_frame()` 中的 `.clone()` 是主要内存分配点。在 30fps、320x240 BGR 图像时，每帧约 230 KB，每秒约 7 MB 分配/释放。OpenCV 使用引用计数，析构是 O(1)。

---

## 9. 构建与部署

### 交叉编译（目标：imx6ull）

```bash
# 配置
cmake -B build \
      -DCMAKE_TOOLCHAIN_FILE=toolchain-imx6ull.cmake \
      -DBUILD_TESTS=ON

# 编译全部
cmake --build build -j4

# 编译产物
build/v4l2_camera     ← 主程序
build/tests/unit_tests   ← 单元测试
build/tests/stress_test  ← 压力测试
```

### 工具链文件说明（toolchain-imx6ull.cmake）

```cmake
CMAKE_C_COMPILER   = arm-linux-gnueabihf-gcc
CMAKE_CXX_COMPILER = arm-linux-gnueabihf-g++
OpenCV_DIR         = /usr/lib/arm-linux-gnueabihf/cmake/opencv4
```

### 目标板运行依赖

将以下动态库拷贝到开发板（如板上没有）：
```
libopencv_core.so.406
libopencv_imgproc.so.406
libopencv_imgcodecs.so.406
libstdc++.so.6
```

### 部署脚本示例

```bash
# 通过 SCP 部署到开发板（假设 IP 为 192.168.1.100）
scp build/v4l2_camera root@192.168.1.100:/usr/bin/
scp build/tests/unit_tests root@192.168.1.100:/tmp/
scp build/tests/stress_test root@192.168.1.100:/tmp/

# 在板上运行
ssh root@192.168.1.100 '/usr/bin/v4l2_camera'
```

---

## 10. 测试策略

### 测试分层

```
┌─────────────────────────────────────────────┐
│  压力测试 (stress_test)                        │
│  - 长时间运行（默认30秒/项，可配置）            │
│  - 内存泄漏检测（/proc/self/status）           │
│  - 多线程并发压力                              │
├─────────────────────────────────────────────┤
│  单元测试 (unit_tests)                         │
│  - 不依赖硬件（无 V4L2/Framebuffer）           │
│  - 可在 x86 主机或 ARM 板上运行               │
│  - 快速（全部 < 30秒）                         │
├─────────────────────────────────────────────┤
│  硬件集成测试（手工）                           │
│  - 需要 /dev/video0 和 /dev/fb0              │
│  - 运行 v4l2_camera 主程序验证端到端功能        │
└─────────────────────────────────────────────┘
```

### 单元测试覆盖项

| 模块 | 测试用例数 | 覆盖内容 |
|------|-----------|---------|
| ImageProcessor | ~15 | 所有6个算法、生命周期、回调、线程安全 |
| PhotoManager | ~15 | 目录管理、保存/加载/列举、边界条件、并发 |

### 压力测试项目

| 编号 | 测试项 | 验证目标 |
|------|--------|---------|
| 1 | 高频帧提交吞吐量 | 系统处理能力下限 |
| 2 | 模式快速轮切稳定性 | 无崩溃、无输出空帧 |
| 3 | 长时间内存稳定性 | 内存增长 < 50MB |
| 4 | 多生产者并发提交 | 无竞态条件、无崩溃 |
| 5 | PhotoManager 批量写入 | I/O 不阻塞、文件不损坏 |
| 6 | 采集→处理→保存联合 | 端到端流水线稳定性 |

### 运行方式

```bash
# 在开发板上运行单元测试
./unit_tests

# 运行压力测试（默认每项30秒）
./stress_test

# 自定义时长（每项10秒，快速验证）
./stress_test 10

# 长时间耐久测试（每项300秒）
./stress_test 300
```

### 不纳入自动测试的部分

以下功能需手工在真实硬件上验证：
- V4L2 设备打开与采集（依赖 `/dev/video0`）
- Framebuffer 显示（依赖 `/dev/fb0`）
- 完整的 CaptureThread 集成（依赖真实 V4L2 数据）
- MJPEG 解码质量（依赖摄像头实际输出）
