#ifndef IMAGE_PROCESSOR_H
#define IMAGE_PROCESSOR_H

#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <memory>
#include <unordered_map>
#include <opencv2/opencv.hpp>

// ==================== 策略模式：图像处理算法 ====================
// 设计思路: 将原来switch-case中的每个处理分支抽取为独立的策略类，
//          符合开闭原则——新增算法只需添加新类，无需修改已有代码

// 处理算法基类 (策略接口)
class ProcessAlgorithm {
public:
    virtual ~ProcessAlgorithm() = default;
    virtual cv::Mat process(const cv::Mat& input) = 0;
    virtual std::string name() const = 0;
};

// 具体策略：高斯模糊
class GaussianBlurAlgorithm : public ProcessAlgorithm {
public:
    explicit GaussianBlurAlgorithm(int kernel_size = 15);
    cv::Mat process(const cv::Mat& input) override;
    std::string name() const override { return "GaussianBlur"; }
private:
    int kernel_size_;
};

// 具体策略：Canny边缘检测
class EdgeDetectAlgorithm : public ProcessAlgorithm {
public:
    EdgeDetectAlgorithm(double low_threshold = 50, double high_threshold = 150);
    cv::Mat process(const cv::Mat& input) override;
    std::string name() const override { return "EdgeDetect"; }
private:
    double low_threshold_;
    double high_threshold_;
};

// 具体策略：灰度化
class GrayscaleAlgorithm : public ProcessAlgorithm {
public:
    cv::Mat process(const cv::Mat& input) override;
    std::string name() const override { return "Grayscale"; }
};

// 具体策略：锐化
class SharpenAlgorithm : public ProcessAlgorithm {
public:
    cv::Mat process(const cv::Mat& input) override;
    std::string name() const override { return "Sharpen"; }
};

// 具体策略：浮雕效果
class EmbossAlgorithm : public ProcessAlgorithm {
public:
    cv::Mat process(const cv::Mat& input) override;
    std::string name() const override { return "Emboss"; }
};

// 具体策略：卡通化
class CartoonAlgorithm : public ProcessAlgorithm {
public:
    cv::Mat process(const cv::Mat& input) override;
    std::string name() const override { return "Cartoon"; }
};

// ==================== 处理模式枚举 ====================
enum class ProcessMode {
    None = 0,
    GaussianBlur,
    EdgeDetect,
    Grayscale,
    Sharpen,
    Emboss,
    Cartoon
};

// ==================== 图像处理线程 (生产者-消费者模型) ====================
// 设计思路: 用condition_variable替代原来的轮询+sleep方式，
//          大幅降低CPU占用并提升响应速度
class ImageProcessor {
public:
    using OutputCallback = std::function<void(const cv::Mat&)>;

    ImageProcessor(const ImageProcessor&) = delete;
    ImageProcessor& operator=(const ImageProcessor&) = delete;

    ImageProcessor();
    ~ImageProcessor();

    // 设置处理模式
    void set_mode(ProcessMode mode);
    ProcessMode get_mode() const;

    // 提交待处理帧 (生产者)
    void submit_frame(const cv::Mat& frame);

    // 注册输出回调
    void set_output_callback(OutputCallback callback);

    // 启动/停止处理线程
    void start();
    void stop();

    bool is_running() const { return running_.load(); }

private:
    // 线程主函数 (消费者)
    void process_loop();

    // 注册所有内置算法
    void register_algorithms();

    std::thread thread_;
    std::atomic<bool> running_{false};

    // 生产者-消费者同步
    std::mutex frame_mutex_;
    std::condition_variable frame_cv_;
    cv::Mat pending_frame_;
    bool has_new_frame_ = false;

    // 处理模式 (原子操作保证线程安全)
    std::atomic<ProcessMode> current_mode_{ProcessMode::None};

    // 算法注册表 (策略模式 + 简单工厂)
    std::unordered_map<ProcessMode, std::unique_ptr<ProcessAlgorithm>> algorithms_;

    std::mutex callback_mutex_;
    OutputCallback output_callback_;
};

#endif // IMAGE_PROCESSOR_H
