#include "frame_buffer.h"

#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>

FrameBuffer::FrameBuffer(const std::string& device_path)
    : device_path_(device_path)
{
}

FrameBuffer::~FrameBuffer()
{
    close();
}

int FrameBuffer::open()
{
    fd_ = ::open(device_path_.c_str(), O_RDWR);
    if (fd_ < 0) {
        perror("[FrameBuffer] 打开framebuffer失败");
        return -1;
    }

    // 获取屏幕信息
    struct fb_var_screeninfo vinfo;
    if (ioctl(fd_, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        perror("[FrameBuffer] 获取屏幕参数失败");
        ::close(fd_);
        fd_ = -1;
        return -2;
    }

    xres_ = vinfo.xres;
    yres_ = vinfo.yres;
    bits_per_pixel_ = vinfo.bits_per_pixel;

    struct fb_fix_screeninfo finfo;
    if (ioctl(fd_, FBIOGET_FSCREENINFO, &finfo) < 0) {
        perror("[FrameBuffer] 获取固定屏幕参数失败");
        ::close(fd_);
        fd_ = -1;
        return -3;
    }

    line_length_ = finfo.line_length;
    screen_size_ = static_cast<long>(line_length_) * yres_;

    // 映射显存
    fb_mem_ = static_cast<char*>(
        mmap(nullptr, screen_size_, PROT_READ | PROT_WRITE,
             MAP_SHARED, fd_, 0));

    if (fb_mem_ == MAP_FAILED) {
        perror("[FrameBuffer] mmap显存失败");
        fb_mem_ = nullptr;
        ::close(fd_);
        fd_ = -1;
        return -4;
    }

    printf("[FrameBuffer] 屏幕: %dx%d, %dbpp\n",
           xres_, yres_, bits_per_pixel_);
    return 0;
}

void FrameBuffer::close()
{
    if (fb_mem_ && fb_mem_ != MAP_FAILED) {
        munmap(fb_mem_, screen_size_);
        fb_mem_ = nullptr;
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

void FrameBuffer::display(const cv::Mat& image)
{
    if (!is_open() || image.empty()) return;

    // 缩放图像以适应屏幕
    cv::Mat resized;
    double scale_x = static_cast<double>(xres_) / image.cols;
    double scale_y = static_cast<double>(yres_) / image.rows;
    double scale = std::min(scale_x, scale_y);

    cv::resize(image, resized, cv::Size(), scale, scale, cv::INTER_LINEAR);

    // 计算居中偏移
    int offset_x = (xres_ - resized.cols) / 2;
    int offset_y = (yres_ - resized.rows) / 2;

    int bytes_per_pixel = bits_per_pixel_ / 8;

    if (bits_per_pixel_ == 32) {
        // ARGB8888: BGR → BGRA，按行批量写入
        cv::Mat bgra;
        cv::cvtColor(resized, bgra, cv::COLOR_BGR2BGRA);

        for (int y = 0; y < bgra.rows; ++y) {
            long fb_offset = static_cast<long>(y + offset_y) * line_length_
                           + static_cast<long>(offset_x) * bytes_per_pixel;
            memcpy(fb_mem_ + fb_offset, bgra.ptr(y),
                   bgra.cols * bytes_per_pixel);
        }
    } else if (bits_per_pixel_ == 16) {
        // RGB565: 按行转换写入
        for (int y = 0; y < resized.rows; ++y) {
            const unsigned char* src_row = resized.ptr(y);
            long fb_offset = static_cast<long>(y + offset_y) * line_length_
                           + static_cast<long>(offset_x) * bytes_per_pixel;
            unsigned short* dst_row =
                reinterpret_cast<unsigned short*>(fb_mem_ + fb_offset);

            for (int x = 0; x < resized.cols; ++x) {
                unsigned char b = src_row[x * 3 + 0];
                unsigned char g = src_row[x * 3 + 1];
                unsigned char r = src_row[x * 3 + 2];
                dst_row[x] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            }
        }
    } else {
        // 其他色深: 回退到逐像素写入
        for (int y = 0; y < resized.rows; ++y) {
            for (int x = 0; x < resized.cols; ++x) {
                cv::Vec3b pixel = resized.at<cv::Vec3b>(y, x);
                write_pixel(x + offset_x, y + offset_y,
                           pixel[0], pixel[1], pixel[2]);
            }
        }
    }
}

void FrameBuffer::clear()
{
    if (fb_mem_ && screen_size_ > 0) {
        memset(fb_mem_, 0, screen_size_);
    }
}

void FrameBuffer::write_pixel(int x, int y, unsigned char b,
                               unsigned char g, unsigned char r)
{
    if (x < 0 || x >= xres_ || y < 0 || y >= yres_) return;

    long offset = static_cast<long>(y) * line_length_ +
                  static_cast<long>(x) * (bits_per_pixel_ / 8);

    if (bits_per_pixel_ == 32) {
        // ARGB8888
        fb_mem_[offset + 0] = b;
        fb_mem_[offset + 1] = g;
        fb_mem_[offset + 2] = r;
        fb_mem_[offset + 3] = 0xFF;  // alpha
    } else if (bits_per_pixel_ == 16) {
        // RGB565
        unsigned short color =
            ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
        *reinterpret_cast<unsigned short*>(fb_mem_ + offset) = color;
    }
}
