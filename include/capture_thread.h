#ifndef CAPTURE_THREAD_H
#define CAPTURE_THREAD_H

#include <thread>
#include <atomic>
#include <functional>
#include <mutex>
#include <opencv2/opencv.hpp>
#include "v4l2_device.h"

// 视频采集线程类
// 设计思路: 用std::thread替代QThread，用回调函数替代Qt信号槽，
//          使用std::atomic保证线程安全的停止标志
class CaptureThread {
public:
    // 回调类型定义 - 替代Qt的信号槽机制
    using FrameCallback = std::function<void(const cv::Mat&)>;
    using ErrorCallback = std::function<void(const std::string&)>;

    CaptureThread(const CaptureThread&) = delete;
    CaptureThread& operator=(const CaptureThread&) = delete;

    explicit CaptureThread(V4L2Device& device);
    ~CaptureThread();

    // 注册回调 (观察者模式的简化版)
    void set_frame_callback(FrameCallback callback);
    void set_error_callback(ErrorCallback callback);

    // 启动/停止采集线程
    void start();
    void stop();

    // 纯采集模式：只做 DQBUF/QBUF，不解码不回调，用于诊断 CPU 瓶颈
    void set_capture_only(bool v) { capture_only_.store(v); }
    bool is_capture_only() const  { return capture_only_.load(); }

    bool is_running() const { return running_.load(); }

private:
    // 线程主函数
    void capture_loop();

    // 自动检测 MJPEG/YUYV 并解码为 BGR cv::Mat
    cv::Mat decode_frame(const unsigned char* data, unsigned int size);

    V4L2Device& device_;            // 引用，不拥有设备
    std::thread thread_;
    std::atomic<bool> running_{false};

    std::atomic<bool> capture_only_{false};
    std::mutex callback_mutex_;
    FrameCallback frame_callback_;
    ErrorCallback error_callback_;
};

#endif // CAPTURE_THREAD_H
