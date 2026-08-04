/**
 * @file    report_scheduler.c
 * @brief   云端状态上报调度器实现
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#include "application/orchestrators/report_scheduler.h"

#include "common/event_types.h"
#include "common/log.h"
#include "common/sw_error.h"
#include "domain/cloud/cloud_point_watcher.h"
#include "ports/outbound/cloud/link/cloud_link_port.h"
#include "ports/outbound/cloud/report/report_port.h"
#include "runtime/config/thread_config.h"
#include "runtime/event_bus/event_bus.h"
#include "runtime/scheduler/periodic_task.h"

#include <sched.h>
#include <stdio.h>
#include <string.h>

/* 支持的最大策略条目数 */
#define REPORT_POLICY_MAX 8U
/* 支持的最大不同周期数（PERIODIC 类型策略去重后） */
#define REPORT_PERIOD_MAX 4U
/* 任务名称缓冲区长度（"cloud_report_" + 10位数字 + "ms" + '\0' = 26） */
#define TASK_NAME_LEN     32U

typedef struct {
    report_policy_entry_t entry;
} report_policy_slot_t;

static report_policy_slot_t s_policies[REPORT_POLICY_MAX];
static size_t               s_policy_count = 0U;
/* 已注册的唯一 period_ms 列表；每个元素作为 periodic_cb 的 ctx 传入 */
static uint32_t s_periods[REPORT_PERIOD_MAX];
static size_t   s_period_count = 0U;
/* 对应每个周期的任务名称缓冲区 */
static char                          s_task_names[REPORT_PERIOD_MAX][TASK_NAME_LEN];
static bool                          s_started        = false;
static report_point_id_resolver_fn_t s_point_resolver = NULL;

void report_scheduler_register_point_resolver(report_point_id_resolver_fn_t resolver)
{
    s_point_resolver = resolver;
}

static bool cloud_is_online(void)
{
    const cloud_link_ops_t *ops = cloud_link_get_ops();

    if ((ops == NULL) || (ops->is_online == NULL)) {
        return false;
    }
    return ops->is_online();
}

static void link_poll_if_needed(void)
{
    const cloud_link_ops_t *ops = cloud_link_get_ops();

    if ((ops != NULL) && (ops->poll != NULL)) {
        ops->poll();
    }
}

static void report_properties_once(void)
{
    const cloud_report_ops_t *ops = cloud_report_get_ops();

    if (!cloud_is_online() || (ops == NULL) || (ops->publish_properties == NULL)) {
        return;
    }

    sw_err_t ret = ops->publish_properties();

    /* 瞬时错误不告警：上报按周期重来，一次失败无需现场关注。原先只放过
     * SW_ERR_COMM，但 BUSY 与 TIMEOUT 在上报路径上同样是等下一拍即可的情形，
     * 为它们打 WARN 只会淹没真正需要看的错误。 */
    if ((ret != SW_OK) && !sw_err_is_transient(ret)) {
        LOG_WARN("report_scheduler: publish_properties failed ret=%s", sw_err_name(ret));
    }
}

static void report_delta_once(const char *const *ids, size_t count)
{
    const cloud_report_ops_t *ops = cloud_report_get_ops();

    if (!cloud_is_online() || (ops == NULL) || (ops->publish_properties_delta == NULL)) {
        return;
    }
    if ((ids == NULL) || (count == 0U)) {
        return;
    }

    sw_err_t ret = ops->publish_properties_delta(ids, count);

    if ((ret != SW_OK) && !sw_err_is_transient(ret)) {
        LOG_WARN("report_scheduler: publish_properties_delta failed ret=%s", sw_err_name(ret));
    }
}

static void run_policy(const report_policy_entry_t *policy, const event_t *evt)
{
    const char *id;
    const char *ids[1];

    if (policy == NULL) {
        return;
    }

    if (policy->use_event_param_as_point_index) {
        if ((evt == NULL) || (s_point_resolver == NULL)) {
            return;
        }
        id = s_point_resolver(evt->param);
        if (id == NULL) {
            return;
        }
        ids[0] = id;
        report_delta_once(ids, 1U);
        return;
    }

    if (policy->full) {
        report_properties_once();
    } else {
        report_delta_once(policy->delta_ids, policy->delta_id_count);
    }
}

