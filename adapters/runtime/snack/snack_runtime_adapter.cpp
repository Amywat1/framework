/**
 * @file    snack_runtime_adapter.cpp
 * @brief   snack 运行时适配实现
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "adapters/runtime/snack/snack_runtime_adapter.h"
#include "adapters/runtime/snack/snack_wrapper.h"
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
 * 调试远程端口读取
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
void snack_runtime_adapter_init(void)
{
    set_remote_port(load_remote_port("/home/neardi/tool/frp/frpc.ini"));
    io_logApi_set(snack_io_log);
}
