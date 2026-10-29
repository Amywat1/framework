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
#include <stdio.h>
#include <string.h>

#define ALARM_PUBLISH_BATCH_MAX (ALARM_ACTIVE_MAX * 3U)
#define ALARM_OBJECT_RUN_MAX    ALARM_CATALOG_MAX

static alarm_def_t      s_catalog[ALARM_CATALOG_MAX];
static unsigned         s_catalog_count;
static alarm_instance_t s_active[ALARM_ACTIVE_MAX];
static unsigned         s_active_count;
static uint32_t         s_session_journal[ALARM_SESSION_JOURNAL_MAX];
static unsigned         s_session_journal_count;
static uint32_t         s_session_journal_dropped;
static bool             s_session_active;
static safety_posture_t s_posture = SAFETY_POSTURE_NOMINAL;
static alarm_op_mode_t  s_op_mode = ALARM_OP_STOPPED;
static pthread_mutex_t  s_mutex   = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    uint32_t             key;
    alarm_object_run_t   run;
    bool                 used;
} object_run_slot_t;

static object_run_slot_t s_object_run[ALARM_OBJECT_RUN_MAX];
static bool              s_oos[ALARM_CATALOG_MAX];

typedef enum {
    PUBLISH_KIND_TRIGGERED = 0,
    PUBLISH_KIND_CLEARED,
    PUBLISH_KIND_ANNOUNCE,
    PUBLISH_KIND_MUTE,
} publish_kind_t;

typedef struct {
    publish_kind_t kind;
    uint32_t       code;
} publish_item_t;

