/**
 * @file    alarm_registry.c
 * @brief   报警注册表聚合根实现
 * @author  HUWANGWEI
 * @date    2026-07-09
 */

#include "domain/safety/alarm_registry/alarm_registry.h"

#include "common/log.h"
#include "common/time_util.h"
#include "ports/inbound/safety/alarm_binding_port.h"

#include <pthread.h>
#include <stdbool.h>
#include <string.h>

static alarm_def_t          s_catalog[ALARM_CATALOG_MAX];
static unsigned             s_catalog_count;
static alarm_instance_t     s_active[ALARM_ACTIVE_MAX];
static unsigned             s_active_count;
static uint32_t             s_session_journal[ALARM_SESSION_JOURNAL_MAX];
static unsigned             s_session_journal_count;
static bool                 s_session_active;
static alarm_domain_event_t s_pending[ALARM_PENDING_EVENT_MAX];
static unsigned             s_pending_count;
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

static void enqueue_event_locked(alarm_domain_event_kind_t kind, uint32_t code)
{
    if (s_pending_count >= ALARM_PENDING_EVENT_MAX) {
        LOG_WARN("alarm_registry: pending event queue full");
        return;
    }
    s_pending[s_pending_count].kind = kind;
    s_pending[s_pending_count].code = code;
    s_pending_count++;
}

static void remove_active_at_locked(unsigned idx)
{
    if (idx >= s_active_count) {
        return;
    }
    if (idx + 1U < s_active_count) {
        memmove(&s_active[idx], &s_active[idx + 1U], (s_active_count - idx - 1U) * sizeof(s_active[0]));
    }
    s_active_count--;
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
        return false;
    }
    s_session_journal[s_session_journal_count++] = code;
    return true;
}

static void append_active_slot_locked(const alarm_def_t *def)
{
    alarm_instance_t *inst = &s_active[s_active_count];

    inst->code            = def->code;
    inst->level           = def->level;
    inst->clear           = def->clear;
    inst->triggered_at_ms = time_util_get_ms();
    s_active_count++;

    if (def->level >= ALARM_LEVEL_MAJOR) {
        (void)append_session_journal_locked(def->code);
    }

    enqueue_event_locked(ALARM_DOMAIN_EVT_TRIGGERED, def->code);
    LOG_WARN("alarm_registry: TRIGGERED %06u (%s)", (unsigned)def->code, def->desc);
}

static sw_err_t insert_active_locked(const alarm_def_t *def)
{
    if (s_active_count >= ALARM_ACTIVE_MAX) {
        LOG_ERROR("alarm_registry: active pool full, reject %06u level=%d",
                  (unsigned)def->code, (int)def->level);
        return SW_ERR_OVERFLOW;
    }

    append_active_slot_locked(def);
    return SW_OK;
}

static void force_clear_locked(uint32_t code)
{
    int idx = find_active_index(code);

    if (idx < 0) {
        return;
    }
    remove_active_at_locked((unsigned)idx);
    enqueue_event_locked(ALARM_DOMAIN_EVT_CLEARED, code);
    LOG_INFO("alarm_registry: CLEARED %06u", (unsigned)code);
}

sw_err_t alarm_registry_trigger(uint32_t code)
{
    int      def_idx;
    sw_err_t ret;

    def_idx = find_def_index(code);
    if (def_idx < 0) {
        LOG_WARN("alarm_registry: trigger unknown code=%06u", (unsigned)code);
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_mutex);
    if (find_active_index(code) >= 0) {
        pthread_mutex_unlock(&s_mutex);
        return SW_OK;
    }
    ret = insert_active_locked(&s_catalog[(unsigned)def_idx]);
    pthread_mutex_unlock(&s_mutex);
    return ret;
}

sw_err_t alarm_registry_clear(uint32_t code)
{
    int           def_idx;
    alarm_clear_t clr;

    def_idx = find_def_index(code);
    if (def_idx < 0) {
        LOG_WARN("alarm_registry: clear unknown code=%06u", (unsigned)code);
        return SW_ERR_PARAM;
    }

    clr = s_catalog[(unsigned)def_idx].clear;
    if ((clr == ALARM_CLEAR_MANUAL_RESET) || (clr == ALARM_CLEAR_ON_MOTION)) {
        return SW_OK;
    }

    pthread_mutex_lock(&s_mutex);
    force_clear_locked(code);
    pthread_mutex_unlock(&s_mutex);
    return SW_OK;
}

sw_err_t alarm_registry_reevaluate_group(motion_reeval_group_id_t group)
{
    uint32_t codes[ALARM_ACTIVE_MAX];
    unsigned n = 0U;
    unsigned i;

    pthread_mutex_lock(&s_mutex);
    for (i = 0; i < s_active_count; ++i) {
        int def_idx = find_def_index(s_active[i].code);
        if (def_idx < 0) {
            continue;
        }
        {
            const alarm_def_t *def = &s_catalog[(unsigned)def_idx];
            if ((def->clear == ALARM_CLEAR_ON_MOTION) && (def->reeval_group == group)) {
                codes[n++] = def->code;
            }
        }
    }
    for (i = 0; i < n; ++i) {
        force_clear_locked(codes[i]);
    }
    pthread_mutex_unlock(&s_mutex);
    return SW_OK;
}

