#ifndef PHOTO_MANAGER_H
#define PHOTO_MANAGER_H

#include <string>
#include <vector>
#include <opencv2/opencv.hpp>

// 照片管理类
// 设计思路: 将原来散落在v4l2类和showphoto类中的照片保存/浏览逻辑
//          统一封装，用std::filesystem替代Qt的QDir
class PhotoManager {
public:
    explicit PhotoManager(const std::string& photo_dir = "./photo");

    // 保存照片 (从原始MJPEG数据)
    std::string save_raw(const unsigned char* data, unsigned int size);

    // 保存照片 (从cv::Mat)
    std::string save_mat(const cv::Mat& image);

    // 获取照片列表
    std::vector<std::string> list_photos() const;

    // 按索引加载照片
    cv::Mat load_photo(int index) const;

    // 获取照片总数
    int count() const;

    // 获取照片目录路径
    const std::string& directory() const { return photo_dir_; }

private:
    // 生成唯一文件名
    std::string generate_filename() const;

    // 确保目录存在
    void ensure_directory();

    std::string photo_dir_;
};

#endif // PHOTO_MANAGER_H
