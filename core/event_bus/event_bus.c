/**
 * @file    event_bus.c
 * @brief   统一事件总线实现
 * @author  胡望伟
 * @date    2026-04-10
 */

#include "core/event_bus/event_bus.h"
#include "config/threading/thread_config.h"
#include "common/time_util.h"

#include <pthread.h>
#include <semaphore.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

/* -------------------------------------------------------------------------
 * 内部日志（core/ 不依赖 snack SDK，使用 printf 作为轻量日志）
 * ------------------------------------------------------------------------- */
#define EVT_LOG_WARN(fmt, ...)  printf("[EVT WARN] " fmt "\n", ##__VA_ARGS__)
#define EVT_LOG_ERROR(fmt, ...) printf("[EVT ERR]  " fmt "\n", ##__VA_ARGS__)

/* -------------------------------------------------------------------------
 * 环形队列
 * ------------------------------------------------------------------------- */
static event_t          s_queue[EVENT_BUS_QUEUE_SIZE];
static uint32_t         s_q_head;
static uint32_t         s_q_tail;
static uint32_t         s_q_count;
static pthread_mutex_t  s_q_mutex = PTHREAD_MUTEX_INITIALIZER;

/* -------------------------------------------------------------------------
 * 订阅表 [event_type][slot]
 * ------------------------------------------------------------------------- */
static event_handler_t  s_subs[EVT_MAX][EVENT_BUS_MAX_SUBS_PER_EVT];
static pthread_mutex_t  s_sub_mutex = PTHREAD_MUTEX_INITIALIZER;

/* -------------------------------------------------------------------------
 * 信号量（dispatch 线程等待事件入队）
 * ------------------------------------------------------------------------- */
static sem_t        s_sem;
static int          s_sem_initialized = 0;

/* -------------------------------------------------------------------------
 * 初始化状态
 * ------------------------------------------------------------------------- */
static volatile int s_initialized = 0;
static volatile int s_shutdown_requested = 0;

/* -------------------------------------------------------------------------
 * 运行统计
 * ------------------------------------------------------------------------- */
static event_bus_stats_t s_stats;

/* -------------------------------------------------------------------------
 * event_bus_init
 * ------------------------------------------------------------------------- */
sw_err_t event_bus_init(void)
{
    s_shutdown_requested = 0;

    /* 重置队列 */
    pthread_mutex_lock(&s_q_mutex);
    memset(s_queue, 0, sizeof(s_queue));
    s_q_head  = 0;
    s_q_tail  = 0;
    s_q_count = 0;
    memset(&s_stats, 0, sizeof(s_stats));
    pthread_mutex_unlock(&s_q_mutex);

    /* 重置订阅表 */
    pthread_mutex_lock(&s_sub_mutex);
    memset(s_subs, 0, sizeof(s_subs));
    pthread_mutex_unlock(&s_sub_mutex);

    /* 信号量初始化 / 重置
     *
     * PRE-CONDITION（调用方负责保证）：
     *   调用此函数时 dispatch 线程必须已停止（不再阻塞于 sem_wait）。
     *   若 dispatch 线程仍在运行时调用 sem_destroy，是 POSIX 未定义行为。
     *
     * 实现策略：
     *   首次调用：sem_init 创建信号量。
     *   后续调用：排空残余计数（不 destroy/recreate），避免 UB 风险。
     */
    if (!s_sem_initialized)
    {
        if (sem_init(&s_sem, 0, 0) != 0)
        {
            EVT_LOG_ERROR("sem_init failed");
            return SW_ERR_HW;
        }
        s_sem_initialized = 1;
    }
    else
    {
        /* 排空残余信号量计数（dispatch 已停止，sem_trywait 不会阻塞）*/
        int val = 0;
        sem_getvalue(&s_sem, &val);
        for (int i = 0; i < val; i++)
        {
            sem_trywait(&s_sem);
        }
    }

    s_initialized = 1;
    return SW_OK;
}

/* -------------------------------------------------------------------------
 * event_bus_shutdown
 * ------------------------------------------------------------------------- */
sw_err_t event_bus_shutdown(void)
{
    if (!s_initialized)
    {
        return SW_ERR_NOT_INIT;
    }

    s_shutdown_requested = 1;
    s_initialized        = 0;

    if (sem_post(&s_sem) != 0)
    {
        pthread_mutex_lock(&s_q_mutex);
        s_stats.sem_post_fail_count++;
        pthread_mutex_unlock(&s_q_mutex);
        EVT_LOG_ERROR("shutdown sem_post failed");
        return SW_ERR_HW;
    }

    return SW_OK;
}

/* -------------------------------------------------------------------------
 * event_publish
 * ------------------------------------------------------------------------- */
