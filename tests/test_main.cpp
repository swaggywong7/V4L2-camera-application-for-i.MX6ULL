#include "test_framework.h"

// 所有 TEST() 宏在 include 对应 .cpp 前就已通过静态注册器登记
// 这里只需要提供 main 入口

int main() {
    return run_all_tests();
}
