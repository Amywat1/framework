/**
 * @file    event_bus.c
 * @brief   统一事件总线实现
 * @author  HUWANGWEI
 * @date    2026-04-10
 */

#include "runtime/event_bus/event_bus.h"

#include "common/log.h"
#include "common/time_util.h"
#include "runtime/event_bus/event_bus_config.h"

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * 内部日志
 *
 * 统一走 sw_log，使队列满、丢事件、sem 失败这类关键诊断能进入
 * 已注册的 sink 链（黑匣子、可观测服务），不再直接 printf 到 stdout。
 * R4 允许 runtime/event_bus 依赖 common/。
 * ------------------------------------------------------------------------- */
#define EVT_LOG_WARN(fmt, ...)  LOG_WARN(fmt, ##__VA_ARGS__)
#define EVT_LOG_ERROR(fmt, ...) LOG_ERROR(fmt, ##__VA_ARGS__)

/* -------------------------------------------------------------------------
 * 普通优先级队列
 * ------------------------------------------------------------------------- */
static event_t  s_queue[EVENT_BUS_QUEUE_SIZE];
static uint32_t s_q_head;
static uint32_t s_q_tail;
static uint32_t s_q_count;

/* -------------------------------------------------------------------------
 * 高优先级队列（SAFETY 类 + ESTOP、 IO 在线/离线事件）
 * dispatch_loop 优先排空高优先级队列，保证安全事件不被业务事件阻塞。
 * ------------------------------------------------------------------------- */
static event_t  s_queue_hi[EVENT_BUS_HI_QUEUE_SIZE];
static uint32_t s_q_hi_head;
static uint32_t s_q_hi_tail;
static uint32_t s_q_hi_count;

static pthread_mutex_t s_q_mutex;
static pthread_mutex_t s_sub_mutex;

/* -------------------------------------------------------------------------
 * 互斥量初始化（优先级继承）
 *
 * 事件总线允许被任意优先级的线程调用：框架自带的急停轮询适配器以 SCHED_FIFO
 * 优先级 90 发布事件，项目也可能从自己的实时线程发布。而同一把队列锁会被多个
 * SCHED_OTHER 周期任务竞争。若普通优先级线程持锁期间被抢占，实时线程会阻塞在
 * 互斥量上且阻塞时长不受优先级保护，构成无界优先级反转。
 *
 * 因此两把锁均启用 PTHREAD_PRIO_INHERIT。这是对"总线可被实时线程调用"这一
 * 契约的保护，不取决于某个具体线程当前是否接入。
 *
 * 用 pthread_once 而非静态初始化：event_bus_init() 允许被重复调用
 * （测试 setUp 每次都调），而对已初始化的互斥量再次 pthread_mutex_init
 * 是 POSIX 未定义行为。once 保证恰好初始化一次，且 event_bus_get_stats
 * 这类不检查 s_initialized 的入口也能安全加锁。
 * ------------------------------------------------------------------------- */
static pthread_once_t s_mutex_once = PTHREAD_ONCE_INIT;

static void event_bus_mutex_init_once(void)
{
    pthread_mutexattr_t attr;

    if (pthread_mutexattr_init(&attr) != 0) {
        /* 属性初始化失败：退化为默认互斥量，功能可用但失去优先级继承 */
        (void)pthread_mutex_init(&s_q_mutex, NULL);
        (void)pthread_mutex_init(&s_sub_mutex, NULL);
        return;
    }

    if (pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT) != 0) {
        /* 平台不支持优先级继承：同样退化为默认互斥量 */
        (void)pthread_mutex_init(&s_q_mutex, NULL);
        (void)pthread_mutex_init(&s_sub_mutex, NULL);
        (void)pthread_mutexattr_destroy(&attr);
        return;
    }

    (void)pthread_mutex_init(&s_q_mutex, &attr);
    (void)pthread_mutex_init(&s_sub_mutex, &attr);
    (void)pthread_mutexattr_destroy(&attr);
}

/**
 * @brief  确保两把互斥量已完成初始化（幂等，可在任意入口调用）
 */
