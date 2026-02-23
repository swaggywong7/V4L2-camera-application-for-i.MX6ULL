#ifndef FRAME_BUFFER_H
#define FRAME_BUFFER_H

#include <string>
#include <opencv2/opencv.hpp>

// Linux FrameBuffer显示封装类
// 设计思路: 替代原来Qt的QLabel显示方式，
//          在嵌入式Linux上直接操作framebuffer硬件进行图像显示，
//          这才是嵌入式开发的正确方式，不依赖GUI框架
class FrameBuffer {
public:
    FrameBuffer(const FrameBuffer&) = delete;
    FrameBuffer& operator=(const FrameBuffer&) = delete;

    explicit FrameBuffer(const std::string& device_path = "/dev/fb0");
    ~FrameBuffer();

    // 打开framebuffer设备
    int open();
    void close();

    // 将OpenCV Mat直接显示到屏幕
    void display(const cv::Mat& image);

    // 清屏
    void clear();

    bool is_open() const { return fd_ >= 0; }
    int width() const { return xres_; }
    int height() const { return yres_; }
    int bpp() const { return bits_per_pixel_; }

private:
    // 将BGR像素转为framebuffer格式 (通常是RGB565或ARGB8888)
    void write_pixel(int x, int y, unsigned char b, unsigned char g, unsigned char r);

    std::string device_path_;
    int fd_ = -1;
    char* fb_mem_ = nullptr;       // mmap映射的显存
    long screen_size_ = 0;         // 显存大小
    int xres_ = 0;                 // 水平分辨率
    int yres_ = 0;                 // 垂直分辨率
    int bits_per_pixel_ = 0;       // 每像素位数
    int line_length_ = 0;          // 每行字节数
};

#endif // FRAME_BUFFER_H
