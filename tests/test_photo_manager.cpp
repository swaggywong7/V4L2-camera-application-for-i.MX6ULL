#include "test_framework.h"
#include "photo_manager.h"

#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <opencv2/opencv.hpp>

namespace fs = std::filesystem;

// ---- 工具 ----

// 每个测试使用独立临时目录，避免相互干扰
static std::string make_temp_dir(const std::string& suffix) {
    std::string path = "/tmp/pm_test_" + suffix;
    fs::remove_all(path);
    return path;
}

static cv::Mat make_color_image(int w = 320, int h = 240) {
    cv::Mat img(h, w, CV_8UC3, cv::Scalar(100, 150, 200));
    cv::rectangle(img, cv::Rect(50, 50, 100, 100), cv::Scalar(0, 255, 0), -1);
    return img;
}

// 构造假的 JPEG 头数据（用于 save_raw 测试）
static std::vector<unsigned char> make_fake_jpeg() {
    // 创建真实 JPEG：先用 OpenCV 编码到内存缓冲
    cv::Mat img = make_color_image(64, 64);
    std::vector<unsigned char> buf;
    cv::imencode(".jpg", img, buf);
    return buf;
}

// ============================================================
// 目录管理
// ============================================================

TEST(PhotoManager, Constructor_CreatesDirectory) {
    std::string dir = make_temp_dir("ctor");
    {
        PhotoManager pm(dir);
    }
    ASSERT_TRUE(fs::exists(dir));
    fs::remove_all(dir);
}

TEST(PhotoManager, Constructor_ExistingDirectory_NoCrash) {
    std::string dir = make_temp_dir("existing");
    fs::create_directories(dir);
    ASSERT_NO_THROW({
        PhotoManager pm(dir);
    });
    fs::remove_all(dir);
}

TEST(PhotoManager, Directory_Getter) {
    std::string dir = make_temp_dir("getter");
    PhotoManager pm(dir);
    ASSERT_EQ(pm.directory(), dir);
    fs::remove_all(dir);
}

// ============================================================
// save_mat
// ============================================================

TEST(PhotoManager, SaveMat_ReturnsNonEmptyPath) {
    std::string dir = make_temp_dir("savemat");
    PhotoManager pm(dir);

    std::string path = pm.save_mat(make_color_image());
    ASSERT_NE(path, std::string(""));
    fs::remove_all(dir);
}

TEST(PhotoManager, SaveMat_FileActuallyExists) {
    std::string dir = make_temp_dir("savemat_exists");
    PhotoManager pm(dir);

    std::string path = pm.save_mat(make_color_image());
    ASSERT_TRUE(fs::exists(path));
    fs::remove_all(dir);
}

TEST(PhotoManager, SaveMat_FileIsValidJPEG) {
    std::string dir = make_temp_dir("savemat_valid");
    PhotoManager pm(dir);

    cv::Mat orig = make_color_image(200, 200);
    std::string path = pm.save_mat(orig);

    cv::Mat loaded = cv::imread(path);
    ASSERT_FALSE(loaded.empty());
    ASSERT_EQ(loaded.rows, orig.rows);
    ASSERT_EQ(loaded.cols, orig.cols);
    fs::remove_all(dir);
}

TEST(PhotoManager, SaveMat_MultipleSaves_UniqueFilenames) {
    std::string dir = make_temp_dir("unique");
    PhotoManager pm(dir);

    std::vector<std::string> paths;
    for (int i = 0; i < 5; ++i) {
        // 稍作间隔，确保时间戳不同
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        paths.push_back(pm.save_mat(make_color_image()));
    }

    // 检查所有路径唯一
    for (size_t i = 0; i < paths.size(); ++i)
        for (size_t j = i + 1; j < paths.size(); ++j)
            ASSERT_NE(paths[i], paths[j]);

    fs::remove_all(dir);
}

TEST(PhotoManager, SaveMat_EmptyMat_ReturnsEmptyPath) {
    std::string dir = make_temp_dir("empty_mat");
    PhotoManager pm(dir);

    // 空 Mat 保存应失败并返回空字符串
    std::string path = pm.save_mat(cv::Mat());
    ASSERT_EQ(path, std::string(""));
    fs::remove_all(dir);
}

// ============================================================
// save_raw
// ============================================================

TEST(PhotoManager, SaveRaw_ValidJPEG_ReturnsPath) {
    std::string dir = make_temp_dir("saveraw");
    PhotoManager pm(dir);

    auto jpeg = make_fake_jpeg();
    std::string path = pm.save_raw(jpeg.data(), jpeg.size());
    ASSERT_NE(path, std::string(""));
    ASSERT_TRUE(fs::exists(path));
    fs::remove_all(dir);
}