static void event_bus_mutexes_ready(void)
{
    (void)pthread_once(&s_mutex_once, event_bus_mutex_init_once);
}

/* -------------------------------------------------------------------------
 * 优先级路由
 * SAFETY 类、ESTOP、 IO 在线/离线事件走高优先级队列。
 * ------------------------------------------------------------------------- */
static bool event_is_high_priority(event_type_t type)
{
    if (event_type_category(type) == EVT_CAT_SAFETY) {
        return true;
    }
    return (type == EVT_HW_ESTOP_ON) || (type == EVT_HW_ESTOP_OFF) || (type == EVT_HW_IO_ONLINE)
           || (type == EVT_HW_IO_OFFLINE);
}

/* -------------------------------------------------------------------------
 * 订阅表 [category][local_id][slot]
 * ------------------------------------------------------------------------- */
static event_handler_t s_subs[EVT_CAT_MAX][EVT_PER_CAT_MAX][EVENT_BUS_MAX_SUBS_PER_EVT];

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
static sem_t s_sem;
static int   s_sem_initialized = 0;

/* -------------------------------------------------------------------------
 * 初始化状态
 * ------------------------------------------------------------------------- */
static volatile int s_initialized        = 0;
static volatile int s_shutdown_requested = 0;

/* -------------------------------------------------------------------------
 * 不可恢复故障回调
 * ------------------------------------------------------------------------- */
static event_bus_fatal_cb_t s_fatal_cb = NULL;

/* -------------------------------------------------------------------------
 * 运行统计
 * ------------------------------------------------------------------------- */
