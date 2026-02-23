cmake_minimum_required(VERSION 3.10)

find_package(OpenCV REQUIRED)
find_package(Threads REQUIRED)

include_directories(
    ${PROJECT_SOURCE_DIR}/include
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ---- 共享源文件（去除 main.cpp）----
set(PROJ_SRCS
    ${PROJECT_SOURCE_DIR}/src/image_processor.cpp
    ${PROJECT_SOURCE_DIR}/src/photo_manager.cpp
)

# ============================================================
# 可执行文件 1: unit_tests  — 单元测试
# ============================================================
add_executable(unit_tests
    test_main.cpp
    test_image_processor.cpp
    test_photo_manager.cpp
    ${PROJ_SRCS}
)

target_link_libraries(unit_tests
    ${OpenCV_LIBS}
    Threads::Threads
)

# ============================================================
# 可执行文件 2: stress_test — 压力 / 耐久测试
# ============================================================
add_executable(stress_test
    stress_test.cpp
    ${PROJ_SRCS}
)

target_link_libraries(stress_test
    ${OpenCV_LIBS}
    Threads::Threads
)

# ---- 可选：注册到 ctest ----
enable_testing()
add_test(NAME UnitTests   COMMAND unit_tests)
add_test(NAME StressTests COMMAND stress_test 10)   # CI 中跑 10 秒
