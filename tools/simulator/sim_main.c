/**
 * @file    sim_main.c
 * @brief   BUILD_SIM 使用的 PC 仿真入口
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    启动 bootstrap，拉起仿真控制台，
 *          后续后台工作由 scheduler 管理的线程完成。
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
