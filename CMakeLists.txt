cmake_minimum_required(VERSION 3.10)
project(v4l2_camera VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 交叉编译时可通过 -DCMAKE_TOOLCHAIN_FILE 指定工具链
# 例如: cmake -DCMAKE_TOOLCHAIN_FILE=toolchain-imx6ull.cmake ..

# 精简版静态 OpenCV（直接指定路径，不走 find_package 避免配置文件问题）
set(OCV_PREFIX ${CMAKE_SOURCE_DIR}/3rdparty/opencv)
set(OCV_LIB   ${OCV_PREFIX}/lib)
set(OCV_3RD   ${OCV_LIB}/opencv4/3rdparty)

# 头文件目录
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${OCV_PREFIX}/include/opencv4)

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

# Cortex-A7 NEON 优化：启用 SIMD 自动向量化
target_compile_options(${PROJECT_NAME} PRIVATE
    -mcpu=cortex-a7
    -mfpu=neon-vfpv4
    -mfloat-abi=hard
    -ftree-vectorize
    -O3
)

# 静态链接精简 OpenCV，C++ 运行时也静态，只保留 libc/libpthread 动态
target_link_libraries(${PROJECT_NAME}
    -Wl,--start-group
    ${OCV_LIB}/libopencv_imgcodecs.a
    ${OCV_LIB}/libopencv_imgproc.a
    ${OCV_LIB}/libopencv_core.a
    ${OCV_3RD}/liblibjpeg-turbo.a
    ${OCV_3RD}/liblibpng.a
    ${OCV_3RD}/libzlib.a
    -Wl,--end-group
    pthread dl
)
target_link_options(${PROJECT_NAME} PRIVATE -static)

# 安装规则
install(TARGETS ${PROJECT_NAME} DESTINATION bin)

# 测试（默认开启，交叉编译时可用 -DBUILD_TESTS=OFF 关闭）
option(BUILD_TESTS "编译单元测试和压力测试" ON)
if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
