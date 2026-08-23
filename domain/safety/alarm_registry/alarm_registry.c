/**
 * @file    alarm_registry.c
 * @brief   报警注册表聚合根实现
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "domain/safety/alarm_registry/alarm_registry.h"

#include "common/log.h"
#include "common/time_util.h"
#include "domain/safety/model/safety_matrix.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static alarm_def_t          s_catalog[ALARM_CATALOG_MAX];
static unsigned             s_catalog_count;
static alarm_instance_t     s_active[ALARM_ACTIVE_MAX];
static unsigned             s_active_count;
static uint32_t             s_session_journal[ALARM_SESSION_JOURNAL_MAX];
static unsigned             s_session_journal_count;
static uint32_t             s_session_journal_dropped;
static bool                 s_session_active;
static alarm_domain_event_t s_pending[ALARM_PENDING_EVENT_MAX];
static unsigned             s_pending_count;
static uint32_t             s_pending_dropped;
static pthread_mutex_t      s_mutex = PTHREAD_MUTEX_INITIALIZER;

static int find_def_index(uint32_t code)
{
    for (unsigned i = 0; i < s_catalog_count; ++i) {
        if (s_catalog[i].code == code) {
            return (int)i;
        }
    }
    return -1;
}

static int find_active_index(uint32_t code)
{
    for (unsigned i = 0; i < s_active_count; ++i) {
        if (s_active[i].code == code) {
            return (int)i;
        }
    }
    return -1;
}

/**
 * @brief  入队一条待发域事件；队列满时计数丢弃
 * @note   清除路径没有可以返回错误的调用方（`reevaluate_group` / `reset_all`
 *         是批量操作），所以这里不能像活跃池那样硬失败。改为累计丢弃数，由
 *         `alarm_registry_pull_events` 与本批事件同锁交出，桥接层据此补发重同步。
 *         日志只在一段丢弃的首条打印，避免突发时刷屏；量级由计数表达。
 */
static void enqueue_event_locked(alarm_domain_event_kind_t kind, uint32_t code)
{
    if (s_pending_count >= ALARM_PENDING_EVENT_MAX) {
        if (s_pending_dropped == 0U) {
            LOG_WARN("alarm_registry: pending event queue full, dropping domain events");
        }
        if (s_pending_dropped < UINT32_MAX) {
            s_pending_dropped++;
        }
        return;
    }
    s_pending[s_pending_count].kind = kind;
    s_pending[s_pending_count].code = code;
    s_pending_count++;
}

/**
 * @brief  删除指定下标的活动告警并发出 CLEARED 域事件
 * @note   调用方必须已持锁。删除会 memmove 前移后续条目，因此遍历活动表并可能
 *         删除时必须倒序扫描。
 */
static void clear_active_at_locked(unsigned idx)
{
    uint32_t code;

    if (idx >= s_active_count) {
        return;
    }
    code = s_active[idx].code;
    if ((idx + 1U) < s_active_count) {
        memmove(&s_active[idx], &s_active[idx + 1U], (s_active_count - idx - 1U) * sizeof(s_active[0]));
    }
    s_active_count--;

    enqueue_event_locked(ALARM_DOMAIN_EVT_CLEARED, code);
    LOG_INFO("alarm_registry: CLEARED %06u", (unsigned)code);
}

static bool append_session_journal_locked(uint32_t code)
{
    unsigned i;

    if (!s_session_active) {
        return false;
    }
    for (i = 0; i < s_session_journal_count; ++i) {
        if (s_session_journal[i] == code) {
            return false;
        }
    }
    if (s_session_journal_count >= ALARM_SESSION_JOURNAL_MAX) {
        if (s_session_journal_dropped == 0U) {
            LOG_WARN("alarm_registry: session journal full, dropping codes");
        }
        if (s_session_journal_dropped < UINT32_MAX) {
            s_session_journal_dropped++;
        }
        return false;
    }
    s_session_journal[s_session_journal_count++] = code;
    return true;
}