TEST(PhotoManager, SaveRaw_ContentMatchesInput) {
    std::string dir = make_temp_dir("saveraw_content");
    PhotoManager pm(dir);

    auto jpeg = make_fake_jpeg();
    std::string path = pm.save_raw(jpeg.data(), jpeg.size());

    // 验证文件大小等于写入字节数
    ASSERT_EQ(static_cast<size_t>(fs::file_size(path)), jpeg.size());
    fs::remove_all(dir);
}

// ============================================================
// list_photos / count
// ============================================================

TEST(PhotoManager, Count_EmptyDirectory_IsZero) {
    std::string dir = make_temp_dir("count_empty");
    PhotoManager pm(dir);
    ASSERT_EQ(pm.count(), 0);
    fs::remove_all(dir);
}

TEST(PhotoManager, Count_AfterSave_IncreasesCorrectly) {
    std::string dir = make_temp_dir("count_save");
    PhotoManager pm(dir);

    for (int i = 1; i <= 3; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        pm.save_mat(make_color_image());
        ASSERT_EQ(pm.count(), i);
    }
    fs::remove_all(dir);
}

TEST(PhotoManager, ListPhotos_EmptyDirectory_ReturnsEmpty) {
    std::string dir = make_temp_dir("list_empty");
    PhotoManager pm(dir);
    ASSERT_EQ(pm.list_photos().size(), static_cast<size_t>(0));
    fs::remove_all(dir);
}

TEST(PhotoManager, ListPhotos_SortedByTime_NewestFirst) {
    std::string dir = make_temp_dir("list_sorted");
    PhotoManager pm(dir);

    std::vector<std::string> saved;
    for (int i = 0; i < 3; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        saved.push_back(pm.save_mat(make_color_image()));
    }

    auto listed = pm.list_photos();
    ASSERT_EQ(listed.size(), static_cast<size_t>(3));

    // 最新保存的应排在最前面
    ASSERT_EQ(listed[0], saved[2]);
    ASSERT_EQ(listed[2], saved[0]);
    fs::remove_all(dir);
}

TEST(PhotoManager, ListPhotos_IgnoresNonImageFiles) {
    std::string dir = make_temp_dir("list_filter");
    PhotoManager pm(dir);

    pm.save_mat(make_color_image());

    // 放入一个非图片文件
    std::ofstream(dir + "/readme.txt") << "test";
    std::ofstream(dir + "/data.bin") << "binary";

    ASSERT_EQ(pm.count(), 1);
    fs::remove_all(dir);
}

// ============================================================
// load_photo
// ============================================================

TEST(PhotoManager, LoadPhoto_ValidIndex_ReturnsNonEmpty) {
    std::string dir = make_temp_dir("load_valid");
    PhotoManager pm(dir);

    cv::Mat orig = make_color_image(320, 240);
    pm.save_mat(orig);

    cv::Mat loaded = pm.load_photo(0);
    ASSERT_FALSE(loaded.empty());
    ASSERT_EQ(loaded.rows, orig.rows);
    ASSERT_EQ(loaded.cols, orig.cols);
    fs::remove_all(dir);
}

TEST(PhotoManager, LoadPhoto_NegativeIndex_ReturnsEmpty) {
    std::string dir = make_temp_dir("load_neg");
    PhotoManager pm(dir);
    pm.save_mat(make_color_image());

    cv::Mat result = pm.load_photo(-1);
    ASSERT_TRUE(result.empty());
    fs::remove_all(dir);
}

TEST(PhotoManager, LoadPhoto_OutOfBoundsIndex_ReturnsEmpty) {
    std::string dir = make_temp_dir("load_oob");
    PhotoManager pm(dir);
    pm.save_mat(make_color_image());

    cv::Mat result = pm.load_photo(99);
    ASSERT_TRUE(result.empty());
    fs::remove_all(dir);
}

TEST(PhotoManager, LoadPhoto_EmptyDirectory_ReturnsEmpty) {
    std::string dir = make_temp_dir("load_empty");
    PhotoManager pm(dir);

    cv::Mat result = pm.load_photo(0);
    ASSERT_TRUE(result.empty());
    fs::remove_all(dir);
}

// ============================================================
// 并发安全
// ============================================================

TEST(PhotoManager, ConcurrentSave_NoCorruption) {
    std::string dir = make_temp_dir("concurrent");
    PhotoManager pm(dir);

    constexpr int N_THREADS = 4;
    constexpr int N_PER_THREAD = 5;
    std::vector<std::thread> threads;

    for (int t = 0; t < N_THREADS; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < N_PER_THREAD; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                pm.save_mat(make_color_image(64, 64));
            }
        });
    }
    for (auto& th : threads) th.join();

    // 所有文件都能被加载
    int count = pm.count();
    ASSERT_EQ(count, N_THREADS * N_PER_THREAD);

    for (int i = 0; i < count; ++i) {
        cv::Mat img = pm.load_photo(i);
        ASSERT_FALSE(img.empty());
    }
    fs::remove_all(dir);
}
