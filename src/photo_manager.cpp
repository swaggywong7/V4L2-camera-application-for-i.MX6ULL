#include "photo_manager.h"

#include <atomic>
#include <cstdio>
#include <ctime>
#include <random>
#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

PhotoManager::PhotoManager(const std::string& photo_dir)
    : photo_dir_(photo_dir)
{
    ensure_directory();
}

void PhotoManager::ensure_directory()
{
    std::error_code ec;
    if (!fs::exists(photo_dir_, ec)) {
        if (fs::create_directories(photo_dir_, ec)) {
            printf("[PhotoManager] 创建目录: %s\n", photo_dir_.c_str());
        } else {
            fprintf(stderr, "[PhotoManager] 创建目录失败: %s (%s)\n",
                    photo_dir_.c_str(), ec.message().c_str());
        }
    }
}

std::string PhotoManager::generate_filename() const
{
    // 使用时间戳 + 全局原子序列号生成唯一文件名
    // BUG-002 修复：毫秒精度在并发场景下会碰撞，加入序列号保证唯一性
    static std::atomic<int> seq{0};
    int n = seq.fetch_add(1, std::memory_order_relaxed);

    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count() % 1000;

    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", std::localtime(&time_t));

    return std::string(buf) + "_" + std::to_string(ms) + "_" + std::to_string(n);
}

std::string PhotoManager::save_raw(const unsigned char* data, unsigned int size)
{
    std::string filename = photo_dir_ + "/photo_" + generate_filename() + ".jpg";

    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        fprintf(stderr, "[PhotoManager] 无法创建文件: %s\n", filename.c_str());
        return "";
    }

    file.write(reinterpret_cast<const char*>(data), size);
    file.close();

    printf("[PhotoManager] 已保存: %s\n", filename.c_str());
    return filename;
}

std::string PhotoManager::save_mat(const cv::Mat& image)
{
    // BUG-001 修复：cv::imwrite 对空 Mat 会抛出断言异常而非返回 false，需提前校验
    if (image.empty()) {
        fprintf(stderr, "[PhotoManager] 图像为空，跳过保存\n");
        return "";
    }

    std::string filename = photo_dir_ + "/photo_" + generate_filename() + ".jpg";

    if (!cv::imwrite(filename, image)) {
        fprintf(stderr, "[PhotoManager] 保存图像失败: %s\n", filename.c_str());
        return "";
    }

    printf("[PhotoManager] 已保存: %s\n", filename.c_str());
    return filename;
}

std::vector<std::string> PhotoManager::list_photos() const
{
    std::vector<std::string> photos;

    std::error_code ec;
    if (!fs::exists(photo_dir_, ec)) return photos;

    for (const auto& entry : fs::directory_iterator(photo_dir_, ec)) {
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            // 转小写比较
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".jpg" || ext == ".jpeg" || ext == ".png") {
                photos.push_back(entry.path().string());
            }
        }
    }

    // 按修改时间排序 (最新在前)
    std::sort(photos.begin(), photos.end(),
              [](const std::string& a, const std::string& b) {
                  return fs::last_write_time(a) > fs::last_write_time(b);
              });

    return photos;
}

cv::Mat PhotoManager::load_photo(int index) const
{
    auto photos = list_photos();
    if (index < 0 || index >= static_cast<int>(photos.size())) {
        fprintf(stderr, "[PhotoManager] 索引越界: %d (共%zu张)\n",
                index, photos.size());
        return cv::Mat();
    }

    cv::Mat image = cv::imread(photos[index]);
    if (image.empty()) {
        fprintf(stderr, "[PhotoManager] 无法加载: %s\n", photos[index].c_str());
    }

    return image;
}

int PhotoManager::count() const
{
    return static_cast<int>(list_photos().size());
}
