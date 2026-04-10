/**
 * @file    app_main.cpp
 * @brief   应用入口（snack 框架回调 app_main，非 main）
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    新架构入口：调用 bootstrap_run() 完成所有初始化和线程启动。
 *          CLI 注册由 bootstrap 调用 cli_adapter_init()（见 bootstrap.c 步骤 19）。
 *          IO 日志和 frp 远程端口设置在此层完成（snack SDK 相关，不属于 bootstrap 职责）。
 */

#include "core/bootstrap/bootstrap.h"
#include "common/sw_version.h"
#include "common/log.h"
#include "middleware/snack_wrapper.h"
#include "log/mlog.h"
#include "io_exp/demo.h"
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
    while (std::getline(fin, line))
    {
        if (line.find("remote_port") == 0)
        {
            int port = -1;
            sscanf(line.c_str(), "remote_port = %d", &port);
            return port;
        }
    }
    return -1;
}

/* -------------------------------------------------------------------------
 * 应用入口（snack 框架回调）
 * ------------------------------------------------------------------------- */
int app_main(int, char **)
{
    set_log_level(6);
    set_app_version((char *)APP_NAME, (char *)APP_VERSION);
    set_remote_port(get_remote_port("/home/neardi/tool/frp/frpc.ini"));

    io_logApi_set(app_io_log);

    if (bootstrap_run() != SW_OK)
    {
        LOG_ERROR("app_main: bootstrap_run failed, abort");
        return -1;
    }

    LOG_INFO("%s %s started", APP_NAME, APP_VERSION);

    while (true)
    {
        sleep(10);
    }

    return 0;
}
