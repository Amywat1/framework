/**
 * @file    event_bus.c
 * @brief   统一事件总线实现
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "framework/runtime/event_bus/event_bus.h"
#include "framework/runtime/event_bus/event_bus_config.h"
#include "framework/common/time_util.h"

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
 * 普通优先级队列
 * ------------------------------------------------------------------------- */
static event_t          s_queue[EVENT_BUS_QUEUE_SIZE];
static uint32_t         s_q_head;
static uint32_t         s_q_tail;
static uint32_t         s_q_count;

/* -------------------------------------------------------------------------
 * 高优先级队列（SAFETY 类 + ESTOP 硬件事件）
 * dispatch_loop 优先排空高优先级队列，保证安全事件不被业务事件阻塞。
 * ------------------------------------------------------------------------- */
static event_t          s_queue_hi[EVENT_BUS_HI_QUEUE_SIZE];
static uint32_t         s_q_hi_head;
static uint32_t         s_q_hi_tail;
static uint32_t         s_q_hi_count;

static pthread_mutex_t  s_q_mutex = PTHREAD_MUTEX_INITIALIZER;

/* -------------------------------------------------------------------------
 * 优先级路由
 * SAFETY 类（LOCKOUT/WARNING/CLEARED）和 ESTOP 硬件事件走高优先级队列。
 * ------------------------------------------------------------------------- */
static bool event_is_high_priority(event_type_t type)
{
    if (event_type_category(type) == EVT_CAT_SAFETY)
    {
        return true;
    }
    return (type == EVT_HW_ESTOP_ON) || (type == EVT_HW_ESTOP_OFF);
}

/* -------------------------------------------------------------------------
 * 订阅表 [category][local_id][slot]
 * ------------------------------------------------------------------------- */
static event_handler_t  s_subs[EVT_CAT_MAX][EVT_PER_CAT_MAX][EVENT_BUS_MAX_SUBS_PER_EVT];
static pthread_mutex_t  s_sub_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief  按复合编码定位订阅槽数组
 * @param  type  已通过 event_type_is_valid 校验的事件类型
 */
static event_handler_t *subs_slot(event_type_t type)
{
    event_category_t cat = event_type_category(type);
    uint8_t          id  = event_type_local_id(type);

    return s_subs[cat][id];
}

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
 * 不可恢复故障回调
 * ------------------------------------------------------------------------- */
static event_bus_fatal_cb_t s_fatal_cb = NULL;

/* -------------------------------------------------------------------------
 * 运行统计
 * ------------------------------------------------------------------------- */
static event_bus_stats_t s_stats;

/* -------------------------------------------------------------------------
 * event_bus_set_fatal_cb
 * ------------------------------------------------------------------------- */
void event_bus_set_fatal_cb(event_bus_fatal_cb_t cb)
{
    s_fatal_cb = cb;
}

/* -------------------------------------------------------------------------
 * event_bus_init
 * ------------------------------------------------------------------------- */