/**
 * @brief  为 lockout 新条目挑选可驱逐的非 lockout 下标
 * @return 活动表下标；无可驱逐条目时为 -1
 * @note   调用方必须已持锁。先取不阻塞开洗（MINOR）中 triggered_at_ms 最早者，
 *         再取其余非 lockout（MAJOR）。时间戳相同则保留先扫到的（插入更早）。
 */
static int find_lockout_eviction_index_locked(void)
{
    int      minor_idx = -1;
    int      major_idx = -1;
    uint64_t minor_at  = UINT64_MAX;
    uint64_t major_at  = UINT64_MAX;
    unsigned i;

    for (i = 0U; i < s_active_count; ++i) {
        alarm_level_t level = s_active[i].level;
        uint64_t      at    = s_active[i].triggered_at_ms;

        if (alarm_level_forces_lockout(level)) {
            continue;
        }
        if (!alarm_level_blocks_wash(level)) {
            if ((minor_idx < 0) || (at < minor_at)) {
                minor_idx = (int)i;
                minor_at  = at;
            }
        } else if ((major_idx < 0) || (at < major_at)) {
            major_idx = (int)i;
            major_at  = at;
        }
    }

    return (minor_idx >= 0) ? minor_idx : major_idx;
}

static void append_active_slot_locked(const alarm_def_t *def)
{
    alarm_instance_t *inst = &s_active[s_active_count];

    inst->code             = def->code;
    inst->level            = def->level;
    inst->clear            = def->clear;
    inst->reeval_group     = def->reeval_group;
    inst->triggered_at_ms  = time_util_get_ms();
    inst->condition_active = true;
    s_active_count++;

    if (alarm_level_records_in_journal(def->level)) {
        (void)append_session_journal_locked(def->code);
    }

    enqueue_event_locked(ALARM_DOMAIN_EVT_TRIGGERED, def->code);
    LOG_WARN("alarm_registry: TRIGGERED %06u (%s)", (unsigned)def->code, def->desc);
}

static sw_err_t insert_active_locked(const alarm_def_t *def)
{
    if (s_active_count >= ALARM_ACTIVE_MAX) {
        if (alarm_level_forces_lockout(def->level)) {
            int victim = find_lockout_eviction_index_locked();

            if (victim >= 0) {
                uint32_t victim_code = s_active[(unsigned)victim].code;

                LOG_ERROR("alarm_registry: evict %06u to admit lockout %06u",
                          (unsigned)victim_code,
                          (unsigned)def->code);
                clear_active_at_locked((unsigned)victim);
            } else {
                LOG_ERROR("alarm_registry: active pool full of lockout, reject %06u", (unsigned)def->code);
                return SW_ERR_OVERFLOW;
            }
        } else {
            LOG_ERROR("alarm_registry: active pool full, reject %06u level=%d",
                      (unsigned)def->code,
                      (int)def->level);
            return SW_ERR_OVERFLOW;
        }
    }

    append_active_slot_locked(def);
    return SW_OK;
}

sw_err_t alarm_registry_trigger(uint32_t code)
{
    int      def_idx;
    sw_err_t ret;

    /* catalog 查找必须与 load_catalog 用同一把锁：后者持锁改写 s_catalog
     * 与 s_catalog_count，锁外查找会读到不一致的中间态。 */
    pthread_mutex_lock(&s_mutex);

    def_idx = find_def_index(code);
    if (def_idx < 0) {
        pthread_mutex_unlock(&s_mutex);
        LOG_WARN("alarm_registry: trigger unknown code=%06u", (unsigned)code);
        return SW_ERR_PARAM;
    }

    {
        int active_idx = find_active_index(code);

        if (active_idx >= 0) {
            s_active[(unsigned)active_idx].condition_active = true;
            pthread_mutex_unlock(&s_mutex);
            return SW_OK;
        }
    }
    ret = insert_active_locked(&s_catalog[(unsigned)def_idx]);
    pthread_mutex_unlock(&s_mutex);
    return ret;
}