sw_err_t event_publish(event_type_t type, uint32_t param)
{
    if (!s_initialized)
    {
        return SW_ERR_NOT_INIT;
    }
    if (type == EVT_NONE || type >= EVT_MAX)
    {
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_q_mutex);

    if (s_q_count >= EVENT_BUS_QUEUE_SIZE)
    {
        s_stats.dropped_count++;
        pthread_mutex_unlock(&s_q_mutex);
        EVT_LOG_WARN("queue full, drop EVT type=%d param=%u", (int)type, param);
        return SW_ERR_OVERFLOW;
    }

    event_t *slot      = &s_queue[s_q_tail];
    slot->type         = type;
    slot->param        = param;
    slot->timestamp_ms = time_util_get_ms(); /* 入队时自动打时间戳 */

    s_q_tail  = (s_q_tail + 1U) % EVENT_BUS_QUEUE_SIZE;
    s_q_count++;
    s_stats.published_count++;
    s_stats.queue_depth = s_q_count;
    if (s_stats.queue_peak_depth < s_q_count)
    {
        s_stats.queue_peak_depth = s_q_count;
    }

    if (sem_post(&s_sem) != 0) /* 通知 dispatch 线程 */
    {
        s_q_tail = (s_q_tail == 0U) ? (EVENT_BUS_QUEUE_SIZE - 1U) : (s_q_tail - 1U);
        s_q_count--;
        s_stats.published_count--;
        s_stats.queue_depth = s_q_count;
        s_stats.sem_post_fail_count++;
        pthread_mutex_unlock(&s_q_mutex);
        EVT_LOG_WARN("sem_post failed for EVT type=%d", (int)type);
        return SW_ERR_HW;
    }

    pthread_mutex_unlock(&s_q_mutex);
    return SW_OK;
}

/* -------------------------------------------------------------------------
 * event_subscribe
 * ------------------------------------------------------------------------- */
sw_err_t event_subscribe(event_type_t type, event_handler_t handler)
{
    int inserted = 0;

    if (!s_initialized)
    {
        return SW_ERR_NOT_INIT;
    }
    if (type == EVT_NONE || type >= EVT_MAX || handler == NULL)
    {
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_sub_mutex);

    for (uint32_t i = 0; i < EVENT_BUS_MAX_SUBS_PER_EVT; i++)
    {
        if (s_subs[type][i] == handler)
        {
            /* 重复订阅：幂等处理，直接返回 OK（不重复添加）*/
            pthread_mutex_unlock(&s_sub_mutex);
            return SW_OK;
        }
        if (s_subs[type][i] == NULL)
        {
            s_subs[type][i] = handler;
            pthread_mutex_unlock(&s_sub_mutex);
            inserted = 1;
            break;
        }
    }

    if (!inserted)
    {
        pthread_mutex_unlock(&s_sub_mutex);
        EVT_LOG_ERROR("subscriber table full for EVT type=%d", (int)type);
        return SW_ERR_OVERFLOW;
    }

    /* 统计字段统一由 s_q_mutex 保护，避免为 stats 再引入第三把锁 */
    pthread_mutex_lock(&s_q_mutex);
    s_stats.subscribe_count++;
    pthread_mutex_unlock(&s_q_mutex);
    return SW_OK;
}

/* -------------------------------------------------------------------------
 * event_bus_get_stats
 * ------------------------------------------------------------------------- */
sw_err_t event_bus_get_stats(event_bus_stats_t *stats)
{
    if (stats == NULL)
    {
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_q_mutex);
    *stats = s_stats;
    pthread_mutex_unlock(&s_q_mutex);
    return SW_OK;
}

/* -------------------------------------------------------------------------
 * event_bus_dispatch_loop
 * ------------------------------------------------------------------------- */
void event_bus_dispatch_loop(void)
{
    event_t             dispatch_evt;
    event_handler_t     handlers[EVENT_BUS_MAX_SUBS_PER_EVT];

    while (1)
    {
        while (sem_wait(&s_sem) != 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            pthread_mutex_lock(&s_q_mutex);
            s_stats.sem_wait_fail_count++;
            pthread_mutex_unlock(&s_q_mutex);
            EVT_LOG_ERROR("sem_wait failed errno=%d", errno);
            return;
        }

        /* 出队 */
        pthread_mutex_lock(&s_q_mutex);
        if (s_q_count == 0U)
        {
            if (s_shutdown_requested)
            {
                pthread_mutex_unlock(&s_q_mutex);
                return;
            }

            /* 并发 publish 后 sem 计数可能超过实际队列数量，正常跳过 */
            pthread_mutex_unlock(&s_q_mutex);
            continue;
        }
        dispatch_evt  = s_queue[s_q_head];
        s_q_head      = (s_q_head + 1U) % EVENT_BUS_QUEUE_SIZE;
        s_q_count--;
        s_stats.dispatched_count++;
        s_stats.queue_depth = s_q_count;
        pthread_mutex_unlock(&s_q_mutex);

        /* 复制 handler 快照（最小化持锁时间，避免 handler 内部 subscribe 死锁）*/
        pthread_mutex_lock(&s_sub_mutex);
        memcpy(handlers, s_subs[dispatch_evt.type], sizeof(handlers));
        pthread_mutex_unlock(&s_sub_mutex);

        /* 逐一回调 */
        for (uint32_t i = 0; i < EVENT_BUS_MAX_SUBS_PER_EVT; i++)
        {
            if (handlers[i] != NULL)
            {
                handlers[i](&dispatch_evt);
            }
        }
    }
}
