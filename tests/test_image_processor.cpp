#include "test_framework.h"
#include "image_processor.h"

#include <atomic>
#include <thread>
#include <chrono>
#include <mutex>
#include <opencv2/opencv.hpp>

// ---- 工具函数 ----

// 创建一张彩色测试图像 (BGR 格式)
static cv::Mat make_test_image(int w = 320, int h = 240) {
    cv::Mat img(h, w, CV_8UC3);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            img.at<cv::Vec3b>(y, x) = cv::Vec3b(
                static_cast<uchar>(x % 256),
                static_cast<uchar>(y % 256),
                static_cast<uchar>((x + y) % 256));
    return img;
}

// ============================================================
// 算法单元测试
// ============================================================

TEST(Algorithm, GaussianBlur_OutputSizeMatchesInput) {
    cv::Mat in = make_test_image(640, 480);
    GaussianBlurAlgorithm algo(15);
    cv::Mat out = algo.process(in);
    ASSERT_EQ(out.rows, in.rows);
    ASSERT_EQ(out.cols, in.cols);
    ASSERT_EQ(out.type(), in.type());
}

TEST(Algorithm, GaussianBlur_OutputIsDifferentFromInput) {
    cv::Mat in = make_test_image();
    GaussianBlurAlgorithm algo(15);
    cv::Mat out = algo.process(in);
    // 模糊后与原图不完全相同
    cv::Mat diff;
    cv::absdiff(in, out, diff);
    ASSERT_GT(cv::sum(diff)[0], 0.0);
}

TEST(Algorithm, GaussianBlur_KernelSizeOne_NoChange) {
    cv::Mat in = make_test_image();
    GaussianBlurAlgorithm algo(1);
    cv::Mat out = algo.process(in);
    // kernel=1 相当于无操作
    cv::Mat diff;
    cv::absdiff(in, out, diff);
    ASSERT_EQ(static_cast<int>(cv::sum(diff)[0]), 0);
}

TEST(Algorithm, EdgeDetect_OutputIsThreeChannel) {
    cv::Mat in = make_test_image();
    EdgeDetectAlgorithm algo(50, 150);
    cv::Mat out = algo.process(in);
    ASSERT_EQ(out.rows, in.rows);
    ASSERT_EQ(out.cols, in.cols);
    ASSERT_EQ(out.channels(), 3);
}

TEST(Algorithm, EdgeDetect_Name) {
    EdgeDetectAlgorithm algo;
    ASSERT_EQ(algo.name(), std::string("EdgeDetect"));
}

TEST(Algorithm, Grayscale_OutputIsThreeChannel) {
    // 灰度算法输出仍是 3 通道 BGR（方便统一回调处理）
    cv::Mat in = make_test_image();
    GrayscaleAlgorithm algo;
    cv::Mat out = algo.process(in);
    ASSERT_EQ(out.channels(), 3);
    ASSERT_EQ(out.rows, in.rows);
    ASSERT_EQ(out.cols, in.cols);
}

TEST(Algorithm, Grayscale_AllChannelsEqual) {
    // 灰度图三个通道应相等
    cv::Mat in = make_test_image();
    GrayscaleAlgorithm algo;
    cv::Mat out = algo.process(in);
    std::vector<cv::Mat> ch;
    cv::split(out, ch);
    cv::Mat diff_gb, diff_br;
    cv::absdiff(ch[0], ch[1], diff_gb);
    cv::absdiff(ch[1], ch[2], diff_br);
    ASSERT_EQ(static_cast<int>(cv::sum(diff_gb)[0]), 0);
    ASSERT_EQ(static_cast<int>(cv::sum(diff_br)[0]), 0);
}

TEST(Algorithm, Sharpen_OutputSizeMatchesInput) {
    cv::Mat in = make_test_image();
    SharpenAlgorithm algo;
    cv::Mat out = algo.process(in);
    ASSERT_EQ(out.rows, in.rows);
    ASSERT_EQ(out.cols, in.cols);
    ASSERT_EQ(out.type(), in.type());
}

