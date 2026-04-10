/**
 * @file    sim_main.c
 * @brief   M8 PC 仿真入口（BUILD_SIM=ON 时替代 snack 框架的 app_main）
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    程序启动流程：
 *            1. bootstrap_run() — 完整初始化序列（跳过真机硬件步骤）
 *            2. sim_console_start() — 启动交互控制台线程（stdin 命令）
 *            3. 主线程阻塞，真实工作由 scheduler 启动的各线程完成
 *
 *          可用控制台命令（由 sim_console.c 实现）：
 *            sensor fwd [0|1]   — 设置前限位
 *            sensor rev [0|1]   — 设置后限位
 *            sensor estop [0|1] — 设置急停
 *            sensor lift_top [0|1]    — 顶刷上限位
 *            sensor lift_bottom [0|1] — 顶刷下限位
 *            encoder [+N|-N]    — 注入码盘脉冲
 *            cmd order [0|1]    — 注入启动洗车命令
 *            cmd stop           — 注入停止洗车命令
 *            cmd reset          — 注入故障复位命令
 *            state              — 打印设备状态快照
 *            quit               — 退出仿真
 */

#include "core/bootstrap/bootstrap.h"
#include "common/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

/* 声明控制台线程入口（sim_console.c 提供）*/
extern void sim_console_start(void);

/* -------------------------------------------------------------------------
 * 信号处理：Ctrl+C 优雅退出
 * ------------------------------------------------------------------------- */
static void handle_sigint(int sig)
{
    (void)sig;
    printf("\n[sim] received SIGINT, exiting\n");
    exit(0);
}

/* -------------------------------------------------------------------------
 * 主入口
 * ------------------------------------------------------------------------- */
int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    signal(SIGINT, handle_sigint);

    printf("========================================\n");
    printf("  M8 PC Simulator (BUILD_SIM)\n");
    printf("========================================\n");

    /* 完整系统初始化（跳过真机硬件步骤）*/
    sw_err_t ret = bootstrap_run();
    if (ret != SW_OK)
    {
        fprintf(stderr, "[sim] bootstrap_run failed ret=%d\n", (int)ret);
        return 1;
    }

    LOG_INFO("sim: bootstrap complete, all threads started");

    /* 启动交互控制台（非阻塞，在独立线程中运行）*/
    sim_console_start();

    /* 主线程等待——真实工作由 scheduler 启动的各业务线程完成 */
    while (true)
    {
        sleep(1);
    }

    return 0;
}
