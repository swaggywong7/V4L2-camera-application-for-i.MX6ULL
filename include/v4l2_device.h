#ifndef V4L2_DEVICE_H
#define V4L2_DEVICE_H

#include <cstdint>
#include <string>
#include <linux/videodev2.h>

// V4L2设备RAII封装类
// 设计思路: 将原始的V4L2系统调用封装为面向对象接口，
//          利用RAII确保资源在任何情况下都能正确释放
class V4L2Device {
public:
    static constexpr int kDefaultWidth = 640;
    static constexpr int kDefaultHeight = 480;
    static constexpr int kBufferCount = 4;

    // 帧数据结构 - 替代原来散落在各处的裸指针+长度
    struct Frame {
        const unsigned char* data;
        unsigned int size;
        int index;  // 缓冲区索引，归还时需要
    };

    // 禁止拷贝，允许移动 (资源独占语义)
    V4L2Device(const V4L2Device&) = delete;
    V4L2Device& operator=(const V4L2Device&) = delete;
    V4L2Device(V4L2Device&& other) noexcept;
    V4L2Device& operator=(V4L2Device&& other) noexcept;

    explicit V4L2Device(const std::string& device_path = "/dev/video1");
    ~V4L2Device();

    // 打开设备并初始化
    // 返回值: 0成功, 负数失败 (错误码定义见实现)
    int open(int width = kDefaultWidth, int height = kDefaultHeight,
             uint32_t pixel_format = V4L2_PIX_FMT_MJPEG);

    // 关闭设备，释放所有资源
    void close();

    // 开始/停止视频流
    int start_streaming();
    int stop_streaming();

    // 采集一帧 (会阻塞直到有数据)
    // 成功返回Frame，失败返回data=nullptr的Frame
    Frame dequeue_buffer();

    // 将缓冲区归还给驱动
    int enqueue_buffer(int index);

    // 打印设备能力信息
    void print_capabilities() const;

    bool is_open() const { return fd_ >= 0; }
    int fd() const { return fd_; }

private:
    // 查询并验证设备能力
    int query_capabilities();

    // 枚举支持的格式和分辨率
    void enumerate_formats() const;

    // 设置视频格式
    int set_format(int width, int height, uint32_t pixel_format);

    // 申请并映射内核缓冲区
    int request_buffers();

    // 释放映射的缓冲区
    void release_buffers();

    std::string device_path_;
    int fd_ = -1;
    bool streaming_ = false;

    // 用户空间映射缓冲区
    char* user_buffers_[kBufferCount] = {};
    int buffer_lengths_[kBufferCount] = {};
    int buffer_count_ = 0;
};

#endif // V4L2_DEVICE_H
