/**
 * @file    m8_runtime_adapter.cpp
 * @brief   M8 机型运行时适配实现
 * @author  HUWANGWEI
 * @date    2026-06-23
 *
 * @note    从 /home/neardi/tool/frp/frpc.ini 读取远程调试端口，
 *          并注册 IO 子板日志回调。此为 M8 设备专属配置。
 */

#include "adapters/machine/m8/m8_runtime_adapter.h"
#include "adapters/sdk/snack/snack_wrapper.h"
#include "log/mlog.h"
#include "io_exp/demo.h"

#include <fstream>
#include <string>
#include <stdarg.h>
#include <stdio.h>

static mlog *s_io_log = new mlog("io_exp");

/* -------------------------------------------------------------------------
 * IO 子板日志回调
 * ------------------------------------------------------------------------- */
static int snack_io_log(const char *fmt, ...)
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
 * 从 frpc.ini 读取远程调试端口
 * ------------------------------------------------------------------------- */
static int load_remote_port(const std::string &filename)
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
 * 对外接口
 * ------------------------------------------------------------------------- */
void m8_runtime_adapter_init(void)
{
    set_remote_port(load_remote_port("/home/neardi/tool/frp/frpc.ini"));
    io_logApi_set(snack_io_log);
}
