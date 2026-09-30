/**
 * @file    log.c
 * @brief   统一日志实现（可插拔 sink，默认 stderr 输出）
 * @author  HUWANGWEI
 * @date    2026-07-04
 */

#include "common/log.h"

#include "common/sw_mutex.h"
#include "common/trace_context.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

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
 * 启用优先级继承：estop_poll 与 control_loop 以 SCHED_FIFO 运行时会写日志。
 * 这两条线程只入队（持 s_spool_mutex），写出在 drain 线程完成。
 *
 * 用 pthread_once 而非静态初始化：日志可能在任何 init 之前就被调用。 */
static pthread_mutex_t s_mutex;
static pthread_mutex_t s_spool_mutex;
static pthread_cond_t  s_spool_cond;
static clockid_t       s_spool_cond_clock = CLOCK_REALTIME;
static pthread_once_t  s_mutex_once       = PTHREAD_ONCE_INIT;

/** 异步排出：被标记线程入队，drain 线程写出，避免 FIFO 路径上 fwrite */
#define LOG_COMPONENT_MAX      32U
#define LOG_DRAIN_IDLE_WAIT_S  1 /* drain 无新日志时单次等待上限（秒） */

typedef struct {
    sw_log_level_t  level;
    trace_context_t trace; /* 入队线程的 trace 上下文，drain 写 sink 前恢复 */
    char            component[LOG_COMPONENT_MAX];
    char            message[SW_LOG_LINE_MAX];
} log_spool_item_t;

static log_spool_item_t      s_spool[SW_LOG_SPOOL_CAP];
static uint32_t              s_spool_head;
static uint32_t              s_spool_count;
static atomic_bool           s_async_enabled;
static atomic_uint_least32_t s_spool_dropped;
static _Thread_local bool    s_thread_async;

static void default_sink(sw_log_level_t level, const char *component, const char *fmt, va_list ap);

static void log_mutex_init_once(void)
{
    pthread_condattr_t attr;

    (void)sw_mutex_init_prio_inherit(&s_mutex);
    (void)sw_mutex_init_prio_inherit(&s_spool_mutex);

    if (pthread_condattr_init(&attr) != 0) {
        (void)pthread_cond_init(&s_spool_cond, NULL);
        return;
    }
    if (pthread_condattr_setclock(&attr, CLOCK_MONOTONIC) == 0) {
        s_spool_cond_clock = CLOCK_MONOTONIC;
    }
    (void)pthread_cond_init(&s_spool_cond, &attr);
    (void)pthread_condattr_destroy(&attr);
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

static void spool_lock(void)
{
    (void)pthread_once(&s_mutex_once, log_mutex_init_once);
    (void)pthread_mutex_lock(&s_spool_mutex);
}

static void spool_unlock(void)
{
    (void)pthread_mutex_unlock(&s_spool_mutex);
}

static void snapshot_sinks(sw_log_sink_fn_t *sinks, size_t *count)
{
    size_t index;

    log_lock();
    *count = s_sink_count;
    for (index = 0U; index < *count; index++) {
        sinks[index] = s_sinks[index];
    }
    log_unlock();
}

static void invoke_sink(sw_log_sink_fn_t sink, sw_log_level_t level, const char *component, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    sink(level, component, fmt, ap);
    va_end(ap);
}

static void dispatch_message(sw_log_level_t level, const char *component, const char *message)
{
    sw_log_sink_fn_t sinks[SW_LOG_SINK_MAX];
    size_t           count;
    size_t           index;

    snapshot_sinks(sinks, &count);
    if (count == 0U) {
        invoke_sink(default_sink, level, component, "%s", message);
        return;
    }
    for (index = 0U; index < count; index++) {
        invoke_sink(sinks[index], level, component, "%s", message);
    }
}

static bool spool_push(sw_log_level_t level, const char *component, const char *message)
{
    log_spool_item_t *item;

    spool_lock();
    if (s_spool_count >= SW_LOG_SPOOL_CAP) {
        spool_unlock();
        return false;
    }
    item        = &s_spool[(s_spool_head + s_spool_count) % SW_LOG_SPOOL_CAP];
    item->level = level;
    item->trace = trace_context_get();
    (void)memset(item->component, 0, sizeof(item->component));
    if (component != NULL) {
        (void)strncpy(item->component, component, sizeof(item->component) - 1U);
    }
    (void)memset(item->message, 0, sizeof(item->message));
    if (message != NULL) {
        (void)strncpy(item->message, message, sizeof(item->message) - 1U);
    }
    s_spool_count++;
    (void)pthread_cond_signal(&s_spool_cond);
    spool_unlock();
    return true;
}

/** @brief 取一条待写日志；队列空时限时等待，超时仍空返回 false */
static bool spool_wait_pop(log_spool_item_t *out)
{
    struct timespec deadline;
    bool            got = false;

    spool_lock();
    if (s_spool_count == 0U) {
        (void)clock_gettime(s_spool_cond_clock, &deadline);
        deadline.tv_sec += LOG_DRAIN_IDLE_WAIT_S;
        (void)pthread_cond_timedwait(&s_spool_cond, &s_spool_mutex, &deadline);
    }
    if (s_spool_count > 0U) {
        *out         = s_spool[s_spool_head];
        s_spool_head = (s_spool_head + 1U) % SW_LOG_SPOOL_CAP;
        s_spool_count--;
        got = true;
    }
    spool_unlock();
    return got;
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

void sw_log_enable_async(void)
{
    atomic_store(&s_async_enabled, true);
}

void sw_log_mark_thread_async(void)
{
    s_thread_async = true;
}

uint32_t sw_log_dropped_count(void)
{
    return (uint32_t)atomic_load(&s_spool_dropped);
}

void *sw_log_drain_thread_fn(void *arg)
{
    log_spool_item_t item;

    (void)arg;
    for (;;) {
        if (!spool_wait_pop(&item)) {
            continue;
        }
        trace_context_set(&item.trace);
        dispatch_message(item.level, item.component, item.message);
        trace_context_set(NULL);
    }
}

void sw_log_write(sw_log_level_t level, const char *component, const char *fmt, ...)
{
    va_list ap;
    char    message[SW_LOG_LINE_MAX];
    int     written;

    /* 级别过滤前置：被丢弃的日志不付出格式化代价 */
    log_lock();
    if (level > s_level) {
        log_unlock();
        return;
    }
    log_unlock();

    va_start(ap, fmt);
    written = vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);
    if (written < 0) {
        return;
    }
    message[sizeof(message) - 1U] = '\0';

    if (s_thread_async && atomic_load(&s_async_enabled)) {
        /* 队列满时丢弃计数，不在 FIFO 路径上回退 stdio；由 health 上报丢弃数 */
        if (!spool_push(level, component, message)) {
            (void)atomic_fetch_add(&s_spool_dropped, 1U);
        }
        return;
    }

    dispatch_message(level, component, message);
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
