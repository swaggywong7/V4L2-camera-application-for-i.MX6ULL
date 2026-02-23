// ============================================================
// 压力测试 / 耐久测试
// 可在开发板上长时间运行，验证：
//   1. 高频帧提交吞吐量
//   2. 模式快速切换稳定性
//   3. 长时间运行内存无泄漏
//   4. 多生产者并发提交
//   5. PhotoManager 批量写入磁盘
//
// 用法:  ./stress_test [duration_seconds=30]
// ============================================================

#include "image_processor.h"
#include "photo_manager.h"
#include "test_framework.h"

#include <unistd.h>
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <filesystem>
#include <opencv2/opencv.hpp>

namespace fs = std::filesystem;

// ---- 测试持续时间（秒），可通过命令行覆盖 ----
static int g_duration_sec = 30;

// ---- 工具 ----

static cv::Mat make_frame(int w = 320, int h = 240) {
    cv::Mat img(h, w, CV_8UC3);
    // 使用随机噪声，避免算法优化掉无变化帧
    cv::randu(img, cv::Scalar(0, 0, 0), cv::Scalar(255, 255, 255));
    return img;
}

static long rss_kb() {
    long rss = 0;
    FILE* f = fopen("/proc/self/status", "r");
    if (!f) return -1;
    char line[128];
    while (fgets(line, sizeof(line), f))
        if (sscanf(line, "VmRSS: %ld kB", &rss) == 1) break;
    fclose(f);
    return rss;
}

static void print_separator(const char* title) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════╗\n");
    printf("  %-52s\n", title);
    printf("╚══════════════════════════════════════════════════════╝\n");
}

static void print_progress(const char* label, long elapsed_ms, long total_ms,
                            long count, long mem_kb) {
    int pct = static_cast<int>(100.0 * elapsed_ms / total_ms);
    printf("  [%3d%%] %s  帧数=%ld  内存=%ld KB\n", pct, label, count, mem_kb);
    fflush(stdout);
}

// ============================================================
// 测试 1：高频帧提交吞吐量
// 目标：衡量 ImageProcessor 每秒能处理多少帧
// ============================================================

TEST(Stress, HighFrequency_GaussianBlur_Throughput) {
    print_separator("测试1: 高频帧提交吞吐量 (GaussianBlur)");

    ImageProcessor proc;
    proc.set_mode(ProcessMode::GaussianBlur);

    std::atomic<long> processed{0};
    proc.set_output_callback([&](const cv::Mat&) {
        processed.fetch_add(1, std::memory_order_relaxed);
    });
    proc.start();

    auto t_end = std::chrono::steady_clock::now()
                 + std::chrono::seconds(g_duration_sec);
    long submitted = 0;

    while (std::chrono::steady_clock::now() < t_end) {
        proc.submit_frame(make_frame());
        ++submitted;
        // 不加任何 sleep，尽力压测
    }

    proc.stop();

    long p = processed.load();
    double fps = static_cast<double>(p) / g_duration_sec;
    printf("  提交帧数 : %ld\n", submitted);
    printf("  处理帧数 : %ld\n", p);
    printf("  有效 FPS : %.1f\n", fps);

    // 不管多慢，至少要处理过一些帧
    ASSERT_GT(p, 0);
}

// ============================================================
// 测试 2：所有模式轮流切换稳定性
// ============================================================

