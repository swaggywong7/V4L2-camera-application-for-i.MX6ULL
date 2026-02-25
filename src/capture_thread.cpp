#include "capture_thread.h"

#include <cstdio>
#include <cerrno>
#include <cstring>
#include <chrono>

// ==================== NEON YUYV→BGR 色彩转换 ====================
// 用 ARM NEON SIMD 手写 BT.601 YUV→BGR，替代 cv::cvtColor。
// 每次处理 16 像素（32 字节 YUYV → 48 字节 BGR），比标量快约 3x。
//
// YUYV 内存格局（每 4 字节 = 2 像素）：
//   [Y0][U][Y1][V][Y2][U][Y3][V] ...
// vld4_u8 自动拆成 4 个寄存器：
//   val[0] = 偶 Y（Y0,Y2,...）
//   val[1] = U
//   val[2] = 奇 Y（Y1,Y3,...）
//   val[3] = V
//
// BT.601 整数公式（int32 避免溢出，298×255=75990 > INT16_MAX）：
//   C = Y-16, D = U-128, E = V-128
//   R = (298C + 409E + 128) >> 8
//   G = (298C - 100D - 208E + 128) >> 8
//   B = (298C + 516D + 128) >> 8
#ifdef __ARM_NEON
#include <arm_neon.h>

