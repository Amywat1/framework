/**
 * @file    alarm_registry.c
 * @brief   报警注册表聚合根实现
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "domain/safety/alarm_registry/alarm_registry.h"

#include "common/event_types.h"
#include "common/log.h"
#include "common/time_util.h"
#include "domain/safety/model/safety_matrix.h"
#include "runtime/event_bus/event_bus.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static alarm_def_t      s_catalog[ALARM_CATALOG_MAX];
static unsigned         s_catalog_count;
static alarm_instance_t s_active[ALARM_ACTIVE_MAX];
static unsigned         s_active_count;
static uint32_t         s_session_journal[ALARM_SESSION_JOURNAL_MAX];
static unsigned         s_session_journal_count;
static uint32_t         s_session_journal_dropped;
static bool             s_session_active;
static safety_posture_t s_posture = SAFETY_POSTURE_NOMINAL;
static pthread_mutex_t  s_mutex   = PTHREAD_MUTEX_INITIALIZER;

typedef enum {
    PUBLISH_KIND_TRIGGERED = 0,
    PUBLISH_KIND_CLEARED,
} publish_kind_t;

typedef struct {
    publish_kind_t kind;
    uint32_t       code;
} publish_item_t;

typedef struct {
    publish_item_t items[ALARM_ACTIVE_MAX];
    unsigned       count;
    int            posture_edge; /**< 0 无边沿；1 LOCKOUT；-1 NOMINAL */
} publish_batch_t;

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

static void batch_add(publish_batch_t *batch, publish_kind_t kind, uint32_t code)
{
    if ((batch == NULL) || (batch->count >= ALARM_ACTIVE_MAX)) {
        return;
    }
    batch->items[batch->count].kind = kind;
    batch->items[batch->count].code = code;
    batch->count++;
}

static safety_posture_t posture_from_active_locked(void)
{
    unsigned i;

    for (i = 0U; i < s_active_count; ++i) {
        if (alarm_level_forces_lockout(s_active[i].level)) {
            return SAFETY_POSTURE_LOCKOUT;
        }
    }
    return SAFETY_POSTURE_NOMINAL;
}

/**
 * @brief  按活动表重算姿态；有边沿时记入 batch，调用方必须已持锁
 */
static void note_posture_edge_locked(publish_batch_t *batch)
{
    safety_posture_t next = posture_from_active_locked();

    if (next == s_posture) {
        return;
    }
    if (batch != NULL) {
        batch->posture_edge = (next == SAFETY_POSTURE_LOCKOUT) ? 1 : -1;
    }
    s_posture = next;
}

/**
 * @brief  放锁后发布本轮变位；调用时不得仍持 s_mutex
 */
static void publish_batch(const publish_batch_t *batch)
{
    unsigned i;

    if (batch == NULL) {
        return;
    }
    for (i = 0U; i < batch->count; ++i) {
        event_type_t type = (batch->items[i].kind == PUBLISH_KIND_TRIGGERED) ? EVT_ALARM_TRIGGERED : EVT_ALARM_CLEARED;

        (void)event_publish_required(type, batch->items[i].code);
    }
    if (batch->posture_edge > 0) {
        LOG_WARN("alarm_registry: posture -> LOCKOUT");
        (void)event_publish_required(EVT_SAFETY_LOCKOUT, 0U);
    } else if (batch->posture_edge < 0) {
        LOG_INFO("alarm_registry: posture -> NOMINAL");
        (void)event_publish_required(EVT_SAFETY_NOMINAL, 0U);
    }
}

/**
 * @brief  删除指定下标的活动告警并记入本轮 CLEARED
 * @note   调用方必须已持锁。删除会 memmove 前移后续条目，因此遍历活动表并可能
 *         删除时必须倒序扫描。
 */
static void clear_active_at_locked(unsigned idx, publish_batch_t *batch)
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

    batch_add(batch, PUBLISH_KIND_CLEARED, code);
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

static void append_active_slot_locked(const alarm_def_t *def, publish_batch_t *batch)
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

    batch_add(batch, PUBLISH_KIND_TRIGGERED, def->code);
    LOG_WARN("alarm_registry: TRIGGERED %06u (%s)", (unsigned)def->code, def->desc);
}

static sw_err_t insert_active_locked(const alarm_def_t *def, publish_batch_t *batch)
{
    if (s_active_count >= ALARM_ACTIVE_MAX) {
        if (alarm_level_forces_lockout(def->level)) {
            int victim = find_lockout_eviction_index_locked();

            if (victim >= 0) {
                uint32_t victim_code = s_active[(unsigned)victim].code;

                LOG_ERROR(
                    "alarm_registry: evict %06u to admit lockout %06u", (unsigned)victim_code, (unsigned)def->code);
                clear_active_at_locked((unsigned)victim, batch);
            } else {
                LOG_ERROR("alarm_registry: active pool full of lockout, reject %06u", (unsigned)def->code);
                return SW_ERR_OVERFLOW;
            }
        } else {
            LOG_ERROR("alarm_registry: active pool full, reject %06u level=%d", (unsigned)def->code, (int)def->level);
            return SW_ERR_OVERFLOW;
        }
    }

    append_active_slot_locked(def, batch);
    return SW_OK;
}

