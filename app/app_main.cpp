/**
 * @file    app_main.cpp
 * @brief   应用入口（snack 框架回调 app_main，非 main）
 * @author  胡望伟
 * @date    2026-04-08
 */

#include "bsp/bsp_init.h"
#include "bsp/bsp_hal.h"
#include "app/app_fsm.h"
#include "app/app_cloud.h"
#include "service/svc_alarm.h"
#include "service/svc_param.h"
#include "common/log.h"
#include "common/sw_version.h"
#include "middleware/snack_wrapper.h"
#include "cli/cli.h"
#include "io_exp/demo.h"
#include "log/mlog.h"
#include <fstream>
#include <string>
#include <stdarg.h>
#include <unistd.h>

static const char *APP_NAME    = SW_PRODUCT_NAME;
static const char *APP_VERSION = SW_VERSION_STR;

void app_info_set(char **name, char **version)
{
    *name    = (char *)APP_NAME;
    *version = (char *)APP_VERSION;
}

/* -------------------------------------------------------------------------
 * IO 子板日志回调（供 io_logApi_set 使用）
 * ------------------------------------------------------------------------- */
static mlog *s_io_log = new mlog("io_exp");

static int app_io_log(const char *fmt, ...)
{
    char buf[512];
    va_list va;
    va_start(va, fmt);
    vsnprintf(buf, sizeof(buf), fmt, va);
    s_io_log->info("%s", buf);
    va_end(va);
    return 0;
}

/* -------------------------------------------------------------------------
 * frp 远程端口读取（调试用）
 * ------------------------------------------------------------------------- */
static int get_remote_port(const std::string &filename)
{
    std::ifstream fin(filename);
    std::string   line;
    while (std::getline(fin, line)) {
        if (line.find("remote_port") == 0) {
            int port = -1;
            sscanf(line.c_str(), "remote_port = %d", &port);
            return port;
        }
    }
    return -1;
}

/* -------------------------------------------------------------------------
 * CLI 调试处理（4 个命令域）
 * ------------------------------------------------------------------------- */
int app_debug(void)
{
    char *cmd[3] = { cli::get(0), cli::get(1), cli::get(2) };
    if (strcmp(cmd[0], "app") != 0) { return 0; }
    return app_debug_ctl(cmd[1], cmd[2], NULL);
}

int bsp_debug(void)
{
    char *cmd[4] = { cli::get(0), cli::get(1), cli::get(2), cli::get(3) };
    if (strcmp(cmd[0], "bsp") != 0) { return 0; }
    return bsp_debug_ctl(cmd[1], cmd[2], cmd[3]);
}

int alarm_debug(void)
{
    char *cmd[3] = { cli::get(0), cli::get(1), cli::get(2) };
    if (strcmp(cmd[0], "alarm") != 0) { return 0; }
    return alarm_debug_ctl(cmd[1], cmd[2], NULL);
}

int param_debug(void)
{
    char *cmd[4] = { cli::get(0), cli::get(1), cli::get(2), cli::get(3) };
    if (strcmp(cmd[0], "param") != 0) { return 0; }
    return param_debug_ctl(cmd[1], cmd[2], cmd[3]);
}

/* -------------------------------------------------------------------------
 * 应用入口
 * ------------------------------------------------------------------------- */
int app_main(int, char **)
{
    set_log_level(6);
    set_app_version((char *)APP_NAME, (char *)APP_VERSION);
    set_remote_port(get_remote_port("/home/neardi/tool/frp/frpc.ini"));

    cli::init();
    cli::add(app_debug);
    cli::add(bsp_debug);
    cli::add(alarm_debug);
    cli::add(param_debug);

    io_logApi_set(app_io_log);

    if (bsp_system_init() != SW_OK) {
        LOG_ERROR("app_main: bsp_system_init failed, abort");
        return -1;
    }

    if (app_fsm_init() != SW_OK) {
        LOG_ERROR("app_main: app_fsm_init failed, abort");
        return -1;
    }

    (void)app_cloud_init();

    LOG_INFO("%s %s started", APP_NAME, APP_VERSION);

    while (true) {
        sleep(10);
    }

    return 0;
}