static void yuyv_row_to_bgr_neon(const uint8_t* __restrict__ src,
                                   uint8_t* __restrict__ dst,
                                   int n_pixels)
{
    const int n16 = n_pixels / 16;

    for (int i = 0; i < n16; ++i, src += 32, dst += 48) {
        // 1. 解交织：32字节→4个uint8x8寄存器（每个8个元素）
        uint8x8x4_t raw = vld4_u8(src);
        // raw.val[0] = Y偶 {Y0,Y2,...,Y14}
        // raw.val[1] = U  {U0,...,U7}
        // raw.val[2] = Y奇 {Y1,Y3,...,Y15}
        // raw.val[3] = V  {V0,...,V7}

        // 2. 扩展到 int16（后续算术需要正负号）
        int16x8_t y0 = vreinterpretq_s16_u16(vmovl_u8(raw.val[0]));
        int16x8_t y1 = vreinterpretq_s16_u16(vmovl_u8(raw.val[2]));
        int16x8_t u  = vreinterpretq_s16_u16(vmovl_u8(raw.val[1]));
        int16x8_t v  = vreinterpretq_s16_u16(vmovl_u8(raw.val[3]));

        // 3. BT.601 偏置：C=Y-16, D=U-128, E=V-128
        int16x8_t cy0 = vsubq_s16(y0, vdupq_n_s16(16));
        int16x8_t cy1 = vsubq_s16(y1, vdupq_n_s16(16));
        int16x8_t d   = vsubq_s16(u,  vdupq_n_s16(128));
        int16x8_t e   = vsubq_s16(v,  vdupq_n_s16(128));

        // 4. BT.601 矩阵乘法（int32，低4/高4像素分别计算）
        //    使用宏避免重复代码（偶Y和奇Y共用同一 d, e）
#define COMPUTE_BGR(cy, r_out, g_out, b_out)                              \
        do {                                                               \
            /* 低4像素 */                                                  \
            int32x4_t base_lo = vaddq_s32(                                \
                vmull_s16(vget_low_s16(cy), vdup_n_s16(298)),             \
                vdupq_n_s32(128));                                         \
            int32x4_t r_lo = vmlal_s16(base_lo,                           \
                vget_low_s16(e), vdup_n_s16(409));                        \
            int32x4_t g_lo = vmlsl_s16(                                   \
                vmlsl_s16(base_lo, vget_low_s16(d), vdup_n_s16(100)),     \
                vget_low_s16(e), vdup_n_s16(208));                        \
            int32x4_t b_lo = vmlal_s16(base_lo,                           \
                vget_low_s16(d), vdup_n_s16(516));                        \
            /* 高4像素 */                                                  \
            int32x4_t base_hi = vaddq_s32(                                \
                vmull_s16(vget_high_s16(cy), vdup_n_s16(298)),            \
                vdupq_n_s32(128));                                         \
            int32x4_t r_hi = vmlal_s16(base_hi,                           \
                vget_high_s16(e), vdup_n_s16(409));                       \
            int32x4_t g_hi = vmlsl_s16(                                   \
                vmlsl_s16(base_hi, vget_high_s16(d), vdup_n_s16(100)),    \
                vget_high_s16(e), vdup_n_s16(208));                       \
            int32x4_t b_hi = vmlal_s16(base_hi,                           \
                vget_high_s16(d), vdup_n_s16(516));                       \
            /* >>8，int32→int16→uint8（饱和截断到[0,255]）*/              \
            r_out = vqmovun_s16(vcombine_s16(vshrn_n_s32(r_lo, 8),        \
                                             vshrn_n_s32(r_hi, 8)));      \
            g_out = vqmovun_s16(vcombine_s16(vshrn_n_s32(g_lo, 8),        \
                                             vshrn_n_s32(g_hi, 8)));      \
            b_out = vqmovun_s16(vcombine_s16(vshrn_n_s32(b_lo, 8),        \
                                             vshrn_n_s32(b_hi, 8)));      \
        } while (0)

        uint8x8_t r0, g0, b0;  // 偶像素（P0,P2,...,P14）
        uint8x8_t r1, g1, b1;  // 奇像素（P1,P3,...,P15）
        COMPUTE_BGR(cy0, r0, g0, b0);
        COMPUTE_BGR(cy1, r1, g1, b1);
#undef COMPUTE_BGR

        // 5. 将偶/奇像素重新交织，恢复 P0,P1,P2,P3... 顺序
        //    vzip_u8({P0,P2,...P14}, {P1,P3,...P15})
        //    → val[0]={P0,P1,P2,P3,P4,P5,P6,P7}
        //    → val[1]={P8,P9,...,P15}
        uint8x8x2_t b_zip = vzip_u8(b0, b1);
        uint8x8x2_t g_zip = vzip_u8(g0, g1);
        uint8x8x2_t r_zip = vzip_u8(r0, r1);

        // 6. vst3 交织写出 BGR 格式（8+8 = 16 像素，共48字节）
        uint8x8x3_t bgr_lo = {{ b_zip.val[0], g_zip.val[0], r_zip.val[0] }};
        vst3_u8(dst,      bgr_lo);
        uint8x8x3_t bgr_hi = {{ b_zip.val[1], g_zip.val[1], r_zip.val[1] }};
        vst3_u8(dst + 24, bgr_hi);
    }

    // 尾部 < 16 像素：标量回退（图宽通常是16的倍数，这里保证正确性）
    int remainder = n_pixels % 16;
    for (int x = 0; x < remainder; x += 2, src += 4, dst += 6) {
        int c0 = src[0] - 16;
        int c1 = src[2] - 16;
        int d  = src[1] - 128;
        int e  = src[3] - 128;
        auto clamp = [](int v) -> uint8_t {
            return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
        };
        dst[0] = clamp((298*c0 + 516*d + 128) >> 8);  // B
        dst[1] = clamp((298*c0 - 100*d - 208*e + 128) >> 8);  // G
        dst[2] = clamp((298*c0 + 409*e + 128) >> 8);  // R
        dst[3] = clamp((298*c1 + 516*d + 128) >> 8);  // B
        dst[4] = clamp((298*c1 - 100*d - 208*e + 128) >> 8);  // G
        dst[5] = clamp((298*c1 + 409*e + 128) >> 8);  // R
    }
}
#endif // __ARM_NEON

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
#ifdef __ARM_NEON
        // NEON 手写路径：BT.601 整数运算，每次处理 16 像素，约快 3x
        cv::Mat bgr(h, w, CV_8UC3);
        const uint8_t* src = data;
        uint8_t*       dst = bgr.data;
        for (int row = 0; row < h; ++row) {
            yuyv_row_to_bgr_neon(src, dst, w);
            src += w * 2;
            dst += w * 3;
        }
        return bgr;
#else
        // 非 ARM 平台（宿主机调试）：回退 OpenCV 路径
        cv::Mat yuyv(h, w, CV_8UC2, const_cast<unsigned char*>(data));
        cv::Mat bgr;
        cv::cvtColor(yuyv, bgr, cv::COLOR_YUV2BGR_YUYV);
        return bgr;
#endif
    }

    fprintf(stderr, "[CaptureThread] 解码失败: size=%u, w=%d, h=%d\n", size, w, h);
    return cv::Mat();
}