sw_err_t event_bus_init(void)
{
    s_shutdown_requested = 0;

    /* 重置队列 */
    pthread_mutex_lock(&s_q_mutex);
    memset(s_queue,    0, sizeof(s_queue));
    memset(s_queue_hi, 0, sizeof(s_queue_hi));
    s_q_head     = 0;
    s_q_tail     = 0;
    s_q_count    = 0;
    s_q_hi_head  = 0;
    s_q_hi_tail  = 0;
    s_q_hi_count = 0;
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
    if (!event_type_is_valid(type))
    {
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_q_mutex);

    if (event_is_high_priority(type))
    {
        /* 高优先级队列 */
        if (s_q_hi_count >= EVENT_BUS_HI_QUEUE_SIZE)
        {
            s_stats.dropped_count++;
            pthread_mutex_unlock(&s_q_mutex);
            EVT_LOG_WARN("hi-queue full, drop EVT type=%d param=%u", (int)type, param);
            return SW_ERR_OVERFLOW;
        }

        event_t *slot      = &s_queue_hi[s_q_hi_tail];
        slot->type         = type;
        slot->param        = param;
        slot->timestamp_ms = time_util_get_ms();

        s_q_hi_tail  = (s_q_hi_tail + 1U) % EVENT_BUS_HI_QUEUE_SIZE;
        s_q_hi_count++;
        s_stats.published_count++;
        s_stats.hi_queue_depth = s_q_hi_count;
        if (s_stats.hi_queue_peak_depth < s_q_hi_count)
        {
            s_stats.hi_queue_peak_depth = s_q_hi_count;
        }
    }
    else
    {
        /* 普通优先级队列 */
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
        slot->timestamp_ms = time_util_get_ms();

        s_q_tail  = (s_q_tail + 1U) % EVENT_BUS_QUEUE_SIZE;
        s_q_count++;
        s_stats.published_count++;
        s_stats.queue_depth = s_q_count;
        if (s_stats.queue_peak_depth < s_q_count)
        {
            s_stats.queue_peak_depth = s_q_count;
        }
    }

    if (sem_post(&s_sem) != 0) /* 通知 dispatch 线程 */
    {
        /* 回滚入队 */
        if (event_is_high_priority(type))
        {
            s_q_hi_tail  = (s_q_hi_tail == 0U) ?
                           (EVENT_BUS_HI_QUEUE_SIZE - 1U) : (s_q_hi_tail - 1U);
            s_q_hi_count--;
            s_stats.hi_queue_depth = s_q_hi_count;
        }
        else
        {
            s_q_tail  = (s_q_tail == 0U) ? (EVENT_BUS_QUEUE_SIZE - 1U) : (s_q_tail - 1U);
            s_q_count--;
            s_stats.queue_depth = s_q_count;
        }
        s_stats.published_count--;
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
    if (!event_type_is_valid(type) || handler == NULL)
    {
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_sub_mutex);

    {
        event_handler_t *slot = subs_slot(type);

        for (uint32_t i = 0; i < EVENT_BUS_MAX_SUBS_PER_EVT; i++)
        {
            if (slot[i] == handler)
            {
                /* 重复订阅：幂等处理，直接返回 OK（不重复添加）*/
                pthread_mutex_unlock(&s_sub_mutex);
                return SW_OK;
            }
            if (slot[i] == NULL)
            {
                slot[i]       = handler;
                inserted      = 1;
                break;
            }
        }

        pthread_mutex_unlock(&s_sub_mutex);
    }

    if (!inserted)
    {
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
 * event_subscribe_table
 * ------------------------------------------------------------------------- */
sw_err_t event_subscribe_table(const event_subscription_t *subs, size_t count)
{
    sw_err_t ret;

    if (subs == NULL)
    {
        return SW_ERR_PARAM;
    }

    for (size_t i = 0; i < count; i++)
    {
        ret = event_subscribe(subs[i].type, subs[i].handler);
        if (ret != SW_OK)
        {
            return ret;
        }
    }

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
            EVT_LOG_ERROR("sem_wait failed errno=%d, event bus fatal", errno);

            if (s_fatal_cb != NULL)
            {
                s_fatal_cb(EVENT_BUS_FATAL_SEM_WAIT, errno);
                /* fatal_cb 约定必须终止进程；若违反约定则兜底退出线程 */
            }
            return;
        }

        /* 出队：优先排空高优先级队列 */
        pthread_mutex_lock(&s_q_mutex);
        if ((s_q_hi_count == 0U) && (s_q_count == 0U))
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

        if (s_q_hi_count > 0U)
        {
            dispatch_evt     = s_queue_hi[s_q_hi_head];
            s_q_hi_head      = (s_q_hi_head + 1U) % EVENT_BUS_HI_QUEUE_SIZE;
            s_q_hi_count--;
            s_stats.hi_queue_depth = s_q_hi_count;
        }
        else
        {
            dispatch_evt  = s_queue[s_q_head];
            s_q_head      = (s_q_head + 1U) % EVENT_BUS_QUEUE_SIZE;
            s_q_count--;
            s_stats.queue_depth = s_q_count;
        }
        s_stats.dispatched_count++;
        pthread_mutex_unlock(&s_q_mutex);

        /* 复制 handler 快照（最小化持锁时间，避免 handler 内部 subscribe 死锁）*/
        pthread_mutex_lock(&s_sub_mutex);
        if (event_type_is_valid(dispatch_evt.type))
        {
            memcpy(handlers, subs_slot(dispatch_evt.type), sizeof(handlers));
        }
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
