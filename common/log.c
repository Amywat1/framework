/**
 * @file    log.c
 * @brief   统一日志实现（可插拔 sink，默认 stderr 输出）
 * @author  HUWANGWEI
 * @date    2026-07-04
 */

#include "common/log.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static sw_log_sink_fn_t s_sink = NULL;

static void default_sink(sw_log_level_t level, const char *fmt, va_list ap)
{
    static const char *const s_level_tag[] = {"ERR", "WRN", "INF", "DBG"};

    fprintf(stderr, "[%s] ", s_level_tag[level]);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
}

void sw_log_register_sink(sw_log_sink_fn_t sink)
{
    s_sink = sink;
}

void sw_log_write(sw_log_level_t level, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    if (s_sink != NULL) {
        s_sink(level, fmt, ap);
    } else {
        default_sink(level, fmt, ap);
    }
    va_end(ap);
}

const char *sw_log_source_file_name(const char *source_path)
{
    const char *unix_separator;
    const char *windows_separator;
    const char *last_separator;

    if (source_path == NULL) {
        return "";
    }

    unix_separator = strrchr(source_path, '/');
    windows_separator = strrchr(source_path, '\\');
    last_separator = unix_separator;
    if ((last_separator == NULL) || ((windows_separator != NULL) && (windows_separator > last_separator))) {
        last_separator = windows_separator;
    }

    return (last_separator == NULL) ? source_path : (last_separator + 1);
}
