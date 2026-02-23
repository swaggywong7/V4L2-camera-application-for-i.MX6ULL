#include "camera_app.h"

#include <cstdio>
#include <csignal>
#include <memory>

// 全局指针用于信号处理 (仅此一处全局变量，用于处理Ctrl+C)
static CameraApp* g_app = nullptr;

static void signal_handler(int sig)
{
    if (sig == SIGINT || sig == SIGTERM) {
        printf("\n[main] 收到退出信号，正在清理...\n");
        if (g_app) {
            g_app->request_stop();
        }
    }
}

int main()
{
    // 注册信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("=== V4L2 Camera Application v1.0 ===\n");

    CameraApp app;
    g_app = &app;

    int ret = app.run();

    g_app = nullptr;
    return ret;
}
