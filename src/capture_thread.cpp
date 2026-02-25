#include "capture_thread.h"

#include <cstdio>
#include <cerrno>
#include <cstring>
#include <chrono>

CaptureThread::CaptureThread(V4L2Device& device)
    : device_(device)
{
    printf("[CaptureThread] 采集线程已创建\n");
}

CaptureThread::~CaptureThread()
{
    stop();
    printf("[CaptureThread] 采集线程已销毁\n");
}

void CaptureThread::set_frame_callback(FrameCallback callback)
{
    std::lock_guard<std::mutex> lock(callback_mutex_);
    frame_callback_ = std::move(callback);
}

void CaptureThread::set_error_callback(ErrorCallback callback)
{
    std::lock_guard<std::mutex> lock(callback_mutex_);
    error_callback_ = std::move(callback);
}

void CaptureThread::start()
{
    if (running_.load()) return;

    running_.store(true);
    thread_ = std::thread(&CaptureThread::capture_loop, this);
    printf("[CaptureThread] 采集线程已启动\n");
}

void CaptureThread::stop()
{
    if (!running_.load()) return;

    printf("[CaptureThread] 正在停止采集线程...\n");
    running_.store(false);

    if (thread_.joinable()) {
        thread_.join();
    }

    printf("[CaptureThread] 采集线程已停止\n");
}

void CaptureThread::capture_loop()
{
    using Clock = std::chrono::steady_clock;
    constexpr auto kDisplayInterval = std::chrono::milliseconds(33);
    auto last_display = Clock::now() - kDisplayInterval;

    // FPS 统计
    auto fps_ts  = Clock::now();
    int  fps_cnt = 0;

    while (running_.load()) {
        // 从设备取出一帧（内部已用 select 阻塞，不会忙等）
        V4L2Device::Frame frame = device_.dequeue_buffer();

        if (!frame.data) {
            if (errno == EINTR || errno == 0) continue;  // select 超时或被中断，重试

            std::string err = "[CaptureThread] DQBUF失败: ";
            err += strerror(errno);

            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (error_callback_) error_callback_(err);
            break;
        }

        // 纯采集模式：直接归还，不解码不回调
        if (capture_only_.load()) {
            device_.enqueue_buffer(frame.index);
            continue;
        }

        auto now = Clock::now();
        bool should_display = (now - last_display) >= kDisplayInterval;

        cv::Mat mat;
        if (should_display) {
            // 只在需要显示时才解码（节省 CPU）
            mat = decode_frame(frame.data, frame.size);
            last_display = now;
        }

        // 归还缓冲区给驱动（无论是否解码都要归还）
        device_.enqueue_buffer(frame.index);

        if (!mat.empty()) {
            fps_cnt++;
            // 每秒输出一次 FPS
            auto now2 = Clock::now();
            long ms = std::chrono::duration_cast<std::chrono::milliseconds>(now2 - fps_ts).count();
            if (ms >= 1000) {
                printf("[PERF] FPS: %.1f\n", fps_cnt * 1000.0f / ms);
                fflush(stdout);
                fps_cnt = 0;
                fps_ts  = now2;
            }

            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (frame_callback_) frame_callback_(mat);
        }
    }
}

cv::Mat CaptureThread::decode_frame(const unsigned char* data, unsigned int size)
{
    if (!data || size == 0) return cv::Mat();

    // MJPEG 帧以 FF D8 FF 开头
    if (size >= 3 && data[0] == 0xFF && data[1] == 0xD8) {
        std::vector<unsigned char> buf(data, data + size);
        cv::Mat decoded = cv::imdecode(buf, cv::IMREAD_COLOR);
        if (!decoded.empty()) return decoded;
        fprintf(stderr, "[CaptureThread] MJPEG解码失败，尝试YUYV\n");
    }

    // YUYV (YUV 4:2:2)：每两个像素 4 字节
    int w = device_.width();
    int h = device_.height();
    if (w > 0 && h > 0 && size >= static_cast<unsigned>(w * h * 2)) {
        cv::Mat yuyv(h, w, CV_8UC2, const_cast<unsigned char*>(data));
        cv::Mat bgr;
        cv::cvtColor(yuyv, bgr, cv::COLOR_YUV2BGR_YUYV);
        return bgr;
    }

    fprintf(stderr, "[CaptureThread] 解码失败: size=%u, w=%d, h=%d\n", size, w, h);
    return cv::Mat();
}
