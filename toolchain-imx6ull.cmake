set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# 交叉编译器
set(CMAKE_C_COMPILER   arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)

# 查找目标库时优先搜索 armhf 多架构路径
set(CMAKE_FIND_ROOT_PATH
    /usr/lib/arm-linux-gnueabihf
    /usr/include/arm-linux-gnueabihf
)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# 指向 armhf 版 OpenCV cmake 配置
set(OpenCV_DIR /usr/lib/arm-linux-gnueabihf/cmake/opencv4)
