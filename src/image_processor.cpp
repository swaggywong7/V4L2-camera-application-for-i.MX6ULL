#include "image_processor.h"

#include <cstdio>

// ==================== 策略实现 ====================

GaussianBlurAlgorithm::GaussianBlurAlgorithm(int kernel_size)
    : kernel_size_(kernel_size)
{
}

cv::Mat GaussianBlurAlgorithm::process(const cv::Mat& input)
{
    cv::Mat output;
    cv::GaussianBlur(input, output, cv::Size(kernel_size_, kernel_size_), 0);
    return output;
}

cv::Mat EdgeDetectAlgorithm::process(const cv::Mat& input)
{
    cv::Mat gray, edges, output;
    cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(gray, gray, cv::Size(5, 5), 0);
    cv::Canny(gray, edges, low_threshold_, high_threshold_);
    cv::cvtColor(edges, output, cv::COLOR_GRAY2BGR);
    return output;
}

EdgeDetectAlgorithm::EdgeDetectAlgorithm(double low, double high)
    : low_threshold_(low), high_threshold_(high)
{
}

cv::Mat GrayscaleAlgorithm::process(const cv::Mat& input)
{
    cv::Mat gray, output;
    cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
    cv::cvtColor(gray, output, cv::COLOR_GRAY2BGR);
    return output;
}

cv::Mat SharpenAlgorithm::process(const cv::Mat& input)
{
    cv::Mat output;
    cv::Mat kernel = (cv::Mat_<float>(3, 3) <<
         0, -1,  0,
        -1,  5, -1,
         0, -1,  0);
    cv::filter2D(input, output, input.depth(), kernel);
    return output;
}

cv::Mat EmbossAlgorithm::process(const cv::Mat& input)
{
    cv::Mat gray, embossed, output;
    cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
    cv::Mat kernel = (cv::Mat_<float>(3, 3) <<
        -2, -1,  0,
        -1,  1,  1,
         0,  1,  2);
    cv::filter2D(gray, embossed, gray.depth(), kernel);
    embossed = embossed + 128;
    cv::cvtColor(embossed, output, cv::COLOR_GRAY2BGR);
    return output;
}

cv::Mat CartoonAlgorithm::process(const cv::Mat& input)
{
    cv::Mat gray, edges, color, output;

    // 边缘提取
    cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
    cv::medianBlur(gray, gray, 7);
    cv::adaptiveThreshold(gray, edges, 255,
                         cv::ADAPTIVE_THRESH_MEAN_C,
                         cv::THRESH_BINARY, 9, 2);

    // 双边滤波平滑
    cv::bilateralFilter(input, color, 9, 300, 300);

    // 合并
    cv::cvtColor(edges, edges, cv::COLOR_GRAY2BGR);
    cv::bitwise_and(color, edges, output);

    return output;
}

// ==================== ImageProcessor 实现 ====================

ImageProcessor::ImageProcessor()
{
    register_algorithms();
    printf("[ImageProcessor] 处理器已创建\n");
}

ImageProcessor::~ImageProcessor()
{
    stop();
    printf("[ImageProcessor] 处理器已销毁\n");
}

void ImageProcessor::register_algorithms()
{
    algorithms_[ProcessMode::GaussianBlur] = std::make_unique<GaussianBlurAlgorithm>();
    algorithms_[ProcessMode::EdgeDetect]   = std::make_unique<EdgeDetectAlgorithm>();
    algorithms_[ProcessMode::Grayscale]    = std::make_unique<GrayscaleAlgorithm>();
    algorithms_[ProcessMode::Sharpen]      = std::make_unique<SharpenAlgorithm>();
    algorithms_[ProcessMode::Emboss]       = std::make_unique<EmbossAlgorithm>();
    algorithms_[ProcessMode::Cartoon]      = std::make_unique<CartoonAlgorithm>();
}

void ImageProcessor::set_mode(ProcessMode mode)
{
    current_mode_.store(mode);
    const char* names[] = {"None", "GaussianBlur", "EdgeDetect",
                           "Grayscale", "Sharpen", "Emboss", "Cartoon"};
    int idx = static_cast<int>(mode);
    printf("[ImageProcessor] 切换处理模式: %s\n",
           (idx >= 0 && idx < 7) ? names[idx] : "Unknown");
}

ProcessMode ImageProcessor::get_mode() const
{
    return current_mode_.load();
}

void ImageProcessor::submit_frame(const cv::Mat& frame)
{
    {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        pending_frame_ = frame;  // 引用计数 +1，零拷贝；process() 输出新 Mat 不修改 input
        has_new_frame_ = true;
    }
    frame_cv_.notify_one();
}

void ImageProcessor::set_output_callback(OutputCallback callback)
{
    std::lock_guard<std::mutex> lock(callback_mutex_);
    output_callback_ = std::move(callback);
}

void ImageProcessor::start()
{
    if (running_.load()) return;

    running_.store(true);
    thread_ = std::thread(&ImageProcessor::process_loop, this);
    printf("[ImageProcessor] 处理线程已启动\n");
}

void ImageProcessor::stop()
{
    if (!running_.load()) return;

    running_.store(false);
    frame_cv_.notify_all();  // 唤醒可能在等待的线程

    if (thread_.joinable()) {
        thread_.join();
    }
    printf("[ImageProcessor] 处理线程已停止\n");
}

void ImageProcessor::process_loop()
{
    while (running_.load()) {
        cv::Mat frame;

        // 等待新帧 (生产者-消费者模式)
        {
            std::unique_lock<std::mutex> lock(frame_mutex_);
            frame_cv_.wait_for(lock, std::chrono::milliseconds(100),
                              [this] { return has_new_frame_ || !running_.load(); });

            if (!running_.load()) break;
            if (!has_new_frame_) continue;

            frame = std::move(pending_frame_);
            has_new_frame_ = false;
        }

        ProcessMode mode = current_mode_.load();

        // None模式直接输出原始帧
        if (mode == ProcessMode::None) {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (output_callback_) {
                output_callback_(frame);
            }
            continue;
        }

        // 查找并执行对应的处理算法
        auto it = algorithms_.find(mode);
        if (it != algorithms_.end()) {
            try {
                cv::Mat result = it->second->process(frame);
                std::lock_guard<std::mutex> lock(callback_mutex_);
                if (output_callback_) {
                    output_callback_(result);
                }
            } catch (const cv::Exception& e) {
                fprintf(stderr, "[ImageProcessor] OpenCV异常: %s\n", e.what());
            }
        }
    }
}
