/**
 * @file    log.c
 * @brief   统一日志实现（可插拔 sink，默认 stderr 输出）
 * @author  HUWANGWEI
 * @date    2026-07-04
 */

#include "common/log.h"

#include "common/sw_mutex.h"

#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define SW_LOG_SINK_MAX 4U

/** 单条日志的最大长度（含级别与组件前缀）；超长部分被截断 */
#define SW_LOG_LINE_MAX 512U

static sw_log_sink_fn_t s_sinks[SW_LOG_SINK_MAX];
static size_t           s_sink_count;
static sw_log_level_t   s_level = SW_LOG_DEBUG;

/* sink 表与级别门限的保护锁。
 * sw_log_register_sink 会先把 count 清零再赋值，若无锁保护，
 * 并发写日志的线程可能读到"count 已归零但 sink 未就绪"的中间态。
 *
 * 启用优先级继承：estop_poll 线程以 SCHED_FIFO 90 运行，每次急停边沿都会
 * LOG_WARN/LOG_INFO，而本锁同时被 9 个 SCHED_OTHER 周期任务竞争。若普通
 * 优先级线程持锁期间被抢占，急停线程会阻塞且时长不受优先级保护——急停边沿
 * 正是最不能容忍这种阻塞的时刻。与 event_bus 两把锁同样处理。
 *
 * 用 pthread_once 而非静态初始化：日志可能在任何 init 之前就被调用，
 * 没有可靠的集中初始化时机；once 保证首次使用前恰好初始化一次。 */
static pthread_mutex_t s_mutex;
static pthread_once_t  s_mutex_once = PTHREAD_ONCE_INIT;

static void log_mutex_init_once(void)
{
    (void)sw_mutex_init_prio_inherit(&s_mutex);
}

/** @brief 确保锁已初始化（幂等，所有加锁点入口调用）*/
static void log_lock(void)
{
    (void)pthread_once(&s_mutex_once, log_mutex_init_once);
    (void)pthread_mutex_lock(&s_mutex);
}

static void log_unlock(void)
{
    (void)pthread_mutex_unlock(&s_mutex);
}

static const char *level_tag(sw_log_level_t level)
{
    static const char *const s_level_tag[] = {"ERR", "WRN", "INF", "DBG"};

    /* 防御越界：level 来自公开接口，非法值不得用于数组下标 */
    if ((level < SW_LOG_ERROR) || (level > SW_LOG_DEBUG)) {
        return "UNK";
    }
    return s_level_tag[level];
}

/*
 * 默认 sink：先整条组装进栈缓冲，再单次 fwrite。
 * 原实现用 fprintf + vfprintf + fputc 三次独立 stdio 调用，
 * 多线程并发时同一行的内容会被其他线程的输出插入其中。
 */
static void default_sink(sw_log_level_t level, const char *component, const char *fmt, va_list ap)
{
    char line[SW_LOG_LINE_MAX];
    int  head;
    int  used;

    head = snprintf(line, sizeof(line), "[%s] [%s] ", level_tag(level), component);
    if (head < 0) {
        return;
    }
    used = head;
    if ((size_t)used < sizeof(line) - 1U) {
        int body = vsnprintf(&line[used], sizeof(line) - (size_t)used, fmt, ap);

        if (body > 0) {
            used += body;
            if ((size_t)used > sizeof(line) - 1U) {
                used = (int)(sizeof(line) - 1U); /* vsnprintf 已截断，修正长度 */
            }
        }
    }
    line[used] = '\n';
    used++;

    (void)fwrite(line, 1U, (size_t)used, stderr);
    (void)fflush(stderr);
}

void sw_log_set_level(sw_log_level_t level)
{
    if ((level < SW_LOG_ERROR) || (level > SW_LOG_DEBUG)) {
        return;
    }
    log_lock();
    s_level = level;
    log_unlock();
}

sw_log_level_t sw_log_get_level(void)
{
    sw_log_level_t level;

    log_lock();
    level = s_level;
    log_unlock();
    return level;
}

bool sw_log_level_enabled(sw_log_level_t level)
{
    return level <= sw_log_get_level();
}

void sw_log_register_sink(sw_log_sink_fn_t sink)
{
    log_lock();
    s_sink_count = 0U;
    if (sink != NULL) {
        s_sinks[0]   = sink;
        s_sink_count = 1U;
    }
    log_unlock();
}

bool sw_log_add_sink(sw_log_sink_fn_t sink)
{
    size_t index;
    bool   ok = false;

    if (sink == NULL) {
        return false;
    }

    log_lock();
    for (index = 0U; index < s_sink_count; index++) {
        if (s_sinks[index] == sink) {
            log_unlock();
            return true;
        }
    }
    if (s_sink_count < SW_LOG_SINK_MAX) {
        s_sinks[s_sink_count] = sink;
        s_sink_count++;
        ok = true;
    }
    log_unlock();
    return ok;
}

void sw_log_write(sw_log_level_t level, const char *component, const char *fmt, ...)
{
    va_list          ap;
    sw_log_sink_fn_t sinks[SW_LOG_SINK_MAX];
    size_t           count;
    size_t           index;

    /* 级别过滤前置：被丢弃的日志不付出格式化代价 */
    log_lock();
    if (level > s_level) {
        log_unlock();
        return;
    }
    /* 取 sink 快照后即释放锁，避免 sink 内部再次写日志造成自锁 */
    count = s_sink_count;
    for (index = 0U; index < count; index++) {
        sinks[index] = s_sinks[index];
    }
    log_unlock();

    va_start(ap, fmt);
    if (count == 0U) {
        default_sink(level, component, fmt, ap);
    } else {
        for (index = 0U; index < count; index++) {
            va_list sink_ap;

            va_copy(sink_ap, ap);
            sinks[index](level, component, fmt, sink_ap);
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

    unix_separator    = strrchr(source_path, '/');
    windows_separator = strrchr(source_path, '\\');
    last_separator    = unix_separator;
    if ((last_separator == NULL) || ((windows_separator != NULL) && (windows_separator > last_separator))) {
        last_separator = windows_separator;
    }

    return (last_separator == NULL) ? source_path : (last_separator + 1);
}
