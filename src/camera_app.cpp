#include "camera_app.h"

#include <cstdio>
#include <iostream>
#include <string>
#include <sstream>

CameraApp::CameraApp()
    : device_(std::make_unique<V4L2Device>())
    , display_(std::make_unique<FrameBuffer>())
    , processor_(std::make_unique<ImageProcessor>())
    , photos_(std::make_unique<PhotoManager>())
{
    printf("[CameraApp] 应用初始化完成\n");
}

CameraApp::~CameraApp()
{
    handle_close();
    printf("[CameraApp] 应用已退出\n");
}

int CameraApp::run()
{
    running_.store(true);

    // 尝试打开framebuffer (非必须，失败不影响拍照功能)
    if (display_->open() < 0) {
        printf("[CameraApp] 警告: 无法打开framebuffer，将仅支持拍照功能\n");
    }

    print_help();

    std::string line;
    while (running_.load()) {
        printf("\n> ");
        fflush(stdout);

        if (!std::getline(std::cin, line)) {
            break;  // EOF 或 信号中断
        }

        if (!running_.load()) break;  // 信号处理器可能在getline期间设置了标志

        // 检查采集线程是否报告了错误 (摄像头断开等)
        if (capture_error_.load()) {
            capture_error_.store(false);
            printf("[CameraApp] 检测到采集异常，正在关闭摄像头...\n");
            handle_close();
        }

        // 去除首尾空格
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        line = line.substr(start);

        if (line == "open" || line == "o") {
            handle_open();
        } else if (line == "close" || line == "c") {
            handle_close();
        } else if (line == "capture" || line == "t") {
            handle_capture();
        } else if (line.substr(0, 4) == "mode") {
            int mode = 0;
            if (line.size() > 5) {
                try { mode = std::stoi(line.substr(5)); }
                catch (...) { printf("请输入有效数字，例如: mode 1\n"); continue; }
            }
            handle_set_mode(mode);
        } else if (line == "list" || line == "l") {
            handle_list_photos();
        } else if (line.substr(0, 4) == "view") {
            int idx = 0;
            if (line.size() > 5) {
                try { idx = std::stoi(line.substr(5)); }
                catch (...) { printf("请输入有效数字，例如: view 0\n"); continue; }
            }
            handle_view_photo(idx);
        } else if (line == "help" || line == "h") {
            print_help();
        } else if (line == "quit" || line == "q") {
            running_.store(false);
        } else {
            printf("未知命令: %s (输入 help 查看帮助)\n", line.c_str());
        }
    }

    return 0;
}

void CameraApp::handle_open()
{
    if (device_->is_open()) {
        printf("摄像头已经是打开状态\n");
        return;
    }

    if (device_->open() < 0) {
        printf("打开摄像头失败!\n");
        return;
    }

    if (device_->start_streaming() < 0) {
        printf("启动视频流失败!\n");
        device_->close();
        return;
    }

    // 创建采集线程
    capture_ = std::make_unique<CaptureThread>(*device_);

    capture_->set_frame_callback(
        [this](const cv::Mat& frame) { on_frame_captured(frame); });
    capture_->set_error_callback(
        [this](const std::string& err) { on_capture_error(err); });

    // 设置处理器输出回调
    processor_->set_output_callback(
        [this](const cv::Mat& frame) { on_frame_processed(frame); });

    // 启动线程
    processor_->start();
    capture_->start();

    printf("摄像头已打开，正在采集...\n");
}

void CameraApp::handle_close()
{
    if (capture_) {
        capture_->stop();
        capture_.reset();
    }

    processor_->stop();

    if (device_->is_open()) {
        device_->close();
        printf("摄像头已关闭\n");
    }
}

void CameraApp::handle_capture()
{
    if (!device_->is_open()) {
        printf("请先打开摄像头 (输入 open)\n");
        return;
    }

    // 从缓存中获取最新帧保存 (避免与采集线程竞争dequeue_buffer)
    cv::Mat frame;
    {
        std::lock_guard<std::mutex> lock(frame_cache_mutex_);
        if (cached_frame_.empty()) {
            printf("尚未采集到画面，请稍后重试\n");
            return;
        }
        frame = cached_frame_.clone();
    }

    std::string path = photos_->save_mat(frame);

    if (!path.empty()) {
        printf("拍照成功: %s\n", path.c_str());
    }
}

void CameraApp::handle_set_mode(int mode)
{
    if (mode < 0 || mode > 6) {
        printf("无效模式! 可用模式:\n");
        printf("  0 - 不处理(原始图像)\n");
        printf("  1 - 高斯模糊\n");
        printf("  2 - 边缘检测(Canny)\n");
        printf("  3 - 灰度化\n");
        printf("  4 - 锐化\n");
        printf("  5 - 浮雕效果\n");
        printf("  6 - 卡通化\n");
        return;
    }

    processor_->set_mode(static_cast<ProcessMode>(mode));
}

void CameraApp::handle_list_photos()
{
    auto photos = photos_->list_photos();
    if (photos.empty()) {
        printf("相册为空\n");
        return;
    }

    printf("相册 (%zu 张照片):\n", photos.size());
    for (size_t i = 0; i < photos.size(); ++i) {
        printf("  [%zu] %s\n", i, photos[i].c_str());
    }
}

void CameraApp::handle_view_photo(int index)
{
    cv::Mat photo = photos_->load_photo(index);
    if (photo.empty()) {
        printf("无法加载照片\n");
        return;
    }

    if (display_->is_open()) {
        std::lock_guard<std::mutex> lock(display_mutex_);
        display_->display(photo);
        printf("照片已显示在屏幕上\n");
    } else {
        printf("framebuffer不可用，无法显示照片\n");
    }
}

void CameraApp::on_frame_captured(const cv::Mat& frame)
{
    // 缓存最新帧用于拍照
    {
        std::lock_guard<std::mutex> lock(frame_cache_mutex_);
        cached_frame_ = frame.clone();
    }

    ProcessMode mode = processor_->get_mode();

    if (mode != ProcessMode::None) {
        // 提交给处理线程
        processor_->submit_frame(frame);
    } else {
        // 直接显示
        on_frame_processed(frame);
    }
}

void CameraApp::on_frame_processed(const cv::Mat& frame)
{
    if (display_->is_open()) {
        std::lock_guard<std::mutex> lock(display_mutex_);
        display_->display(frame);
    }
}

void CameraApp::on_capture_error(const std::string& error)
{
    fprintf(stderr, "[CameraApp] 采集错误: %s\n", error.c_str());
    // 不在此处调用handle_close()——因为本回调运行在采集线程上下文中，
    // handle_close()会调capture_->stop()->thread_.join()，
    // 等于采集线程join自己，必然死锁。
    // 只设标志，让主循环来做清理。
    capture_error_.store(true);
}

void CameraApp::print_help()
{
    printf("\n");
    printf("========================================\n");
    printf("  IMX6ULL V4L2 Camera Application\n");
    printf("========================================\n");
    printf("命令:\n");
    printf("  open   (o)     - 打开摄像头\n");
    printf("  close  (c)     - 关闭摄像头\n");
    printf("  capture(t)     - 拍照\n");
    printf("  mode <N>       - 设置处理模式 (0-6)\n");
    printf("  list   (l)     - 查看相册\n");
    printf("  view <N>       - 查看第N张照片\n");
    printf("  help   (h)     - 显示帮助\n");
    printf("  quit   (q)     - 退出\n");
    printf("========================================\n");
}
