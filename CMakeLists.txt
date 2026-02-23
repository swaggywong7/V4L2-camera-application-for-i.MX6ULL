cmake_minimum_required(VERSION 3.10)
project(v4l2_camera VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 交叉编译时可通过 -DCMAKE_TOOLCHAIN_FILE 指定工具链
# 例如: cmake -DCMAKE_TOOLCHAIN_FILE=toolchain-imx6ull.cmake ..

# 查找 OpenCV
find_package(OpenCV REQUIRED)

# 头文件目录
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${OpenCV_INCLUDE_DIRS})

# 源文件
set(SOURCES
    src/main.cpp
    src/v4l2_device.cpp
    src/frame_buffer.cpp
    src/capture_thread.cpp
    src/image_processor.cpp
    src/photo_manager.cpp
    src/camera_app.cpp
)

# 可执行文件
add_executable(${PROJECT_NAME} ${SOURCES})

# 链接库
target_link_libraries(${PROJECT_NAME}
    ${OpenCV_LIBS}
    pthread
)

# 安装规则
install(TARGETS ${PROJECT_NAME} DESTINATION bin)

# 测试（默认开启，交叉编译时可用 -DBUILD_TESTS=OFF 关闭）
option(BUILD_TESTS "编译单元测试和压力测试" ON)
if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