sw_err_t alarm_registry_clear(uint32_t code)
{
    int active_idx;

    pthread_mutex_lock(&s_mutex);

    /* 活动实例已缓存清除策略，命中时不必回查目录 */
    active_idx = find_active_index(code);
    if (active_idx >= 0) {
        alarm_instance_t *inst = &s_active[(unsigned)active_idx];

        inst->condition_active = false;
        if (alarm_clear_is_auto(inst->clear)) {
            clear_active_at_locked((unsigned)active_idx);
        }
        pthread_mutex_unlock(&s_mutex);
        return SW_OK;
    }

    /* 只有未活动时才需要查目录：区分「条件消失但本就没报」与「上报了未定义码」 */
    if (find_def_index(code) < 0) {
        pthread_mutex_unlock(&s_mutex);
        LOG_WARN("alarm_registry: clear unknown code=%06u", (unsigned)code);
        return SW_ERR_PARAM;
    }

    pthread_mutex_unlock(&s_mutex);
    return SW_OK;
}

sw_err_t alarm_registry_reevaluate_group(motion_reeval_group_id_t group)
{
    unsigned i;

    /* 只落地“动作中已判定条件消失”的条目；条件仍成立则保持报警。
     * 运动结束本身不是清除理由，避免停机误清后被监测源立刻再拉起。
     * 倒序扫描：clear_active_at_locked 会前移后续条目。 */
    pthread_mutex_lock(&s_mutex);
    for (i = s_active_count; i-- > 0U;) {
        const alarm_instance_t *inst = &s_active[i];

        if (alarm_clear_needs_motion_reeval(inst->clear) && (inst->reeval_group == group)
            && !inst->condition_active) {
            clear_active_at_locked(i);
        }
    }
    pthread_mutex_unlock(&s_mutex);
    return SW_OK;
}

void alarm_registry_on_wash_session_started(void)
{
    pthread_mutex_lock(&s_mutex);
    s_session_active          = true;
    s_session_journal_count   = 0U;
    s_session_journal_dropped = 0U;
    pthread_mutex_unlock(&s_mutex);
}

void alarm_registry_on_wash_session_ended(void)
{
    pthread_mutex_lock(&s_mutex);
    s_session_active = false;
    pthread_mutex_unlock(&s_mutex);
}

void alarm_registry_reset_all(void)
{
    unsigned i;

    /* 倒序扫描：clear_active_at_locked 会前移后续条目 */
    pthread_mutex_lock(&s_mutex);
    for (i = s_active_count; i-- > 0U;) {
        const alarm_instance_t *inst = &s_active[i];

        if (alarm_clear_allows_manual_reset(inst->clear) && !inst->condition_active) {
            clear_active_at_locked(i);
        }
    }
    pthread_mutex_unlock(&s_mutex);
}

bool alarm_registry_is_active(uint32_t code)
{
    bool active;

    pthread_mutex_lock(&s_mutex);
    active = (find_active_index(code) >= 0);
    pthread_mutex_unlock(&s_mutex);
    return active;
}

unsigned alarm_registry_get_session_journal(uint32_t *buf, unsigned max, uint32_t *dropped_out)
{
    unsigned copied = 0U;

    pthread_mutex_lock(&s_mutex);
    if (dropped_out != NULL) {
        *dropped_out = s_session_journal_dropped;
    }
    if ((buf != NULL) && (max > 0U)) {
        copied = s_session_journal_count;
        if (copied > max) {
            copied = max;
        }
        memcpy(buf, s_session_journal, copied * sizeof(buf[0]));
    }
    pthread_mutex_unlock(&s_mutex);
    return copied;
}

/**
 * @brief  在已持锁前提下汇总安全投影，供三个公开读接口共用
 * @note   调用方必须已持有 s_mutex。
 */
static unsigned copy_safety_view_locked(alarm_instance_t *list,
                                        unsigned          list_max,
                                        bool             *blocking_out,
                                        uint32_t         *top_out,
                                        safety_posture_t *posture_out)
{
    unsigned         copied = s_active_count;
    unsigned         i;
    int              highest  = -1;
    uint32_t         top      = ALARM_CODE_NONE;
    bool             blocking = false;
    safety_posture_t posture  = SAFETY_POSTURE_NOMINAL;

    if (list_max == 0U) {
        list = NULL;
    }
    if ((list != NULL) && (copied > list_max)) {
        copied = list_max;
    }
    if ((list != NULL) && (copied > 0U)) {
        memcpy(list, s_active, copied * sizeof(list[0]));
    }

    for (i = 0U; i < s_active_count; ++i) {
        if (alarm_level_blocks_wash(s_active[i].level)) {
            blocking = true;
        }
        if (alarm_level_forces_lockout(s_active[i].level)) {
            posture = SAFETY_POSTURE_LOCKOUT;
        }
        if ((int)s_active[i].level > highest) {
            highest = (int)s_active[i].level;
            top     = s_active[i].code;
        }
    }

    if (blocking_out != NULL) {
        *blocking_out = blocking;
    }
    if (top_out != NULL) {
        *top_out = top;
    }
    if (posture_out != NULL) {
        *posture_out = posture;
    }
    return copied;
}