typedef struct {
    publish_item_t items[ALARM_PUBLISH_BATCH_MAX];
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
    if ((batch == NULL) || (batch->count >= ALARM_PUBLISH_BATCH_MAX)) {
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

static event_type_t publish_kind_event(publish_kind_t kind)
{
    switch (kind) {
    case PUBLISH_KIND_TRIGGERED:
        return EVT_ALARM_TRIGGERED;
    case PUBLISH_KIND_CLEARED:
        return EVT_ALARM_CLEARED;
    case PUBLISH_KIND_ANNOUNCE:
        return EVT_ALARM_ANNOUNCE;
    case PUBLISH_KIND_MUTE:
        return EVT_ALARM_MUTE;
    default:
        return EVT_ALARM_TRIGGERED;
    }
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
        (void)event_publish_required(publish_kind_event(batch->items[i].kind), batch->items[i].code);
    }
    if (batch->posture_edge > 0) {
        LOG_WARN("alarm_registry: posture -> LOCKOUT");
        (void)event_publish_required(EVT_SAFETY_LOCKOUT, 0U);
    } else if (batch->posture_edge < 0) {
        LOG_INFO("alarm_registry: posture -> NOMINAL");
        (void)event_publish_required(EVT_SAFETY_NOMINAL, 0U);
    }
}

static alarm_object_run_t lookup_run_locked(uint32_t key)
{
    unsigned i;

    for (i = 0U; i < ALARM_OBJECT_RUN_MAX; ++i) {
        if (s_object_run[i].used && (s_object_run[i].key == key)) {
            return s_object_run[i].run;
        }
    }
    return ALARM_OBJECT_RUN_IDLE;
}

static bool object_has_active_root_locked(uint32_t key, uint32_t except_code)
{
    unsigned i;

    for (i = 0U; i < s_active_count; ++i) {
        const alarm_instance_t *inst = &s_active[i];
        int                     def_idx;

        if ((inst->code == except_code) || !inst->condition_active) {
            continue;
        }
        if (alarm_code_object_key(inst->code) != key) {
            continue;
        }
        def_idx = find_def_index(inst->code);
        if (def_idx < 0) {
            continue;
        }
        if (alarm_def_eclipse_role(&s_catalog[(unsigned)def_idx]) == ALARM_ECLIPSE_ROOT) {
            return true;
        }
    }
    return false;
}

static bool safety_trigger_active_locked(void)
{
    unsigned i;

    for (i = 0U; i < s_active_count; ++i) {
        if ((ALARM_CODE_FAULT_MODE(s_active[i].code) == ALM_N_SAFETY_TRIGGER) && s_active[i].condition_active) {
            return true;
        }
    }
    return false;
}

static bool cutout_eclipse_locked(void)
{
    return (posture_from_active_locked() == SAFETY_POSTURE_LOCKOUT) || safety_trigger_active_locked();
}

static bool catalog_index_oos(unsigned idx)
{
    return (idx < s_catalog_count) && s_oos[idx];
}

static void decide_announce_locked(const alarm_instance_t *inst,
                                   const alarm_def_t      *def,
                                   bool                   *announced,
                                   alarm_mute_reason_t    *reason)
{
    uint32_t             fault_mode;
    uint32_t             key;
    alarm_object_run_t   run;
    alarm_eclipse_role_t role;
    bool                 admit_mode;
    bool                 safety_class;

    *announced = true;
    *reason    = ALARM_MUTE_NONE;
    if ((inst == NULL) || (def == NULL)) {
        return;
    }

    fault_mode   = ALARM_CODE_FAULT_MODE(inst->code);
    key          = alarm_code_object_key(inst->code);
    run          = lookup_run_locked(key);
    role         = alarm_def_eclipse_role(def);
    admit_mode   = (s_op_mode == ALARM_OP_STANDBY) || (s_op_mode == ALARM_OP_ACTIVE)
                 || (s_op_mode == ALARM_OP_HOLD);
    safety_class = (fault_mode == ALM_N_SAFETY_TRIGGER) || alarm_level_forces_lockout(inst->level)
                   || (fault_mode == ALM_N_OVERLOAD);

    if (s_op_mode == ALARM_OP_INIT) {
        if (!alarm_level_forces_lockout(inst->level)) {
            *announced = false;
            *reason    = ALARM_MUTE_MODE;
            return;
        }
    } else if (!admit_mode) {
        if (((def->suppress_modes & ALARM_SUPPRESS_MODE_BIT(s_op_mode)) != 0U) && !safety_class) {
            *announced = false;
            *reason    = ALARM_MUTE_MODE;
            return;
        }
    }

    if ((def->need_run == ALARM_NEED_RUN_COMMANDED) && (run == ALARM_OBJECT_RUN_IDLE)) {
        *announced = false;
        *reason    = ALARM_MUTE_RUN;
        return;
    }

    if (role == ALARM_ECLIPSE_CONSEQUENTIAL) {
        if (object_has_active_root_locked(key, inst->code) || (run == ALARM_OBJECT_RUN_TRIPPED)) {
            *announced = false;
            *reason    = ALARM_MUTE_ECLIPSE;
            return;
        }
        if (cutout_eclipse_locked() && (fault_mode != ALM_N_SAFETY_TRIGGER) && (fault_mode != ALM_N_OVERLOAD)) {
            *announced = false;
            *reason    = ALARM_MUTE_ECLIPSE;
            return;
        }
    }

    if (inst->shelved_until_ms != 0U) {
        *announced = false;
        *reason    = ALARM_MUTE_SHELVED;
        return;
    }
}

static void reeval_announce_locked(publish_batch_t *batch)
{
    unsigned i;

    for (i = 0U; i < s_active_count; ++i) {
        alarm_instance_t   *inst = &s_active[i];
        int                 def_idx;
        bool                next_announced;
        alarm_mute_reason_t next_reason;

        def_idx = find_def_index(inst->code);
        if (def_idx < 0) {
            continue;
        }
        decide_announce_locked(inst, &s_catalog[(unsigned)def_idx], &next_announced, &next_reason);
        if (next_announced == inst->announced) {
            inst->mute_reason = next_announced ? ALARM_MUTE_NONE : next_reason;
            continue;
        }
        inst->announced   = next_announced;
        inst->mute_reason = next_announced ? ALARM_MUTE_NONE : next_reason;
        batch_add(batch, next_announced ? PUBLISH_KIND_ANNOUNCE : PUBLISH_KIND_MUTE, inst->code);
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
    bool     was_announced;

    if (idx >= s_active_count) {
        return;
    }
    code          = s_active[idx].code;
    was_announced = s_active[idx].announced;
    if ((idx + 1U) < s_active_count) {
        memmove(&s_active[idx], &s_active[idx + 1U], (s_active_count - idx - 1U) * sizeof(s_active[0]));
    }
    s_active_count--;

    if (was_announced) {
        batch_add(batch, PUBLISH_KIND_MUTE, code);
    }
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
    inst->lifecycle        = def->lifecycle;
    inst->reeval_group     = def->reeval_group;
    inst->triggered_at_ms  = time_util_get_ms();
    inst->condition_active = true;
    inst->announced        = false;
    inst->mute_reason      = ALARM_MUTE_NONE;
    inst->shelved_until_ms = 0U;
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

static void apply_def_defaults(alarm_def_t *def)
{
    uint32_t fault_mode;

    if (def == NULL) {
        return;
    }
    fault_mode = ALARM_CODE_FAULT_MODE(def->code);
    if ((def->need_run == ALARM_NEED_RUN_ANY)
        && ((fault_mode == ALM_N_ACTION_TIMEOUT) || (fault_mode == ALM_N_UNDERLOAD))) {
        def->need_run = ALARM_NEED_RUN_COMMANDED;
    }
}

static bool catalog_entry_valid(const alarm_def_t *def, unsigned i)
{
    bool has_group;

    if (def == NULL) {
        return false;
    }
    has_group = (def->reeval_group != ALARM_REEVAL_GROUP_NONE);
    if (!alarm_code_is_valid(def->code)) {
        LOG_ERROR("alarm_registry: catalog[%u] invalid code=%u", i, (unsigned)def->code);
        return false;
    }
    if (!alarm_level_is_defined(def->level) || !alarm_lifecycle_is_defined(def->lifecycle)) {
        LOG_ERROR("alarm_registry: catalog[%u] code=%06u undefined level=%d lifecycle=%d",
                  i,
                  (unsigned)def->code,
                  (int)def->level,
                  (int)def->lifecycle);
        return false;
    }
    if (alarm_lifecycle_follows_signal(def->lifecycle) && has_group) {
        LOG_ERROR("alarm_registry: catalog[%u] code=%06u follow_signal cannot have reeval_group=%u",
                  i,
                  (unsigned)def->code,
                  (unsigned)def->reeval_group);
        return false;
    }
    if ((def->need_run == ALARM_NEED_RUN_COMMANDED)
        && alarm_need_run_commanded_forbidden(ALARM_CODE_FAULT_MODE(def->code))) {
        LOG_ERROR("alarm_registry: catalog[%u] code=%06u need_run=commanded forbidden for fault_mode",
                  i,
                  (unsigned)def->code);
        return false;
    }
    if (alarm_suppress_modes_touch_admit(def->suppress_modes)) {
        LOG_ERROR("alarm_registry: catalog[%u] code=%06u suppress_modes cannot cover admit states",
                  i,
                  (unsigned)def->code);
        return false;
    }
    if (!alarm_def_oos_policy_legal(def)) {
        LOG_ERROR("alarm_registry: catalog[%u] code=%06u allow_oos illegal", i, (unsigned)def->code);
        return false;
    }
    return true;
}

static bool catalog_defs_valid(const alarm_def_t *defs, unsigned count)
{
    unsigned i;
    unsigned j;

    for (i = 0U; i < count; ++i) {
        if (!catalog_entry_valid(&defs[i], i)) {
            return false;
        }
        for (j = 0U; j < i; ++j) {
            if (defs[j].code == defs[i].code) {
                LOG_ERROR("alarm_registry: catalog[%u] duplicate code=%06u first at [%u]", i, (unsigned)defs[i].code, j);
                return false;
            }
        }
    }
    return true;
}

static bool code_in_defs(const alarm_def_t *defs, unsigned count, uint32_t code)
{
    unsigned i;

    for (i = 0U; i < count; ++i) {
        if (defs[i].code == code) {
            return true;
        }
    }
    return false;
}

uint32_t alarm_registry_oos_compensating_code(uint32_t original_code)
{
    return ALARM_CODE_MAKE(ALARM_CODE_LAYER(original_code), ALARM_CODE_OBJECT(original_code), ALM_N_OUT_OF_SERVICE);
}

static void fill_oos_compensating(const alarm_def_t *src, alarm_def_t *dst)
{
    memset(dst, 0, sizeof(*dst));
    dst->code         = alarm_registry_oos_compensating_code(src->code);
    dst->level        = (src->oos_admit == ALARM_OOS_ADMIT_RUN) ? ALARM_LEVEL_MINOR : ALARM_LEVEL_MAJOR;
    dst->lifecycle    = ALARM_LIFECYCLE_FOLLOW_SIGNAL;
    dst->reeval_group = ALARM_REEVAL_GROUP_NONE;
    (void)snprintf(dst->desc, sizeof(dst->desc), "检测停用");
    dst->need_run      = ALARM_NEED_RUN_ANY;
    dst->eclipse_role  = ALARM_ECLIPSE_ROOT;
    dst->shelve_policy = ALARM_SHELVE_FORBID;
    dst->allow_oos     = false;
}

static sw_err_t insert_compensating_locked(uint32_t original_code, publish_batch_t *batch)
{
    uint32_t comp = alarm_registry_oos_compensating_code(original_code);
    int      def_idx;
    int      active_idx;

    def_idx = find_def_index(comp);
    if (def_idx < 0) {
        return SW_ERR_STATE;
    }
    active_idx = find_active_index(comp);
    if (active_idx >= 0) {
        s_active[(unsigned)active_idx].condition_active = true;
        return SW_OK;
    }
    return insert_active_locked(&s_catalog[(unsigned)def_idx], batch);
}

static void drop_code_locked(uint32_t code, publish_batch_t *batch)
{
    int idx = find_active_index(code);

    if (idx >= 0) {
        clear_active_at_locked((unsigned)idx, batch);
    }
}

sw_err_t alarm_registry_trigger(uint32_t code)
{
    int             def_idx;
    sw_err_t        ret   = SW_OK;
    publish_batch_t batch = {0};

    pthread_mutex_lock(&s_mutex);

    def_idx = find_def_index(code);
    if (def_idx < 0) {
        pthread_mutex_unlock(&s_mutex);
        LOG_WARN("alarm_registry: trigger unknown code=%06u", (unsigned)code);
        return SW_ERR_PARAM;
    }

    if (catalog_index_oos((unsigned)def_idx)) {
        ret = insert_compensating_locked(code, &batch);
        if (ret == SW_OK) {
            note_posture_edge_locked(&batch);
            reeval_announce_locked(&batch);
        }
        pthread_mutex_unlock(&s_mutex);
        if (ret == SW_OK) {
            publish_batch(&batch);
        }
        return ret;
    }

    {
        int               active_idx = find_active_index(code);
        alarm_instance_t *inst;
        bool              reraise;

        if (active_idx >= 0) {
            inst    = &s_active[(unsigned)active_idx];
            reraise = !inst->condition_active;
            inst->condition_active = true;
            if (reraise) {
                inst->shelved_until_ms = 0U;
                reeval_announce_locked(&batch);
            }
            pthread_mutex_unlock(&s_mutex);
            publish_batch(&batch);
            return SW_OK;
        }
    }
    ret = insert_active_locked(&s_catalog[(unsigned)def_idx], &batch);
    if (ret == SW_OK) {
        note_posture_edge_locked(&batch);
        reeval_announce_locked(&batch);
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
        if (alarm_lifecycle_follows_signal(inst->lifecycle)) {
            clear_active_at_locked((unsigned)active_idx, &batch);
            note_posture_edge_locked(&batch);
        }
        reeval_announce_locked(&batch);
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

        if ((inst->reeval_group != ALARM_REEVAL_GROUP_NONE) && (inst->reeval_group == group)
            && !inst->condition_active) {
            clear_active_at_locked(i, &batch);
        }
    }
    if (batch.count > 0U) {
        note_posture_edge_locked(&batch);
    }
    reeval_announce_locked(&batch);
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

/**
 * @brief  码尾是否为安全触发
 * @param  code 报警码
 * @return 是安全触发为 true
 */
static bool alarm_code_is_safety_trigger(uint32_t code)
{
    return alarm_fault_mode_blocks_recover(ALARM_CODE_FAULT_MODE(code));
}

/**
 * @brief  倒序删除满足条件的活动报警并发布
 * @param  should_clear 为 true 时删除该条
 */
static void reset_matching(bool (*should_clear)(const alarm_instance_t *inst))
{
    unsigned        i;
    publish_batch_t batch = {0};

    pthread_mutex_lock(&s_mutex);
    for (i = s_active_count; i-- > 0U;) {
        if (should_clear(&s_active[i])) {
            clear_active_at_locked(i, &batch);
        }
    }
    if (batch.count > 0U) {
        note_posture_edge_locked(&batch);
    }
    reeval_announce_locked(&batch);
    pthread_mutex_unlock(&s_mutex);
    publish_batch(&batch);
}

/**
 * @brief  reset_all：条件已消失的可人工复位条目
 * @param  inst 活动条目
 * @return 应删除为 true
 */
static bool reset_all_match(const alarm_instance_t *inst)
{
    return alarm_lifecycle_allows_manual_reset(inst->lifecycle) && !inst->condition_active;
}

/**
 * @brief  reset_non_safety：非安全触发一律删除（形态 11 补偿码除外）
 * @param  inst 活动条目
 * @return 应删除为 true
 */
static bool reset_non_safety_match(const alarm_instance_t *inst)
{
    return !alarm_code_is_safety_trigger(inst->code)
           && (ALARM_CODE_FAULT_MODE(inst->code) != ALM_N_OUT_OF_SERVICE);
}

void alarm_registry_reset_all(void)
{
    reset_matching(reset_all_match);
}

void alarm_registry_reset_non_safety(void)
{
    reset_matching(reset_non_safety_match);
}

bool alarm_registry_has_safety_trigger_active(void)
{
    unsigned i;
    bool     active = false;

    pthread_mutex_lock(&s_mutex);
    for (i = 0U; i < s_active_count; ++i) {
        if (alarm_code_is_safety_trigger(s_active[i].code) && s_active[i].condition_active) {
            active = true;
            break;
        }
    }
    pthread_mutex_unlock(&s_mutex);
    return active;
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

    for (i = 0U; (i < s_catalog_count) && (out->oos_count < ALARM_OOS_MAX); ++i) {
        if (s_oos[i] && (ALARM_CODE_FAULT_MODE(s_catalog[i].code) != ALM_N_OUT_OF_SERVICE)) {
            out->oos_list[out->oos_count++] = s_catalog[i].code;
        }
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
    memset(s_object_run, 0, sizeof(s_object_run));
    memset(s_oos, 0, sizeof(s_oos));
}

sw_err_t alarm_registry_load_catalog(const alarm_def_t *defs, unsigned count)
{
    alarm_def_t built[ALARM_CATALOG_MAX];
    unsigned    built_count = 0U;
    unsigned    i;

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

    memcpy(built, defs, count * sizeof(built[0]));
    built_count = count;
    for (i = 0U; i < built_count; ++i) {
        apply_def_defaults(&built[i]);
        if ((built[i].need_run == ALARM_NEED_RUN_COMMANDED)
            && alarm_need_run_commanded_forbidden(ALARM_CODE_FAULT_MODE(built[i].code))) {
            LOG_ERROR("alarm_registry: catalog code=%06u need_run default illegal", (unsigned)built[i].code);
            return SW_ERR_PARAM;
        }
    }

    for (i = 0U; i < count; ++i) {
        alarm_def_t comp;

        if (!built[i].allow_oos) {
            continue;
        }
        fill_oos_compensating(&built[i], &comp);
        if (code_in_defs(built, built_count, comp.code)) {
            continue;
        }
        if (built_count >= ALARM_CATALOG_MAX) {
            LOG_ERROR("alarm_registry: no room for oos compensating of %06u", (unsigned)built[i].code);
            return SW_ERR_OVERFLOW;
        }
        built[built_count++] = comp;
    }

    pthread_mutex_lock(&s_mutex);
    memcpy(s_catalog, built, built_count * sizeof(s_catalog[0]));
    s_catalog_count = built_count;
    reset_runtime_state_locked();
    pthread_mutex_unlock(&s_mutex);

    LOG_INFO("alarm_registry: catalog loaded defs=%u", built_count);
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
    s_op_mode       = ALARM_OP_STOPPED;
    reset_runtime_state_locked();
    pthread_mutex_unlock(&s_mutex);

    LOG_INFO("alarm_registry: init ok");
    return SW_OK;
}

void alarm_registry_set_op_mode(alarm_op_mode_t mode)
{
    publish_batch_t batch = {0};

    pthread_mutex_lock(&s_mutex);
    if (s_op_mode != mode) {
        s_op_mode = mode;
        reeval_announce_locked(&batch);
    }
    pthread_mutex_unlock(&s_mutex);
    publish_batch(&batch);
}

sw_err_t alarm_registry_set_object_run(uint32_t layer, uint32_t object_id, alarm_object_run_t run)
{
    uint32_t        key;
    unsigned        i;
    int             free_idx = -1;
    publish_batch_t batch    = {0};

    if (!alarm_code_parts_valid(layer, object_id, 0U)) {
        return SW_ERR_PARAM;
    }
    key = alarm_object_key(layer, object_id);

    pthread_mutex_lock(&s_mutex);
    for (i = 0U; i < ALARM_OBJECT_RUN_MAX; ++i) {
        if (s_object_run[i].used && (s_object_run[i].key == key)) {
            s_object_run[i].run = run;
            reeval_announce_locked(&batch);
            pthread_mutex_unlock(&s_mutex);
            publish_batch(&batch);
            return SW_OK;
        }
        if (!s_object_run[i].used && (free_idx < 0)) {
            free_idx = (int)i;
        }
    }
    if (free_idx < 0) {
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_OVERFLOW;
    }
    s_object_run[(unsigned)free_idx].used = true;
    s_object_run[(unsigned)free_idx].key  = key;
    s_object_run[(unsigned)free_idx].run  = run;
    reeval_announce_locked(&batch);
    pthread_mutex_unlock(&s_mutex);
    publish_batch(&batch);
    return SW_OK;
}

sw_err_t alarm_registry_shelve(uint32_t code, uint32_t duration_ms)
{
    int             def_idx;
    int             active_idx;
    uint32_t        capped;
    publish_batch_t batch = {0};

    if (duration_ms == 0U) {
        return SW_ERR_PARAM;
    }
    capped = (duration_ms > ALARM_SHELVE_MAX_MS) ? ALARM_SHELVE_MAX_MS : duration_ms;

    pthread_mutex_lock(&s_mutex);
    def_idx = find_def_index(code);
    if ((def_idx < 0) || !alarm_def_allows_shelve(&s_catalog[(unsigned)def_idx])) {
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_PARAM;
    }
    active_idx = find_active_index(code);
    if (active_idx < 0) {
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_PARAM;
    }
    s_active[(unsigned)active_idx].shelved_until_ms = time_util_get_ms() + (uint64_t)capped;
    reeval_announce_locked(&batch);
    pthread_mutex_unlock(&s_mutex);
    publish_batch(&batch);
    return SW_OK;
}

sw_err_t alarm_registry_unshelve(uint32_t code)
{
    int             active_idx;
    publish_batch_t batch = {0};

    pthread_mutex_lock(&s_mutex);
    active_idx = find_active_index(code);
    if (active_idx < 0) {
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_PARAM;
    }
    s_active[(unsigned)active_idx].shelved_until_ms = 0U;
    reeval_announce_locked(&batch);
    pthread_mutex_unlock(&s_mutex);
    publish_batch(&batch);
    return SW_OK;
}

sw_err_t alarm_registry_set_oos(uint32_t code, bool enable)
{
    int             def_idx;
    publish_batch_t batch = {0};

    pthread_mutex_lock(&s_mutex);
    def_idx = find_def_index(code);
    if ((def_idx < 0) || !s_catalog[(unsigned)def_idx].allow_oos) {
        pthread_mutex_unlock(&s_mutex);
        return SW_ERR_PARAM;
    }
    if (s_oos[(unsigned)def_idx] == enable) {
        pthread_mutex_unlock(&s_mutex);
        return SW_OK;
    }
    s_oos[(unsigned)def_idx] = enable;
    if (enable) {
        drop_code_locked(code, &batch);
        (void)insert_compensating_locked(code, &batch);
    } else {
        drop_code_locked(alarm_registry_oos_compensating_code(code), &batch);
    }
    note_posture_edge_locked(&batch);
    reeval_announce_locked(&batch);
    pthread_mutex_unlock(&s_mutex);
    publish_batch(&batch);
    return SW_OK;
}

bool alarm_registry_is_oos(uint32_t code)
{
    int  def_idx;
    bool oos;

    pthread_mutex_lock(&s_mutex);
    def_idx = find_def_index(code);
    oos     = (def_idx >= 0) && s_oos[(unsigned)def_idx];
    pthread_mutex_unlock(&s_mutex);
    return oos;
}

void alarm_registry_clear_announce_overrides(void)
{
    unsigned        i;
    publish_batch_t batch = {0};

    pthread_mutex_lock(&s_mutex);
    for (i = 0U; i < s_active_count; ++i) {
        s_active[i].shelved_until_ms = 0U;
    }
    memset(s_object_run, 0, sizeof(s_object_run));
    reeval_announce_locked(&batch);
    pthread_mutex_unlock(&s_mutex);
    publish_batch(&batch);
}

void alarm_registry_clear_oos(void)
{
    unsigned        i;
    publish_batch_t batch = {0};

    pthread_mutex_lock(&s_mutex);
    for (i = 0U; i < s_catalog_count; ++i) {
        if (!s_oos[i]) {
            continue;
        }
        s_oos[i] = false;
        drop_code_locked(alarm_registry_oos_compensating_code(s_catalog[i].code), &batch);
    }
    note_posture_edge_locked(&batch);
    reeval_announce_locked(&batch);
    pthread_mutex_unlock(&s_mutex);
    publish_batch(&batch);
}

void alarm_registry_poll(uint64_t now_ms)
{
    unsigned        i;
    bool            changed = false;
    publish_batch_t batch   = {0};

    pthread_mutex_lock(&s_mutex);
    for (i = 0U; i < s_active_count; ++i) {
        if ((s_active[i].shelved_until_ms != 0U) && (now_ms >= s_active[i].shelved_until_ms)) {
            s_active[i].shelved_until_ms = 0U;
            changed                      = true;
        }
    }
    if (changed) {
        reeval_announce_locked(&batch);
    }
    pthread_mutex_unlock(&s_mutex);
    publish_batch(&batch);
}
