/**
 * @file    snack_stub.c
 * @brief   BUILD_SIM 使用的 snack SDK 兼容桩
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    仿真构建不会链接 snack SDK。
 *          本文件提供 common/log.h 所需的最小符号集合。
 */

#include "projects/m8/adapters/runtime/snack/snack_log.h"
#include "projects/m8/adapters/runtime/snack/snack_mqtt.h"
#include <stdio.h>
#include <stdarg.h>

/* -------------------------------------------------------------------------
 * 日志实现（输出到 stderr，带级别前缀）
 * ------------------------------------------------------------------------- */
static void sim_vlog(const char *level, const char *fmt, va_list ap)
{
    fprintf(stderr, "[%s] ", level);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
}

void snack_log_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    sim_vlog("ERR", fmt, ap);
    va_end(ap);
}

void snack_log_warn(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    sim_vlog("WRN", fmt, ap);
    va_end(ap);
}

void snack_log_info(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    sim_vlog("INF", fmt, ap);
    va_end(ap);
}

void snack_log_debug(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    sim_vlog("DBG", fmt, ap);
    va_end(ap);
}

/* -------------------------------------------------------------------------
 * 其余 snack 函数：空桩（sim 构建中不会被调用，但某些编译单元可能
 * 引用了 snack_wrapper.h 中声明的其他符号；此处兜底防止链接错误）
 * ------------------------------------------------------------------------- */
void set_log_level(int type)                        { (void)type; }
void set_app_version(char *name, char *ver)         { (void)name; (void)ver; }
void set_remote_port(int port)                      { (void)port; }

/* MQTT 桩（snack_cloud_adapter 已从 sim 排除，但保留防意外链接）*/
int  aliyun_mqtt_init(char *pk, char *dn, char *ds) { (void)pk; (void)dn; (void)ds; return -1; }
int  mqtt_is_online(void)                           { return 0; }
int  net_mqtt_send(char *topic, char *msg)          { (void)topic; (void)msg; return -1; }
void mqtt_recv_handler_set(mqtt_recv_handler_t cb)  { (void)cb; }