unsigned alarm_registry_copy_safety_view(alarm_instance_t *list,
                                         unsigned          list_max,
                                         bool             *blocking_out,
                                         uint32_t         *top_out,
                                         safety_posture_t *posture_out)
{
    unsigned copied;

    pthread_mutex_lock(&s_mutex);
    copied = copy_safety_view_locked(list, list_max, blocking_out, top_out, posture_out);
    pthread_mutex_unlock(&s_mutex);
    return copied;
}

/* 下面两个聚合查询都走 copy_safety_view_locked，不再各自重写一遍等级扫描：
 * 「MAJOR 阻塞开洗」「CRITICAL 进 LOCKOUT」这两句判定在本文件里只出现一次。 */
bool alarm_registry_has_blocking_active(void)
{
    bool blocking = false;

    pthread_mutex_lock(&s_mutex);
    (void)copy_safety_view_locked(NULL, 0U, &blocking, NULL, NULL);
    pthread_mutex_unlock(&s_mutex);
    return blocking;
}

safety_posture_t alarm_registry_safety_posture(void)
{
    safety_posture_t posture = SAFETY_POSTURE_NOMINAL;

    pthread_mutex_lock(&s_mutex);
    (void)copy_safety_view_locked(NULL, 0U, NULL, NULL, &posture);
    pthread_mutex_unlock(&s_mutex);
    return posture;
}

unsigned alarm_registry_pull_events(alarm_domain_event_t *buf, unsigned max, uint32_t *dropped_out)
{
    unsigned pulled = 0U;

    if (dropped_out != NULL) {
        *dropped_out = 0U;
    }
    if ((buf == NULL) || (max == 0U)) {
        return 0U;
    }

    pthread_mutex_lock(&s_mutex);
    pulled = s_pending_count;
    if (pulled > max) {
        pulled = max;
    }
    memcpy(buf, s_pending, pulled * sizeof(buf[0]));
    if (pulled > 0U) {
        memmove(s_pending, &s_pending[pulled], (s_pending_count - pulled) * sizeof(s_pending[0]));
        s_pending_count -= pulled;
    }
    /* 丢弃计数与本批事件同锁取出并清零：否则「取计数」与「取事件」之间新产生的
     * 丢弃会被下一次调用当成已上报而清掉。 */
    if (dropped_out != NULL) {
        *dropped_out      = s_pending_dropped;
        s_pending_dropped = 0U;
    }
    pthread_mutex_unlock(&s_mutex);
    return pulled;
}

/**
 * @brief  校验待装载的报警目录
 * @return true 表示全部条目合法
 *
 * @note   目录是项目手写资产，下面四类错误在运行期都不会报错，只会表现为
 *         「报警行为和配置对不上」，且没有任何日志线索：
 *           - 非法码：绕开 6 位编码约定，按大类/性质分流的消费者全部失准；
 *           - 重复码：`find_def_index` 永远命中第一条，后一条的等级与清除策略
 *             静默失效——配了 CRITICAL 却不停机就是这么来的；
 *           - 未定义等级/策略：落进行为矩阵的最严格兜底分支，现场看是莫名停机，
 *             而那个兜底本意是纵深防御，不该成为常态入口；
 *           - ON_MOTION 与 reeval_group 不配对：前者没有分组则永远进不了任何
 *             重评估批次，后者填了分组却不参与重评估，两种都是配错了还照跑。
 *         全部在装载期拒绝，把问题挡在启动而不是现场。
 */
