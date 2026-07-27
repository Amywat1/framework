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

#define SW_LOG_SINK_MAX 4U

static sw_log_sink_fn_t s_sinks[SW_LOG_SINK_MAX];
static size_t           s_sink_count;

static void default_sink(sw_log_level_t level, const char *component, const char *fmt, va_list ap)
{
    static const char *const s_level_tag[] = {"ERR", "WRN", "INF", "DBG"};

    fprintf(stderr, "[%s] [%s] ", s_level_tag[level], component);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
}

void sw_log_register_sink(sw_log_sink_fn_t sink)
{
    s_sink_count = 0U;
    if (sink != NULL) {
        s_sinks[0] = sink;
        s_sink_count = 1U;
    }
}

bool sw_log_add_sink(sw_log_sink_fn_t sink)
{
    size_t index;

    if (sink == NULL) {
        return false;
    }
    for (index = 0U; index < s_sink_count; index++) {
        if (s_sinks[index] == sink) {
            return true;
        }
    }
    if (s_sink_count >= SW_LOG_SINK_MAX) {
        return false;
    }
    s_sinks[s_sink_count] = sink;
    s_sink_count++;
    return true;
}

void sw_log_write(sw_log_level_t level, const char *component, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    if (s_sink_count == 0U) {
        default_sink(level, component, fmt, ap);
    } else {
        size_t index;

        for (index = 0U; index < s_sink_count; index++) {
            va_list sink_ap;

            va_copy(sink_ap, ap);
            s_sinks[index](level, component, fmt, sink_ap);
            va_end(sink_ap);
        }
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