TEST(Algorithm, Emboss_OutputIsThreeChannel) {
    cv::Mat in = make_test_image();
    EmbossAlgorithm algo;
    cv::Mat out = algo.process(in);
    ASSERT_EQ(out.channels(), 3);
    ASSERT_EQ(out.rows, in.rows);
    ASSERT_EQ(out.cols, in.cols);
}

TEST(Algorithm, Cartoon_OutputNotEmpty) {
    cv::Mat in = make_test_image();
    CartoonAlgorithm algo;
    cv::Mat out = algo.process(in);
    ASSERT_FALSE(out.empty());
    ASSERT_EQ(out.rows, in.rows);
    ASSERT_EQ(out.cols, in.cols);
}

TEST(Algorithm, AllNames_NonEmpty) {
    ASSERT_NE(GaussianBlurAlgorithm().name(), std::string(""));
    ASSERT_NE(EdgeDetectAlgorithm().name(),   std::string(""));
    ASSERT_NE(GrayscaleAlgorithm().name(),    std::string(""));
    ASSERT_NE(SharpenAlgorithm().name(),      std::string(""));
    ASSERT_NE(EmbossAlgorithm().name(),       std::string(""));
    ASSERT_NE(CartoonAlgorithm().name(),      std::string(""));
}

// ============================================================
// ImageProcessor 生命周期测试
// ============================================================

TEST(ImageProcessor, ConstructDestruct_NoCrash) {
    ASSERT_NO_THROW({
        ImageProcessor proc;
    });
}

TEST(ImageProcessor, StartStop_Basic) {
    ImageProcessor proc;
    proc.start();
    ASSERT_TRUE(proc.is_running());
    proc.stop();
    ASSERT_FALSE(proc.is_running());
}

TEST(ImageProcessor, StartStop_Multiple_Times) {
    ImageProcessor proc;
    for (int i = 0; i < 5; ++i) {
        proc.start();
        ASSERT_TRUE(proc.is_running());
        proc.stop();
        ASSERT_FALSE(proc.is_running());
    }
}

TEST(ImageProcessor, Start_Idempotent) {
    // 连续 start 两次不应崩溃
    ImageProcessor proc;
    proc.start();
    proc.start();   // 第二次应被忽略
    ASSERT_TRUE(proc.is_running());
    proc.stop();
}

TEST(ImageProcessor, Stop_WithoutStart_NoCrash) {
    ImageProcessor proc;
    ASSERT_NO_THROW(proc.stop());
}

// ============================================================
// 模式切换测试
// ============================================================

TEST(ImageProcessor, SetGetMode_DefaultIsNone) {
    ImageProcessor proc;
    ASSERT_TRUE(proc.get_mode() == ProcessMode::None);
}

TEST(ImageProcessor, SetGetMode_AllModes) {
    ImageProcessor proc;
    const ProcessMode modes[] = {
        ProcessMode::None,
        ProcessMode::GaussianBlur,
        ProcessMode::EdgeDetect,
        ProcessMode::Grayscale,
        ProcessMode::Sharpen,
        ProcessMode::Emboss,
        ProcessMode::Cartoon,
    };
    for (auto m : modes) {
        proc.set_mode(m);
        ASSERT_TRUE(proc.get_mode() == m);
    }
}