/**
 * 周期回调：ctx 指向 s_periods[] 中对应的 period_ms 值，
 * 仅执行 period_ms 与之匹配的 PERIODIC 策略。
 */
static void periodic_cb(void *ctx)
{
    uint32_t period = *(const uint32_t *)ctx;
    size_t   i;

    link_poll_if_needed();
    cloud_point_watcher_poll();

    for (i = 0U; i < s_policy_count; i++) {
        if ((s_policies[i].entry.kind == REPORT_TRIGGER_PERIODIC) && (s_policies[i].entry.period_ms == period)) {
            run_policy(&s_policies[i].entry, NULL);
        }
    }
}

void report_scheduler_request_resync(void)
{
    report_properties_once();
}

static void on_event_policy(const event_t *evt)
{
    size_t i;

    if (evt == NULL) {
        return;
    }

    for (i = 0U; i < s_policy_count; i++) {
        if ((s_policies[i].entry.kind == REPORT_TRIGGER_EVENT) && (s_policies[i].entry.event_id == evt->type)) {
            if (s_policies[i].entry.event_id == EVT_CLOUD_CONNECTED) {
                LOG_INFO("report_scheduler: cloud connected, resync");
            }
            run_policy(&s_policies[i].entry, evt);
        }
    }
}

sw_err_t report_scheduler_register(const report_policy_entry_t *policies, size_t count)
{
    sw_err_t ret;
    size_t   i;
    size_t   j;
    bool     found;

    if (s_started) {
        return SW_OK;
    }

    if ((policies == NULL) || (count == 0U) || (count > REPORT_POLICY_MAX)) {
        return SW_ERR_PARAM;
    }

    memset(s_policies, 0, sizeof(s_policies));
    s_policy_count = count;
    s_period_count = 0U;

    /* 复制策略并收集唯一 period_ms */
    for (i = 0U; i < count; i++) {
        s_policies[i].entry = policies[i];
        if (policies[i].kind == REPORT_TRIGGER_PERIODIC) {
            if (policies[i].period_ms == 0U) {
                LOG_ERROR("report_scheduler: PERIODIC 策略 period_ms 不能为 0");
                return SW_ERR_PARAM;
            }
            /* 检查是否已收录该周期 */
            found = false;
            for (j = 0U; j < s_period_count; j++) {
                if (s_periods[j] == policies[i].period_ms) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                if (s_period_count >= REPORT_PERIOD_MAX) {
                    LOG_ERROR("report_scheduler: 超出最大周期数 %u", (unsigned)REPORT_PERIOD_MAX);
                    return SW_ERR_PARAM;
                }
                s_periods[s_period_count] = policies[i].period_ms;
                s_period_count++;
            }
        }
    }

    /* 为每个唯一周期注册独立的 periodic_task */
    for (j = 0U; j < s_period_count; j++) {
        snprintf(s_task_names[j], TASK_NAME_LEN, "cloud_report_%ums", (unsigned)s_periods[j]);
        ret = periodic_task_register(
            s_task_names[j], s_periods[j], periodic_cb, &s_periods[j], SCHED_OTHER, 0, THD_CLOUD_STACK);
        if (ret != SW_OK) {
            return ret;
        }
    }

    /* 注册事件触发策略 */
    for (i = 0U; i < count; i++) {
        if (policies[i].kind == REPORT_TRIGGER_EVENT) {
            ret = event_subscribe(policies[i].event_id, on_event_policy);
            if (ret != SW_OK) {
                return ret;
            }
        }
    }

    s_started = true;
    LOG_INFO("report_scheduler: registered policies=%u periods=%u", (unsigned)count, (unsigned)s_period_count);
    return SW_OK;
}
