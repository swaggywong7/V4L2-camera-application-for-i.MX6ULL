#include "capture_thread.h"

#include <cstdio>
#include <cerrno>
#include <cstring>

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
    while (running_.load()) {
        // 从设备取出一帧
        V4L2Device::Frame frame = device_.dequeue_buffer();

        if (!frame.data) {
            if (errno == EINTR) continue;

            std::string err = "[CaptureThread] DQBUF失败: ";
            err += strerror(errno);

            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (error_callback_) {
                error_callback_(err);
            }
            break;
        }

        // 解码MJPEG为cv::Mat
        cv::Mat mat = decode_mjpeg(frame.data, frame.size);

        // 归还缓冲区给驱动 (无论解码是否成功都要归还)
        device_.enqueue_buffer(frame.index);

        if (!mat.empty()) {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (frame_callback_) {
                frame_callback_(mat);
            }
        }
    }
}

cv::Mat CaptureThread::decode_mjpeg(const unsigned char* data, unsigned int size)
{
    // 使用OpenCV的imdecode解码MJPEG数据
    std::vector<unsigned char> buf(data, data + size);
    cv::Mat decoded = cv::imdecode(buf, cv::IMREAD_COLOR);

    if (decoded.empty()) {
        fprintf(stderr, "[CaptureThread] MJPEG解码失败\n");
    }

    return decoded;
}