TEST(ImageProcessor, ModeSwitch_WhileRunning_NoCrash) {
    ImageProcessor proc;
    proc.start();

    const ProcessMode modes[] = {
        ProcessMode::GaussianBlur,
        ProcessMode::EdgeDetect,
        ProcessMode::Grayscale,
        ProcessMode::None,
    };
    for (auto m : modes) {
        proc.set_mode(m);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    proc.stop();
}

// ============================================================
// 回调机制测试
// ============================================================

TEST(ImageProcessor, SubmitFrame_NoCallback_NoCrash) {
    ImageProcessor proc;
    proc.start();
    cv::Mat frame = make_test_image();
    ASSERT_NO_THROW(proc.submit_frame(frame));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    proc.stop();
}

TEST(ImageProcessor, SubmitFrame_NoneMode_CallbackReceivesFrame) {
    ImageProcessor proc;
    proc.set_mode(ProcessMode::None);

    std::atomic<int> cb_count{0};
    cv::Size received_size;

    proc.set_output_callback([&](const cv::Mat& out) {
        cb_count.fetch_add(1);
        received_size = out.size();
    });

    proc.start();
    cv::Mat frame = make_test_image(320, 240);
    proc.submit_frame(frame);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    proc.stop();

    ASSERT_GT(cb_count.load(), 0);
    ASSERT_EQ(received_size.width, 320);
    ASSERT_EQ(received_size.height, 240);
}

TEST(ImageProcessor, SubmitFrame_GaussianBlur_CallbackReceivesProcessedFrame) {
    ImageProcessor proc;
    proc.set_mode(ProcessMode::GaussianBlur);

    std::atomic<bool> cb_called{false};
    proc.set_output_callback([&](const cv::Mat& out) {
        cb_called.store(true);
        if (out.empty()) throw std::runtime_error("回调收到空图像");
    });

    proc.start();
    proc.submit_frame(make_test_image());
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    proc.stop();

    ASSERT_TRUE(cb_called.load());
}

TEST(ImageProcessor, SubmitFrame_AllModes_NoException) {
    const ProcessMode modes[] = {
        ProcessMode::GaussianBlur,
        ProcessMode::EdgeDetect,
        ProcessMode::Grayscale,
        ProcessMode::Sharpen,
        ProcessMode::Emboss,
        ProcessMode::Cartoon,
    };

    for (auto mode : modes) {
        ImageProcessor proc;
        proc.set_mode(mode);

        std::atomic<bool> error_occurred{false};
        proc.set_output_callback([&](const cv::Mat& out) {
            if (out.empty()) error_occurred.store(true);
        });

        proc.start();
        proc.submit_frame(make_test_image());
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        proc.stop();

        ASSERT_FALSE(error_occurred.load());
    }
}

TEST(ImageProcessor, OnlyLatestFrame_IsProcessed) {
    // 快速连续提交多帧，只有最后一帧会被实际处理（旧帧被覆盖）
    ImageProcessor proc;
    proc.set_mode(ProcessMode::GaussianBlur);

    std::atomic<int> cb_count{0};
    proc.set_output_callback([&](const cv::Mat&) {
        cb_count.fetch_add(1);
    });

    proc.start();
    for (int i = 0; i < 100; ++i) {
        proc.submit_frame(make_test_image());
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    proc.stop();

    // 快速提交 100 帧，处理器应远少于 100 次（旧帧被覆盖）
    ASSERT_LT(cb_count.load(), 100);
    ASSERT_GT(cb_count.load(), 0);
}

// ============================================================
// 线程安全测试
// ============================================================

TEST(ImageProcessor, ConcurrentModeSwitch_NoRaceCondition) {
    ImageProcessor proc;
    std::atomic<int> cb_count{0};
    proc.set_output_callback([&](const cv::Mat&) {
        cb_count.fetch_add(1);
    });
    proc.start();

    // 两个线程同时切换模式并提交帧
    std::thread producer([&]() {
        for (int i = 0; i < 200; ++i) {
            proc.submit_frame(make_test_image(64, 64));
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });

    std::thread switcher([&]() {
        const ProcessMode modes[] = {
            ProcessMode::None, ProcessMode::GaussianBlur,
            ProcessMode::Grayscale, ProcessMode::EdgeDetect
        };
        for (int i = 0; i < 40; ++i) {
            proc.set_mode(modes[i % 4]);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    producer.join();
    switcher.join();
    proc.stop();

    ASSERT_GT(cb_count.load(), 0);
}

TEST(ImageProcessor, SetCallback_WhileRunning_NoCrash) {
    ImageProcessor proc;
    proc.start();

    for (int i = 0; i < 5; ++i) {
        proc.set_output_callback([](const cv::Mat&) {});
        proc.submit_frame(make_test_image(64, 64));
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    // 清除回调
    proc.set_output_callback(nullptr);
    proc.submit_frame(make_test_image(64, 64));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    proc.stop();
}

TEST(ImageProcessor, EmptyFrame_DoesNotCrash) {
    ImageProcessor proc;
    proc.set_mode(ProcessMode::GaussianBlur);
    proc.start();

    // 提交一个空 Mat，不应崩溃（OpenCV 会抛异常，处理器应捕获）
    ASSERT_NO_THROW(proc.submit_frame(cv::Mat()));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    proc.stop();
}
