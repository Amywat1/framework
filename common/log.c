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
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define SW_LOG_SINK_MAX 4U

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
    struct timespec occurred_at;
    char            component[LOG_COMPONENT_MAX];
    char            message[SW_LOG_LINE_MAX];
} log_spool_item_t;

/** sink 回调期间可见的发生时刻；嵌套 LOG_* 时按栈保存/恢复 */
typedef struct {
    struct timespec occurred_at;
    bool            valid;
} log_time_scope_t;

static log_spool_item_t       s_spool[SW_LOG_SPOOL_CAP];
static uint32_t               s_spool_head;
static uint32_t               s_spool_count;
static atomic_bool            s_async_enabled;
static atomic_uint_least32_t  s_spool_dropped;
static _Thread_local bool             s_thread_async;
static _Thread_local log_time_scope_t s_occurred;
/** 同步路径正文；default_sink 另用 s_sink_line，避免覆盖尚未写出的 message */
static _Thread_local char             s_write_line[SW_LOG_LINE_MAX];
static _Thread_local char             s_sink_line[SW_LOG_LINE_MAX];
/** drain 单线程复用，避免在 16KB 排出栈上再开一条目 */
static log_spool_item_t               s_drain_item;

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

/** @brief 调用点采样墙上时钟；失败则写 0 */
static void capture_occurred_at(struct timespec *out)
{
    if (clock_gettime(CLOCK_REALTIME, out) != 0) {
        out->tv_sec  = 0;
        out->tv_nsec = 0;
    }
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

/**
 * @brief  带发生时刻调用各 sink；嵌套 LOG_* 时恢复外层时刻
 */
static void dispatch_recorded(sw_log_level_t level, const char *component, const char *message,
                              struct timespec occurred_at)
{
    log_time_scope_t saved = s_occurred;

    s_occurred.occurred_at = occurred_at;
    s_occurred.valid       = true;
    dispatch_message(level, component, message);
    s_occurred = saved;
}

/**
 * @brief  直接格式化进队列槽，FIFO 线程栈上不落整行缓冲
 */
static bool spool_push_vformat(sw_log_level_t level, const char *component, const char *fmt, va_list ap,
                               struct timespec occurred_at)
{
    log_spool_item_t *item;
    int               written;

    spool_lock();
    if (s_spool_count >= SW_LOG_SPOOL_CAP) {
        spool_unlock();
        return false;
    }
    item              = &s_spool[(s_spool_head + s_spool_count) % SW_LOG_SPOOL_CAP];
    item->level       = level;
    item->trace       = trace_context_get();
    item->occurred_at = occurred_at;
    (void)memset(item->component, 0, sizeof(item->component));
    if (component != NULL) {
        (void)strncpy(item->component, component, sizeof(item->component) - 1U);
    }
    written = vsnprintf(item->message, sizeof(item->message), fmt, ap);
    if (written < 0) {
        spool_unlock();
        return true;
    }
    (void)sw_log_clip_line(item->message, sizeof(item->message), written);
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

/**
 * @brief  按实测拼装长度收口一行
 */
size_t sw_log_clip_line(char *line, size_t cap, int written)
{
    const size_t mark_len = sizeof(SW_LOG_TRUNCATED_MARK) - 1U;
    size_t       used;

    if ((line == NULL) || (cap == 0U)) {
        return 0U;
    }
    if (written < 0) {
        line[0] = '\0';
        return 0U;
    }

    used = (size_t)written;
    if (used < cap) {
        line[used] = '\0';
        return used;
    }
    if (cap <= 1U) {
        line[0] = '\0';
        return 0U;
    }

    used = cap - 1U;
    if (used >= mark_len) {
        (void)memcpy(line + (used - mark_len), SW_LOG_TRUNCATED_MARK, mark_len);
    } else {
        (void)memcpy(line, SW_LOG_TRUNCATED_MARK, used);
    }
    line[used] = '\0';
    return used;
}

/*
 * 默认 sink：先整条组装进栈缓冲，再按实测行长收口，最后单次 fwrite。
 * 原实现用 fprintf + vfprintf + fputc 三次独立 stdio 调用，
 * 多线程并发时同一行的内容会被其他线程的输出插入其中。
 */
static void default_sink(sw_log_level_t level, const char *component, const char *fmt, va_list ap)
{
    int    head;
    int    written;
    size_t used;

    if (component == NULL) {
        component = "-";
    }
    if (fmt == NULL) {
        return;
    }

    head = snprintf(s_sink_line, sizeof(s_sink_line), "[%s] [%s] ", level_tag(level), component);
    if (head < 0) {
        return;
    }
    if ((size_t)head >= sizeof(s_sink_line)) {
        written = head;
    } else {
        int body = vsnprintf(&s_sink_line[head], sizeof(s_sink_line) - (size_t)head, fmt, ap);

        if (body < 0) {
            return;
        }
        written = head + body;
    }

    used             = sw_log_clip_line(s_sink_line, sizeof(s_sink_line), written);
    s_sink_line[used] = '\n';
    (void)fwrite(s_sink_line, 1U, used + 1U, stderr);
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

bool sw_log_occurred_at(struct timespec *out)
{
    if ((out == NULL) || !s_occurred.valid) {
        return false;
    }
    *out = s_occurred.occurred_at;
    return true;
}

size_t sw_log_format_occurred_at(const struct timespec *ts, char *buffer, size_t size, bool with_date)
{
    struct timespec local;
    struct tm       broken_down;
    char            date_time[32];
    int             written;

    if ((buffer == NULL) || (size == 0U)) {
        return 0U;
    }
    if (ts == NULL) {
        if (!sw_log_occurred_at(&local)) {
            (void)snprintf(buffer, size, "-");
            return 1U;
        }
        ts = &local;
    }
    if ((localtime_r(&ts->tv_sec, &broken_down) == NULL)
        || (strftime(date_time, sizeof(date_time), with_date ? "%Y-%m-%d %H:%M:%S" : "%H:%M:%S", &broken_down)
            == 0U)) {
        (void)snprintf(buffer, size, "-");
        return 1U;
    }
    written = snprintf(buffer, size, "%s.%03ld", date_time, ts->tv_nsec / 1000000L);
    if (written < 0) {
        buffer[0] = '\0';
        return 0U;
    }
    return strlen(buffer);
}

uint32_t sw_log_dropped_count(void)
{
    return (uint32_t)atomic_load(&s_spool_dropped);
}

void *sw_log_drain_thread_fn(void *arg)
{
    (void)arg;
    for (;;) {
        if (!spool_wait_pop(&s_drain_item)) {
            continue;
        }
        trace_context_set(&s_drain_item.trace);
        dispatch_recorded(s_drain_item.level, s_drain_item.component, s_drain_item.message, s_drain_item.occurred_at);
        trace_context_set(NULL);
    }
}

void sw_log_write(sw_log_level_t level, const char *component, const char *fmt, ...)
{
    va_list         ap;
    int             written;
    struct timespec occurred_at;

    /* 级别过滤前置：被丢弃的日志不付出格式化代价 */
    log_lock();
    if (level > s_level) {
        log_unlock();
        return;
    }
    log_unlock();

    if (fmt == NULL) {
        return;
    }

    /* 先采发生时刻再格式化/入队，记录的是 LOG_* 调用点而非写出点 */
    capture_occurred_at(&occurred_at);

    va_start(ap, fmt);
    if (s_thread_async && atomic_load(&s_async_enabled)) {
        if (!spool_push_vformat(level, component, fmt, ap, occurred_at)) {
            (void)atomic_fetch_add(&s_spool_dropped, 1U);
        }
        va_end(ap);
        return;
    }
    written = vsnprintf(s_write_line, sizeof(s_write_line), fmt, ap);
    va_end(ap);
    if (written < 0) {
        return;
    }
    (void)sw_log_clip_line(s_write_line, sizeof(s_write_line), written);
    dispatch_recorded(level, component, s_write_line, occurred_at);
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
