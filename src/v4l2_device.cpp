#include "v4l2_device.h"

#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

V4L2Device::V4L2Device(const std::string& device_path)
    : device_path_(device_path)
{
}

V4L2Device::V4L2Device(V4L2Device&& other) noexcept
    : device_path_(std::move(other.device_path_))
    , fd_(other.fd_)
    , streaming_(other.streaming_)
    , buffer_count_(other.buffer_count_)
{
    for (int i = 0; i < kBufferCount; ++i) {
        user_buffers_[i] = other.user_buffers_[i];
        buffer_lengths_[i] = other.buffer_lengths_[i];
        other.user_buffers_[i] = nullptr;
        other.buffer_lengths_[i] = 0;
    }
    other.fd_ = -1;
    other.streaming_ = false;
    other.buffer_count_ = 0;
}

V4L2Device& V4L2Device::operator=(V4L2Device&& other) noexcept
{
    if (this != &other) {
        close();
        device_path_ = std::move(other.device_path_);
        fd_ = other.fd_;
        streaming_ = other.streaming_;
        buffer_count_ = other.buffer_count_;
        for (int i = 0; i < kBufferCount; ++i) {
            user_buffers_[i] = other.user_buffers_[i];
            buffer_lengths_[i] = other.buffer_lengths_[i];
            other.user_buffers_[i] = nullptr;
            other.buffer_lengths_[i] = 0;
        }
        other.fd_ = -1;
        other.streaming_ = false;
        other.buffer_count_ = 0;
    }
    return *this;
}

V4L2Device::~V4L2Device()
{
    close();
}

int V4L2Device::open(int width, int height, uint32_t pixel_format)
{
    if (fd_ >= 0) {
        fprintf(stderr, "[V4L2Device] 设备已打开\n");
        return -1;
    }

    // 1. 打开设备文件
    fd_ = ::open(device_path_.c_str(), O_RDWR);
    if (fd_ < 0) {
        perror("[V4L2Device] 打开设备失败");
        return -1;
    }

    // 2. 查询设备能力
    int ret = query_capabilities();
    if (ret < 0) {
        ::close(fd_);
        fd_ = -1;
        return ret;
    }

    // 3. 枚举支持的格式 (打印信息)
    enumerate_formats();

    // 4. 设置视频格式
    ret = set_format(width, height, pixel_format);
    if (ret < 0) {
        ::close(fd_);
        fd_ = -1;
        return ret;
    }

    // 5. 申请并映射缓冲区
    ret = request_buffers();
    if (ret < 0) {
        ::close(fd_);
        fd_ = -1;
        return ret;
    }

    printf("[V4L2Device] 设备初始化成功: %s (%dx%d)\n",
           device_path_.c_str(), width, height);
    return 0;
}

void V4L2Device::close()
{
    if (fd_ < 0) return;

    if (streaming_) {
        stop_streaming();
    }
    
    release_buffers();

    ::close(fd_);
    fd_ = -1;
    printf("[V4L2Device] 设备已关闭\n");
}

int V4L2Device::start_streaming()
{
    if (!is_open() || streaming_) return -1;

    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd_, VIDIOC_STREAMON, &type) < 0) {
        perror("[V4L2Device] 启动视频流失败");
        return -1;
    }

    streaming_ = true;
    printf("[V4L2Device] 视频流已启动\n");
    return 0;
}

int V4L2Device::stop_streaming()
{
    if (!is_open() || !streaming_) return -1;

    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd_, VIDIOC_STREAMOFF, &type) < 0) {
        perror("[V4L2Device] 停止视频流失败");
        return -1;
    }

    streaming_ = false;
    printf("[V4L2Device] 视频流已停止\n");
    return 0;
}

V4L2Device::Frame V4L2Device::dequeue_buffer()
{
    Frame frame = {nullptr, 0, -1};

    struct v4l2_buffer buffer;
    memset(&buffer, 0, sizeof(buffer));
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd_, VIDIOC_DQBUF, &buffer) < 0) {
        return frame;
    }

    frame.data = reinterpret_cast<const unsigned char*>(user_buffers_[buffer.index]);
    frame.size = buffer.bytesused;
    frame.index = buffer.index;
    return frame;
}