TEST(Stress, RapidModeSwitching_Stability) {
    print_separator("测试2: 模式快速轮切稳定性");

    const ProcessMode modes[] = {
        ProcessMode::None,
        ProcessMode::GaussianBlur,
        ProcessMode::EdgeDetect,
        ProcessMode::Grayscale,
        ProcessMode::Sharpen,
        ProcessMode::Emboss,
        ProcessMode::Cartoon,
    };
    const int N_MODES = sizeof(modes) / sizeof(modes[0]);

    ImageProcessor proc;
    std::atomic<long> processed{0};
    std::atomic<bool> error_flag{false};

    proc.set_output_callback([&](const cv::Mat& out) {
        if (out.empty()) error_flag.store(true);
        processed.fetch_add(1, std::memory_order_relaxed);
    });
    proc.start();

    long mode_switches = 0;
    long submitted = 0;
    auto t_end = std::chrono::steady_clock::now()
                 + std::chrono::seconds(g_duration_sec);

    while (std::chrono::steady_clock::now() < t_end) {
        proc.submit_frame(make_frame(64, 64));   // 小尺寸，频繁提交
        ++submitted;

        if (submitted % 20 == 0) {
            proc.set_mode(modes[mode_switches % N_MODES]);
            ++mode_switches;
        }
    }

    proc.stop();

    printf("  提交帧数   : %ld\n", submitted);
    printf("  处理帧数   : %ld\n", processed.load());
    printf("  模式切换次数: %ld\n", mode_switches);

    ASSERT_FALSE(error_flag.load());
    ASSERT_GT(processed.load(), 0);
}

// ============================================================
// 测试 3：长时间运行内存稳定性
// ============================================================

TEST(Stress, LongRunning_MemoryStability) {
    print_separator("测试3: 长时间运行内存稳定性");

    ImageProcessor proc;
    proc.set_mode(ProcessMode::EdgeDetect);
    std::atomic<long> frame_count{0};
    proc.set_output_callback([&](const cv::Mat&) {
        frame_count.fetch_add(1, std::memory_order_relaxed);
    });
    proc.start();

    long mem_start = rss_kb();
    std::vector<long> mem_samples;

    auto t_start = std::chrono::steady_clock::now();
    auto t_end   = t_start + std::chrono::seconds(g_duration_sec);
    auto t_next_sample = t_start + std::chrono::seconds(5);
    long submitted = 0;

    while (std::chrono::steady_clock::now() < t_end) {
        proc.submit_frame(make_frame());
        ++submitted;
        std::this_thread::sleep_for(std::chrono::microseconds(500));  // ~2000 fps 上限

        auto now = std::chrono::steady_clock::now();
        if (now >= t_next_sample) {
            long mem = rss_kb();
            mem_samples.push_back(mem);
            long elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                now - t_start).count();
            print_progress("EdgeDetect", elapsed * 1000,
                           g_duration_sec * 1000, frame_count.load(), mem);
            t_next_sample = now + std::chrono::seconds(5);
        }
    }

    proc.stop();

    long mem_end = rss_kb();
    long mem_growth = mem_end - mem_start;

    printf("\n  起始内存  : %ld KB\n", mem_start);
    printf("  结束内存  : %ld KB\n", mem_end);
    printf("  内存增长  : %+ld KB\n", mem_growth);
    printf("  总提交帧数 : %ld\n", submitted);
    printf("  总处理帧数 : %ld\n", frame_count.load());

    // 内存增长超过 50 MB 视为泄漏
    ASSERT_LT(mem_growth, 50 * 1024L);
}

// ============================================================
// 测试 4：多生产者并发提交
// ============================================================

