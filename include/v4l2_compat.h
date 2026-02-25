#ifndef V4L2_COMPAT_H
#define V4L2_COMPAT_H

// 解决 Ubuntu 24.04 (64-bit timeval) 与 32-bit 内核 (32-bit timeval) 的 ABI 不兼容问题
// struct v4l2_buffer 中包含 struct timeval，其大小影响 ioctl 编号
// 主机: sizeof(timeval)=16 → sizeof(v4l2_buffer)=80
// 板子: sizeof(timeval)=8  → sizeof(v4l2_buffer)=72
// 解法: 用显式 32-bit timeval 重新定义 v4l2_buffer_compat，并重算 ioctl 编号

#include <linux/videodev2.h>
#include <sys/ioctl.h>

// 32-bit timeval（匹配板子内核 ABI）
struct v4l2_timeval_compat {
    __s32 tv_sec;
    __s32 tv_usec;
};

// 与 32-bit 内核 ABI 兼容的 v4l2_buffer
struct v4l2_buffer_compat {
    __u32                       index;
    __u32                       type;
    __u32                       bytesused;
    __u32                       flags;
    __u32                       field;
    struct v4l2_timeval_compat  timestamp;   // 8 bytes（而不是 Ubuntu 的 16 bytes）
    struct v4l2_timecode        timecode;
    __u32                       sequence;
    __u32                       memory;
    union {
        __u32       offset;
        __u32       userptr;   // 32-bit ARM 指针
        __s32       fd;
    } m;
    __u32                       length;
    __u32                       reserved2;
    __u32                       reserved;
} __attribute__((packed));

// 用 v4l2_buffer_compat 重算 ioctl 编号
#define VIDIOC_QUERYBUF_COMPAT  _IOWR('V',  9, struct v4l2_buffer_compat)
#define VIDIOC_QBUF_COMPAT      _IOWR('V', 15, struct v4l2_buffer_compat)
#define VIDIOC_DQBUF_COMPAT     _IOWR('V', 17, struct v4l2_buffer_compat)

#endif // V4L2_COMPAT_H
