#ifndef CAMERA_APP_H
#define CAMERA_APP_H

#include <memory>
#include <atomic>
#include "v4l2_device.h"
#include "frame_buffer.h"
#include "capture_thread.h"
#include "image_processor.h"
#include "photo_manager.h"

// 相机应用主控制器
// 设计思路: 作为整个应用的协调者，管理各组件的生命周期和交互，
//          替代原来通过全局变量耦合的方式，
//          用组合替代继承，用依赖注入替代硬编码
class CameraApp {
public:
    CameraApp(const CameraApp&) = delete;
    CameraApp& operator=(const CameraApp&) = delete;

    CameraApp();
    ~CameraApp();

    // 运行主循环 (命令行交互)
    int run();

    // 请求停止 (信号处理器调用)
    void request_stop() { running_.store(false); }

private:
    // 命令处理
    void handle_open();
    void handle_close();
    void handle_capture();
    void handle_set_mode(int mode);
    void handle_list_photos();
    void handle_view_photo(int index);
    void handle_benchmark();
    void print_help();

    // 回调：接收采集帧
    void on_frame_captured(const cv::Mat& frame);

    // 回调：接收处理后的帧
    void on_frame_processed(const cv::Mat& frame);

    // 回调：采集错误
    void on_capture_error(const std::string& error);

    // 各组件 (使用unique_ptr管理生命周期)
    std::unique_ptr<V4L2Device> device_;
    std::unique_ptr<FrameBuffer> display_;
    std::unique_ptr<CaptureThread> capture_;
    std::unique_ptr<ImageProcessor> processor_;
    std::unique_ptr<PhotoManager> photos_;

    std::atomic<bool> running_{false};
    std::mutex display_mutex_;     // 保护显示操作的互斥锁

    // 缓存最新帧用于拍照 (避免与采集线程竞争dequeue)
    std::mutex frame_cache_mutex_;
    cv::Mat cached_frame_;

    // 采集错误标志 (由采集线程设置，主循环检查并清理)
    std::atomic<bool> capture_error_{false};
};

#endif // CAMERA_APP_H