static event_bus_stats_t s_stats;
static uint64_t          s_next_event_id;

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
    event_bus_mutexes_ready();

    s_shutdown_requested = 0;

    /* 重置队列 */
    pthread_mutex_lock(&s_q_mutex);
    memset(s_queue, 0, sizeof(s_queue));
    memset(s_queue_hi, 0, sizeof(s_queue_hi));
    s_q_head     = 0;
    s_q_tail     = 0;
    s_q_count    = 0;
    s_q_hi_head  = 0;
    s_q_hi_tail  = 0;
    s_q_hi_count = 0;
    memset(&s_stats, 0, sizeof(s_stats));
    s_next_event_id = 1U;
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
    if (!s_sem_initialized) {
        if (sem_init(&s_sem, 0, 0) != 0) {
            EVT_LOG_ERROR("sem_init failed");
            return SW_ERR_HW;
        }
        s_sem_initialized = 1;
    } else {
        /* 排空残余信号量计数（dispatch 已停止，sem_trywait 不会阻塞）*/
        int val = 0;
        sem_getvalue(&s_sem, &val);
        for (int i = 0; i < val; i++) {
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
    if (!s_initialized) {
        return SW_ERR_NOT_INIT;
    }

    event_bus_mutexes_ready();

    s_shutdown_requested = 1;
    s_initialized        = 0;

    if (sem_post(&s_sem) != 0) {
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
    if (!s_initialized) {
        return SW_ERR_NOT_INIT;
    }
    if (!event_type_is_valid(type)) {
        return SW_ERR_PARAM;
    }

    event_bus_mutexes_ready();
    pthread_mutex_lock(&s_q_mutex);

    if (event_is_high_priority(type)) {
        /* 高优先级队列 */
        if (s_q_hi_count >= EVENT_BUS_HI_QUEUE_SIZE) {
            s_stats.dropped_count++;
            s_stats.dropped_by_cat[event_type_category(type)]++;
            pthread_mutex_unlock(&s_q_mutex);
            EVT_LOG_WARN("hi-queue full, drop EVT type=%d param=%u", (int)type, param);
            return SW_ERR_OVERFLOW;
        }

        event_t *slot      = &s_queue_hi[s_q_hi_tail];
        slot->type         = type;
        slot->param        = param;
        slot->timestamp_ms = time_util_get_ms();
        slot->event_id     = s_next_event_id++;
        slot->trace        = trace_context_get();

        s_q_hi_tail = (s_q_hi_tail + 1U) % EVENT_BUS_HI_QUEUE_SIZE;
        s_q_hi_count++;
        s_stats.published_count++;
        s_stats.published_by_cat[event_type_category(type)]++;
        s_stats.hi_queue_depth = s_q_hi_count;
        if (s_stats.hi_queue_peak_depth < s_q_hi_count) {
            s_stats.hi_queue_peak_depth = s_q_hi_count;
        }
    } else {
        /* 普通优先级队列 */
        if (s_q_count >= EVENT_BUS_QUEUE_SIZE) {
            s_stats.dropped_count++;
            s_stats.dropped_by_cat[event_type_category(type)]++;
            pthread_mutex_unlock(&s_q_mutex);
            EVT_LOG_WARN("queue full, drop EVT type=%d param=%u", (int)type, param);
            return SW_ERR_OVERFLOW;
        }

        event_t *slot      = &s_queue[s_q_tail];
        slot->type         = type;
        slot->param        = param;
        slot->timestamp_ms = time_util_get_ms();
        slot->event_id     = s_next_event_id++;
        slot->trace        = trace_context_get();

        s_q_tail = (s_q_tail + 1U) % EVENT_BUS_QUEUE_SIZE;
        s_q_count++;
        s_stats.published_count++;
        s_stats.published_by_cat[event_type_category(type)]++;
        s_stats.queue_depth = s_q_count;
        if (s_stats.queue_peak_depth < s_q_count) {
            s_stats.queue_peak_depth = s_q_count;
        }
    }

    if (sem_post(&s_sem) != 0) /* 通知 dispatch 线程 */
    {
        /* 回滚入队 */
        if (event_is_high_priority(type)) {
            s_q_hi_tail = (s_q_hi_tail == 0U) ? (EVENT_BUS_HI_QUEUE_SIZE - 1U) : (s_q_hi_tail - 1U);
            s_q_hi_count--;
            s_stats.hi_queue_depth = s_q_hi_count;
        } else {
            s_q_tail = (s_q_tail == 0U) ? (EVENT_BUS_QUEUE_SIZE - 1U) : (s_q_tail - 1U);
            s_q_count--;
            s_stats.queue_depth = s_q_count;
        }
        s_stats.published_count--;
        s_stats.published_by_cat[event_type_category(type)]--;
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

    if (!s_initialized) {
        return SW_ERR_NOT_INIT;
    }
    if (!event_type_is_valid(type) || handler == NULL) {
        return SW_ERR_PARAM;
    }

    event_bus_mutexes_ready();
    pthread_mutex_lock(&s_sub_mutex);

    {
        event_handler_t *slot = subs_slot(type);

        for (uint32_t i = 0; i < EVENT_BUS_MAX_SUBS_PER_EVT; i++) {
            if (slot[i] == handler) {
                /* 重复订阅：幂等处理，直接返回 OK（不重复添加）*/
                pthread_mutex_unlock(&s_sub_mutex);
                return SW_OK;
            }
            if (slot[i] == NULL) {
                slot[i]  = handler;
                inserted = 1;
                break;
            }
        }

        pthread_mutex_unlock(&s_sub_mutex);
    }

    if (!inserted) {
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

    if (subs == NULL) {
        return SW_ERR_PARAM;
    }

    for (size_t i = 0; i < count; i++) {
        ret = event_subscribe(subs[i].type, subs[i].handler);
        if (ret != SW_OK) {
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
    if (stats == NULL) {
        return SW_ERR_PARAM;
    }

    event_bus_mutexes_ready();
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
    event_t         dispatch_evt;
    event_handler_t handlers[EVENT_BUS_MAX_SUBS_PER_EVT];

    event_bus_mutexes_ready();

    while (1) {
        while (sem_wait(&s_sem) != 0) {
            if (errno == EINTR) {
                continue;
            }

            pthread_mutex_lock(&s_q_mutex);
            s_stats.sem_wait_fail_count++;
            pthread_mutex_unlock(&s_q_mutex);
            EVT_LOG_ERROR("sem_wait failed errno=%d, event bus fatal", errno);

            if (s_fatal_cb != NULL) {
                s_fatal_cb(EVENT_BUS_FATAL_SEM_WAIT, errno);
                /* fatal_cb 约定必须终止进程；若违反约定则兜底退出线程 */
            }
            return;
        }

        /* 出队：优先排空高优先级队列 */
        pthread_mutex_lock(&s_q_mutex);
        if ((s_q_hi_count == 0U) && (s_q_count == 0U)) {
            if (s_shutdown_requested) {
                pthread_mutex_unlock(&s_q_mutex);
                return;
            }

            /* 并发 publish 后 sem 计数可能超过实际队列数量，正常跳过 */
            pthread_mutex_unlock(&s_q_mutex);
            continue;
        }

        if (s_q_hi_count > 0U) {
            dispatch_evt = s_queue_hi[s_q_hi_head];
            s_q_hi_head  = (s_q_hi_head + 1U) % EVENT_BUS_HI_QUEUE_SIZE;
            s_q_hi_count--;
            s_stats.hi_queue_depth = s_q_hi_count;
        } else {
            dispatch_evt = s_queue[s_q_head];
            s_q_head     = (s_q_head + 1U) % EVENT_BUS_QUEUE_SIZE;
            s_q_count--;
            s_stats.queue_depth = s_q_count;
        }
        s_stats.dispatched_count++;
        s_stats.dispatched_by_cat[event_type_category(dispatch_evt.type)]++;
        pthread_mutex_unlock(&s_q_mutex);

        /* 复制 handler 快照（最小化持锁时间，避免 handler 内部 subscribe 死锁）*/
        pthread_mutex_lock(&s_sub_mutex);
        if (event_type_is_valid(dispatch_evt.type)) {
            memcpy(handlers, subs_slot(dispatch_evt.type), sizeof(handlers));
        } else {
            /* 防御：非法类型在 publish 侧已被拦截，理论不可达。
             * 若不清零，数组会保留上一轮事件的 handler 并被重复调用。 */
            memset(handlers, 0, sizeof(handlers));
        }
        pthread_mutex_unlock(&s_sub_mutex);

        /* 逐一回调，并统计耗时。
         * dispatch 线程串行执行全部 handler，单个慢 handler 会直接推高队列水位，
         * 因此需要能定位到具体事件类型。 */
        uint32_t dispatch_ms = 0U;

        for (uint32_t i = 0; i < EVENT_BUS_MAX_SUBS_PER_EVT; i++) {
            if (handlers[i] != NULL) {
                trace_context_t previous = trace_context_get();
                trace_context_t current  = dispatch_evt.trace;
                uint64_t        began;
                uint32_t        cost_ms;

                current.causation_id = dispatch_evt.event_id;
                trace_context_set(&current);
                began = time_util_get_ms();
                handlers[i](&dispatch_evt);
                cost_ms = time_elapsed_ms(began, time_util_get_ms());
                trace_context_set(&previous);

                dispatch_ms += cost_ms;

                pthread_mutex_lock(&s_q_mutex);
                if (s_stats.handler_max_ms < cost_ms) {
                    s_stats.handler_max_ms   = cost_ms;
                    s_stats.handler_max_type = dispatch_evt.type;
                }
                if (cost_ms >= EVENT_BUS_SLOW_HANDLER_MS) {
                    s_stats.slow_handler_count++;
                }
                pthread_mutex_unlock(&s_q_mutex);

                if (cost_ms >= EVENT_BUS_SLOW_HANDLER_MS) {
                    EVT_LOG_WARN("slow handler: EVT type=%d slot=%u cost=%ums",
                                 (int)dispatch_evt.type,
                                 (unsigned)i,
                                 (unsigned)cost_ms);
                }
            }
        }

        if (dispatch_ms > 0U) {
            pthread_mutex_lock(&s_q_mutex);
            if (s_stats.dispatch_max_ms < dispatch_ms) {
                s_stats.dispatch_max_ms = dispatch_ms;
            }
            pthread_mutex_unlock(&s_q_mutex);
        }
    }
}