void alarm_registry_on_wash_session_started(void)
{
    pthread_mutex_lock(&s_mutex);
    s_session_active        = true;
    s_session_journal_count = 0U;
    pthread_mutex_unlock(&s_mutex);
}

void alarm_registry_on_wash_session_ended(void)
{
    pthread_mutex_lock(&s_mutex);
    s_session_active = false;
    pthread_mutex_unlock(&s_mutex);
}

void alarm_registry_recover_all(void)
{
    uint32_t codes[ALARM_ACTIVE_MAX];
    unsigned n = 0U;
    unsigned i;

    pthread_mutex_lock(&s_mutex);
    for (i = 0; i < s_active_count; ++i) {
        int           def_idx;
        alarm_clear_t clr;
        uint32_t      code = s_active[i].code;

        def_idx = find_def_index(code);
        if (def_idx < 0) {
            continue;
        }
        clr = s_catalog[(unsigned)def_idx].clear;
        if ((clr != ALARM_CLEAR_ON_MOTION) && (clr != ALARM_CLEAR_MANUAL_RESET)) {
            continue;
        }
        codes[n++] = code;
    }
    for (i = 0; i < n; ++i) {
        force_clear_locked(codes[i]);
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

bool alarm_registry_has_blocking_active(void)
{
    bool     blocking = false;
    unsigned i;

    pthread_mutex_lock(&s_mutex);
    for (i = 0; i < s_active_count; ++i) {
        if (s_active[i].level >= ALARM_LEVEL_MAJOR) {
            blocking = true;
            break;
        }
    }
    pthread_mutex_unlock(&s_mutex);
    return blocking;
}

unsigned alarm_registry_get_session_journal(uint32_t *buf, unsigned max)
{
    unsigned copied = 0U;

    if ((buf == NULL) || (max == 0U)) {
        return 0U;
    }

    pthread_mutex_lock(&s_mutex);
    copied = s_session_journal_count;
    if (copied > max) {
        copied = max;
    }
    memcpy(buf, s_session_journal, copied * sizeof(buf[0]));
    pthread_mutex_unlock(&s_mutex);
    return copied;
}

unsigned alarm_registry_copy_active_projection(alarm_instance_t *list,
                                               unsigned          list_max,
                                               bool             *blocking_out,
                                               uint32_t         *top_out)
{
    unsigned copied = 0U;
    unsigned i;
    int      highest  = -1;
    uint32_t top      = ALARM_CODE_NONE;
    bool     blocking = false;

    if (list_max == 0U) {
        list = NULL;
    }

    pthread_mutex_lock(&s_mutex);
    copied = s_active_count;
    if ((list != NULL) && (copied > list_max)) {
        copied = list_max;
    }
    if ((list != NULL) && (copied > 0U)) {
        memcpy(list, s_active, copied * sizeof(list[0]));
    }

    for (i = 0U; i < s_active_count; ++i) {
        if (s_active[i].level >= ALARM_LEVEL_MAJOR) {
            blocking = true;
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
    pthread_mutex_unlock(&s_mutex);
    return copied;
}

safety_posture_t alarm_registry_safety_posture(void)
{
    safety_posture_t posture = SAFETY_POSTURE_NOMINAL;
    unsigned         i;

    pthread_mutex_lock(&s_mutex);
    for (i = 0; i < s_active_count; ++i) {
        if (s_active[i].level == ALARM_LEVEL_CRITICAL) {
            posture = SAFETY_POSTURE_LOCKOUT;
            break;
        }
    }
    pthread_mutex_unlock(&s_mutex);
    return posture;
}

unsigned alarm_registry_pull_events(alarm_domain_event_t *buf, unsigned max)
{
    unsigned pulled = 0U;

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
    pthread_mutex_unlock(&s_mutex);
    return pulled;
}

sw_err_t alarm_registry_load_catalog(const alarm_def_t *defs, unsigned count)
{
    if (defs == NULL) {
        return SW_ERR_PARAM;
    }
    if (count > ALARM_CATALOG_MAX) {
        return SW_ERR_OVERFLOW;
    }

    pthread_mutex_lock(&s_mutex);
    memcpy(s_catalog, defs, count * sizeof(s_catalog[0]));
    s_catalog_count = count;
    s_active_count  = 0U;
    s_pending_count = 0U;
    pthread_mutex_unlock(&s_mutex);

    LOG_INFO("alarm_registry: catalog loaded defs=%u", count);
    return SW_OK;
}

static sw_err_t binding_load_catalog(const alarm_def_t *defs, unsigned count)
{
    return alarm_registry_load_catalog(defs, count);
}

sw_err_t alarm_registry_init(void)
{
    static const alarm_binding_ops_t s_ops = {
        .trigger      = alarm_registry_trigger,
        .clear        = alarm_registry_clear,
        .load_catalog = binding_load_catalog,
    };

    alarm_binding_register(&s_ops);
    LOG_INFO("alarm_registry: init ok");
    return SW_OK;
}