sw_err_t alarm_registry_trigger(uint32_t code)
{
    int             def_idx;
    sw_err_t        ret;
    publish_batch_t batch = {0};

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
    ret = insert_active_locked(&s_catalog[(unsigned)def_idx], &batch);
    if (ret == SW_OK) {
        note_posture_edge_locked(&batch);
    }
    pthread_mutex_unlock(&s_mutex);

    if (ret == SW_OK) {
        publish_batch(&batch);
    }
    return ret;
}

sw_err_t alarm_registry_clear(uint32_t code)
{
    int             active_idx;
    publish_batch_t batch = {0};

    pthread_mutex_lock(&s_mutex);

    active_idx = find_active_index(code);
    if (active_idx >= 0) {
        alarm_instance_t *inst = &s_active[(unsigned)active_idx];

        inst->condition_active = false;
        if (alarm_clear_is_auto(inst->clear)) {
            clear_active_at_locked((unsigned)active_idx, &batch);
            note_posture_edge_locked(&batch);
        }
        pthread_mutex_unlock(&s_mutex);
        publish_batch(&batch);
        return SW_OK;
    }

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
    unsigned        i;
    publish_batch_t batch = {0};

    pthread_mutex_lock(&s_mutex);
    for (i = s_active_count; i-- > 0U;) {
        const alarm_instance_t *inst = &s_active[i];

        if (alarm_clear_needs_motion_reeval(inst->clear) && (inst->reeval_group == group) && !inst->condition_active) {
            clear_active_at_locked(i, &batch);
        }
    }
    if (batch.count > 0U) {
        note_posture_edge_locked(&batch);
    }
    pthread_mutex_unlock(&s_mutex);
    publish_batch(&batch);
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
    unsigned        i;
    publish_batch_t batch = {0};

    pthread_mutex_lock(&s_mutex);
    for (i = s_active_count; i-- > 0U;) {
        const alarm_instance_t *inst = &s_active[i];

        if (alarm_clear_allows_manual_reset(inst->clear) && !inst->condition_active) {
            clear_active_at_locked(i, &batch);
        }
    }
    if (batch.count > 0U) {
        note_posture_edge_locked(&batch);
    }
    pthread_mutex_unlock(&s_mutex);
    publish_batch(&batch);
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

static void fill_safety_view_locked(alarm_safety_view_t *out)
{
    unsigned i;
    int      highest = -1;

    memset(out, 0, sizeof(*out));
    out->count    = s_active_count;
    out->top_code = ALARM_CODE_NONE;
    out->posture  = SAFETY_POSTURE_NOMINAL;

    if (out->count > 0U) {
        memcpy(out->list, s_active, out->count * sizeof(out->list[0]));
    }

    for (i = 0U; i < s_active_count; ++i) {
        if (alarm_level_blocks_wash(s_active[i].level)) {
            out->blocking = true;
        }
        if (alarm_level_forces_lockout(s_active[i].level)) {
            out->posture = SAFETY_POSTURE_LOCKOUT;
        }
        if ((int)s_active[i].level > highest) {
            highest       = (int)s_active[i].level;
            out->top_code = s_active[i].code;
        }
    }

    out->journal_count   = s_session_journal_count;
    out->journal_dropped = s_session_journal_dropped;
    if (out->journal_count > 0U) {
        memcpy(out->session_journal, s_session_journal, out->journal_count * sizeof(out->session_journal[0]));
    }
}

sw_err_t alarm_registry_copy_safety_view(alarm_safety_view_t *out)
{
    if (out == NULL) {
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_mutex);
    fill_safety_view_locked(out);
    pthread_mutex_unlock(&s_mutex);
    return SW_OK;
}

bool alarm_registry_has_blocking_active(void)
{
    alarm_safety_view_t view;

    pthread_mutex_lock(&s_mutex);
    fill_safety_view_locked(&view);
    pthread_mutex_unlock(&s_mutex);
    return view.blocking;
}

safety_posture_t alarm_registry_safety_posture(void)
{
    safety_posture_t posture;

    pthread_mutex_lock(&s_mutex);
    posture = s_posture;
    pthread_mutex_unlock(&s_mutex);
    return posture;
}

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
 * @note   调用方必须已持锁。不发布 CLEARED——init / load_catalog 是启动期复位。
 */
static void reset_runtime_state_locked(void)
{
    memset(s_active, 0, sizeof(s_active));
    s_active_count = 0U;
    memset(s_session_journal, 0, sizeof(s_session_journal));
    s_session_journal_count   = 0U;
    s_session_journal_dropped = 0U;
    s_session_active          = false;
    s_posture                 = SAFETY_POSTURE_NOMINAL;
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
    pthread_mutex_lock(&s_mutex);
    memset(s_catalog, 0, sizeof(s_catalog));
    s_catalog_count = 0U;
    reset_runtime_state_locked();
    pthread_mutex_unlock(&s_mutex);

    LOG_INFO("alarm_registry: init ok");
    return SW_OK;
}
