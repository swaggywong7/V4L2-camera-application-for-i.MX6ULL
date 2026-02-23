#pragma once

// ============================================================
// 轻量级测试框架 — 无外部依赖，可交叉编译到 ARM
// 用法:
//   TEST(SuiteName, CaseName) { ... ASSERT_TRUE(x); ... }
//   int main() { return run_all_tests(); }
// ============================================================

#include <string>
#include <vector>
#include <functional>
#include <stdexcept>
#include <sstream>
#include <iostream>
#include <chrono>
#include <cstdio>

// ---- 测试注册表 ----
struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& test_registry() {
    static std::vector<TestCase> registry;
    return registry;
}

// ---- 断言宏 (失败时抛出异常，终止当前 test) ----

#define ASSERT_TRUE(expr) \
    do { if (!(expr)) { \
        std::ostringstream _s; \
        _s << "ASSERT_TRUE(" #expr ") 失败  line " << __LINE__; \
        throw std::runtime_error(_s.str()); \
    } } while(0)

#define ASSERT_FALSE(expr) \
    do { if ((expr)) { \
        std::ostringstream _s; \
        _s << "ASSERT_FALSE(" #expr ") 失败  line " << __LINE__; \
        throw std::runtime_error(_s.str()); \
    } } while(0)

#define ASSERT_EQ(a, b) \
    do { if (!((a) == (b))) { \
        std::ostringstream _s; \
        _s << "ASSERT_EQ 失败: " #a "=" << (a) << "  " #b "=" << (b) \
           << "  line " << __LINE__; \
        throw std::runtime_error(_s.str()); \
    } } while(0)

#define ASSERT_NE(a, b) \
    do { if ((a) == (b)) { \
        std::ostringstream _s; \
        _s << "ASSERT_NE 失败: " #a " == " #b " = " << (a) \
           << "  line " << __LINE__; \
        throw std::runtime_error(_s.str()); \
    } } while(0)

#define ASSERT_GT(a, b) \
    do { if (!((a) > (b))) { \
        std::ostringstream _s; \
        _s << "ASSERT_GT 失败: " #a "=" << (a) << " <= " #b "=" << (b) \
           << "  line " << __LINE__; \
        throw std::runtime_error(_s.str()); \
    } } while(0)

#define ASSERT_GE(a, b) \
    do { if (!((a) >= (b))) { \
        std::ostringstream _s; \
        _s << "ASSERT_GE 失败: " #a "=" << (a) << " < " #b "=" << (b) \
           << "  line " << __LINE__; \
        throw std::runtime_error(_s.str()); \
    } } while(0)

#define ASSERT_LT(a, b) \
    do { if (!((a) < (b))) { \
        std::ostringstream _s; \
        _s << "ASSERT_LT 失败: " #a "=" << (a) << " >= " #b "=" << (b) \
           << "  line " << __LINE__; \
        throw std::runtime_error(_s.str()); \
    } } while(0)

#define ASSERT_NO_THROW(expr) \
    do { try { expr; } catch (const std::exception& _e) { \
        std::ostringstream _s; \
        _s << "ASSERT_NO_THROW(" #expr ") 抛出异常: " << _e.what() \
           << "  line " << __LINE__; \
        throw std::runtime_error(_s.str()); \
    } } while(0)

// ---- TEST 宏：静态注册 ----
#define TEST(suite, name) \
    static void _tf_##suite##_##name(); \
    static bool _reg_##suite##_##name = (test_registry().push_back( \
        {#suite "::" #name, _tf_##suite##_##name}), true); \
    static void _tf_##suite##_##name()

// ---- 读取进程 RSS 内存 (KB)，仅在 Linux 有效 ----
inline long get_rss_kb() {
    long rss = 0;
    FILE* fp = fopen("/proc/self/status", "r");
    if (!fp) return -1;
    char line[128];
    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "VmRSS: %ld kB", &rss) == 1) break;
    }
    fclose(fp);
    return rss;
}

// ---- 运行所有已注册的测试 ----
inline int run_all_tests() {
    auto& tests = test_registry();
    int passed = 0, failed = 0;

    printf("\n");
    printf("============================================================\n");
    printf("  单元测试  共 %zu 个\n", tests.size());
    printf("============================================================\n");

    for (auto& tc : tests) {
        auto t0 = std::chrono::high_resolution_clock::now();
        bool ok = true;
        std::string msg;
        try {
            tc.fn();
        } catch (const std::exception& e) {
            ok = false;
            msg = e.what();
        } catch (...) {
            ok = false;
            msg = "未知异常";
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        if (ok) {
            printf("  [PASS] %-55s %6.1f ms\n", tc.name.c_str(), ms);
            ++passed;
        } else {
            printf("  [FAIL] %-55s %6.1f ms\n", tc.name.c_str(), ms);
            printf("         >> %s\n", msg.c_str());
            ++failed;
        }
    }

    printf("------------------------------------------------------------\n");
    printf("  通过: %d   失败: %d   总计: %d\n", passed, failed, passed + failed);
    printf("============================================================\n\n");
    return failed;   // 0 = 全部通过
}