int V4L2Device::enqueue_buffer(int index)
{
    struct v4l2_buffer buffer;
    memset(&buffer, 0, sizeof(buffer));
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;
    buffer.index = index;

    if (ioctl(fd_, VIDIOC_QBUF, &buffer) < 0) {
        perror("[V4L2Device] 归还缓冲区失败");
        return -1;
    }
    return 0;
}

int V4L2Device::query_capabilities()
{
    struct v4l2_capability cap;
    if (ioctl(fd_, VIDIOC_QUERYCAP, &cap) < 0) {
        perror("[V4L2Device] 查询设备能力失败");
        return -2;
    }

    printf("[V4L2Device] 驱动: %s, 设备: %s\n", cap.driver, cap.card);

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "[V4L2Device] 设备不支持视频采集\n");
        return -2;
    }

    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "[V4L2Device] 设备不支持流式传输\n");
        return -3;
    }

    return 0;
}

void V4L2Device::enumerate_formats() const
{
    struct v4l2_fmtdesc fmtdesc;
    memset(&fmtdesc, 0, sizeof(fmtdesc));
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    printf("[V4L2Device] 支持的格式:\n");

    for (int i = 0; ; ++i) {
        fmtdesc.index = i;
        if (ioctl(fd_, VIDIOC_ENUM_FMT, &fmtdesc) < 0)
            break;

        printf("  [%d] %s (%c%c%c%c)\n", i, fmtdesc.description,
               fmtdesc.pixelformat & 0xff,
               (fmtdesc.pixelformat >> 8) & 0xff,
               (fmtdesc.pixelformat >> 16) & 0xff,
               (fmtdesc.pixelformat >> 24) & 0xff);

        // 列出该格式支持的分辨率
        struct v4l2_frmsizeenum frmsize;
        frmsize.pixel_format = fmtdesc.pixelformat;
        for (int j = 0; ; ++j) {
            frmsize.index = j;
            if (ioctl(fd_, VIDIOC_ENUM_FRAMESIZES, &frmsize) < 0)
                break;
            if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
                printf("    %dx%d\n", frmsize.discrete.width, frmsize.discrete.height);
            }
        }
    }
}

int V4L2Device::set_format(int width, int height, uint32_t pixel_format)
{
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = pixel_format;

    if (ioctl(fd_, VIDIOC_S_FMT, &fmt) < 0) {
        perror("[V4L2Device] 设置视频格式失败");
        return -4;
    }

    printf("[V4L2Device] 格式已设置: %dx%d\n",
           fmt.fmt.pix.width, fmt.fmt.pix.height);
    return 0;
}

int V4L2Device::request_buffers()
{
    struct v4l2_requestbuffers reqbuf;
    memset(&reqbuf, 0, sizeof(reqbuf));
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.count = kBufferCount;
    reqbuf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd_, VIDIOC_REQBUFS, &reqbuf) < 0) {
        perror("[V4L2Device] 申请缓冲区失败");
        return -5;
    }

    buffer_count_ = reqbuf.count;

    for (int i = 0; i < buffer_count_; ++i) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        // 查询缓冲区信息
        if (ioctl(fd_, VIDIOC_QUERYBUF, &buf) < 0) {
            perror("[V4L2Device] 查询缓冲区失败");
            release_buffers();
            return -5;
        }
        // 映射缓冲区到用户空间
        user_buffers_[i] = static_cast<char*>(
            mmap(nullptr, buf.length, PROT_READ | PROT_WRITE,
                 MAP_SHARED, fd_, buf.m.offset));
        if (user_buffers_[i] == MAP_FAILED) {
            perror("[V4L2Device] mmap失败");
            user_buffers_[i] = nullptr;
            release_buffers();
            return -5;
        }
        buffer_lengths_[i] = buf.length;

        // 将缓冲区加入内核队列
        if (ioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
            perror("[V4L2Device] 入队缓冲区失败");
            release_buffers();
            return -5;
        }
    }

    printf("[V4L2Device] 已映射 %d 个缓冲区\n", buffer_count_);
    return 0;
}

void V4L2Device::release_buffers()
{
    for (int i = 0; i < kBufferCount; ++i) {
        if (user_buffers_[i] && user_buffers_[i] != MAP_FAILED) {
            munmap(user_buffers_[i], buffer_lengths_[i]);
        }
        user_buffers_[i] = nullptr;
        buffer_lengths_[i] = 0;
    }
    buffer_count_ = 0;
}