static bool catalog_defs_valid(const alarm_def_t *defs, unsigned count)
{
    unsigned i;
    unsigned j;

    for (i = 0U; i < count; ++i) {
        const alarm_def_t *def         = &defs[i];
        bool               needs_group = alarm_clear_needs_motion_reeval(def->clear);
        bool               has_group   = (def->reeval_group != ALARM_REEVAL_GROUP_NONE);

        if (!alarm_code_is_valid(def->code)) {
            LOG_ERROR("alarm_registry: catalog[%u] invalid code=%u", i, (unsigned)def->code);
            return false;
        }
        if (!alarm_level_is_defined(def->level) || !alarm_clear_is_defined(def->clear)) {
            LOG_ERROR("alarm_registry: catalog[%u] code=%06u undefined level=%d clear=%d",
                      i,
                      (unsigned)def->code,
                      (int)def->level,
                      (int)def->clear);
            return false;
        }
        if (needs_group != has_group) {
            LOG_ERROR("alarm_registry: catalog[%u] code=%06u clear=%d mismatches reeval_group=%u",
                      i,
                      (unsigned)def->code,
                      (int)def->clear,
                      (unsigned)def->reeval_group);
            return false;
        }
        for (j = 0U; j < i; ++j) {
            if (defs[j].code == def->code) {
                LOG_ERROR("alarm_registry: catalog[%u] duplicate code=%06u first at [%u]", i, (unsigned)def->code, j);
                return false;
            }
        }
    }
    return true;
}

/**
 * @brief  清空全部运行期状态（不含目录本身）
 * @note   调用方必须已持锁。`init` 与 `load_catalog` 共用，确保两条复位路径
 *         覆盖范围一致——此前 load_catalog 只清活动表与 pending，会话日志里
 *         留着新目录已不存在的码。
 */
static void reset_runtime_state_locked(void)
{
    memset(s_active, 0, sizeof(s_active));
    s_active_count = 0U;
    memset(s_session_journal, 0, sizeof(s_session_journal));
    s_session_journal_count   = 0U;
    s_session_journal_dropped = 0U;
    s_session_active          = false;
    memset(s_pending, 0, sizeof(s_pending));
    s_pending_count   = 0U;
    s_pending_dropped = 0U;
}

sw_err_t alarm_registry_load_catalog(const alarm_def_t *defs, unsigned count)
{
    if (defs == NULL) {
        return SW_ERR_PARAM;
    }
    if (count > ALARM_CATALOG_MAX) {
        LOG_ERROR("alarm_registry: catalog too large count=%u max=%u", count, (unsigned)ALARM_CATALOG_MAX);
        return SW_ERR_OVERFLOW;
    }
    if (!catalog_defs_valid(defs, count)) {
        return SW_ERR_PARAM;
    }

    /* 目录换了，运行期状态就没有一项还对应得上：活动实例缓存的是旧定义，
     * 会话日志里可能是新目录不存在的码。一并清空，不做半清。 */
    pthread_mutex_lock(&s_mutex);
    memcpy(s_catalog, defs, count * sizeof(s_catalog[0]));
    s_catalog_count = count;
    reset_runtime_state_locked();
    pthread_mutex_unlock(&s_mutex);

    LOG_INFO("alarm_registry: catalog loaded defs=%u", count);
    return SW_OK;
}

unsigned alarm_registry_catalog_count(void)
{
    unsigned count;

    pthread_mutex_lock(&s_mutex);
    count = s_catalog_count;
    pthread_mutex_unlock(&s_mutex);
    return count;
}

sw_err_t alarm_registry_init(void)
{
    /* 清空全部运行期状态。bootstrap 在 bind 阶段调用本函数；入站端口由
     * application/bridges/alarm_binding_bridge 在其后注册，再执行
     * project_bind_alarm_catalog，故此处清空目录不会丢掉已加载的资产。 */
    pthread_mutex_lock(&s_mutex);
    memset(s_catalog, 0, sizeof(s_catalog));
    s_catalog_count = 0U;
    reset_runtime_state_locked();
    pthread_mutex_unlock(&s_mutex);

    LOG_INFO("alarm_registry: init ok");
    return SW_OK;
}