TEST(Stress, MultiProducer_ConcurrentSubmit) {
    print_separator("测试4: 多生产者并发提交");

    constexpr int N_PRODUCERS = 4;

    ImageProcessor proc;
    proc.set_mode(ProcessMode::Grayscale);
    std::atomic<long> processed{0};
    std::atomic<bool> error_flag{false};

    proc.set_output_callback([&](const cv::Mat& out) {
        if (out.empty()) error_flag.store(true);
        processed.fetch_add(1, std::memory_order_relaxed);
    });
    proc.start();

    std::atomic<long> total_submitted{0};
    std::vector<std::thread> producers;
    std::atomic<bool> stop_flag{false};

    for (int t = 0; t < N_PRODUCERS; ++t) {
        producers.emplace_back([&]() {
            while (!stop_flag.load(std::memory_order_relaxed)) {
                proc.submit_frame(make_frame(160, 120));
                total_submitted.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::seconds(g_duration_sec));
    stop_flag.store(true);
    for (auto& th : producers) th.join();

    proc.stop();

    printf("  生产者线程数 : %d\n", N_PRODUCERS);
    printf("  总提交帧数   : %ld\n", total_submitted.load());
    printf("  总处理帧数   : %ld\n", processed.load());

    ASSERT_FALSE(error_flag.load());
    ASSERT_GT(processed.load(), 0);
}

// ============================================================
// 测试 5：PhotoManager 批量写入压力
// ============================================================

TEST(Stress, PhotoManager_BulkWrite) {
    print_separator("测试5: PhotoManager 批量写入压力");

    std::string dir = "/tmp/stress_photo_" + std::to_string(getpid());
    fs::remove_all(dir);
    PhotoManager pm(dir);

    constexpr int BATCH = 50;   // 写入照片数量
    long mem_before = rss_kb();

    auto t0 = std::chrono::steady_clock::now();
    std::vector<std::string> paths;
    for (int i = 0; i < BATCH; ++i) {
        cv::Mat frame = make_frame(640, 480);
        std::string p = pm.save_mat(frame);
        ASSERT_NE(p, std::string(""));
        paths.push_back(p);
    }
    auto t1 = std::chrono::steady_clock::now();
    double sec = std::chrono::duration<double>(t1 - t0).count();

    printf("  写入 %d 张 640x480 JPEG\n", BATCH);
    printf("  耗时      : %.2f 秒\n", sec);
    printf("  写入速度  : %.1f 张/秒\n", BATCH / sec);
    printf("  目录文件数 : %d\n", pm.count());

    ASSERT_EQ(pm.count(), BATCH);

    // 随机加载验证
    for (int i = 0; i < 5; ++i) {
        cv::Mat img = pm.load_photo(i);
        ASSERT_FALSE(img.empty());
    }

    long mem_after = rss_kb();
    printf("  内存增量  : %+ld KB\n", mem_after - mem_before);

    fs::remove_all(dir);
}

// ============================================================
// 测试 6：ImageProcessor + PhotoManager 联合压力
// 模拟实际相机应用：采集→处理→按需保存
// ============================================================

TEST(Stress, Combined_CaptureProcessSave) {
    print_separator("测试6: 采集→处理→保存联合压力");

    std::string dir = "/tmp/stress_combined_" + std::to_string(getpid());
    fs::remove_all(dir);
    PhotoManager pm(dir);

    ImageProcessor proc;
    proc.set_mode(ProcessMode::Sharpen);

    std::atomic<long> saved_count{0};
    std::atomic<long> drop_count{0};
    std::mutex save_mutex;

    // 处理完成后每隔 10 帧保存一次
    std::atomic<long> cb_total{0};
    proc.set_output_callback([&](const cv::Mat& out) {
        long n = cb_total.fetch_add(1, std::memory_order_relaxed);
        if (n % 10 == 0) {
            std::lock_guard<std::mutex> lk(save_mutex);
            pm.save_mat(out);
            saved_count.fetch_add(1, std::memory_order_relaxed);
        }
    });
    proc.start();

    auto t_end = std::chrono::steady_clock::now()
                 + std::chrono::seconds(g_duration_sec);
    long submitted = 0;

    while (std::chrono::steady_clock::now() < t_end) {
        proc.submit_frame(make_frame(320, 240));
        ++submitted;
        std::this_thread::sleep_for(std::chrono::milliseconds(33));  // 模拟 30fps
    }

    proc.stop();

    long saved = saved_count.load();
    printf("  模拟帧率  : 30 fps\n");
    printf("  总提交帧数 : %ld\n", submitted);
    printf("  已保存照片 : %ld\n", saved);

    ASSERT_GT(saved, 0);
    ASSERT_EQ(pm.count(), static_cast<int>(saved));

    fs::remove_all(dir);
}

// ============================================================
// main
// ============================================================

int main(int argc, char* argv[]) {
    if (argc >= 2) {
        g_duration_sec = std::atoi(argv[1]);
        if (g_duration_sec <= 0) g_duration_sec = 30;
    }

    printf("============================================================\n");
    printf("  v4l2_camera 压力测试\n");
    printf("  每项测试持续时间: %d 秒\n", g_duration_sec);
    printf("============================================================\n");

    return run_all_tests();
}
