/**
 * @file    observation_event_bridge.c
 * @brief   框架事件到观测记录的桥接实现
 * @author  HUWANGWEI
 * @date    2026-08-04
 */

#include "application/bridges/observation_event_bridge.h"

#include "common/event_types.h"
#include "common/log.h"
#include "observability/core/observation.h"
#include "runtime/event_bus/event_bus.h"

#include <stddef.h>

/* -------------------------------------------------------------------------
 * 记录表
 *
 * 选事件的判据是「事后复盘需要这条时间线」，不是「事件重要」：
 *   安全姿态与急停       故障复盘的起点，必须有精确时刻
 *   报警触发/清除        与安全姿态互为因果，缺一条链就断
 *   IO 掉线/恢复         通信类故障的根因常在此，且与报警不一一对应
 *   洗车中止             区分"正常完成"与"中途中止"，后者需要现场
 *   命令被拒             拒绝原因编码在 param，是排查"指令没反应"的唯一线索
 *   恢复请求/完成        故障恢复是否走完，只能靠这两条配对判断
 *   云连接断开           上报中断期间的数据缺口需要有边界标记
 *
 * 不记录的：EVT_CLOUD_POINT_DIRTY（每次点位变化都发，量级远高于其余事件，
 * 会淹没队列）、EVT_OP_MODE_CONTEXT_SYNC（周期同步，无事件语义）、
 * EVT_WASH_CHECKPOINT_REACHED（正常流程进度，非异常线索）。
 * ------------------------------------------------------------------------- */
typedef struct {
    event_type_t              type;
    observation_severity_t    severity;
    observation_record_kind_t kind;
    const char               *source;
} observed_event_t;

static const observed_event_t k_observed[] = {
    {EVT_HW_ESTOP_ON,                OBSERVATION_SEVERITY_CRITICAL, OBSERVATION_RECORD_INCIDENT, "hw.estop"        },
    {EVT_HW_ESTOP_OFF,               OBSERVATION_SEVERITY_WARN,     OBSERVATION_RECORD_EVENT,    "hw.estop"        },
    {EVT_HW_IO_OFFLINE,              OBSERVATION_SEVERITY_ERROR,    OBSERVATION_RECORD_INCIDENT, "hw.io"           },
    {EVT_HW_IO_ONLINE,               OBSERVATION_SEVERITY_INFO,     OBSERVATION_RECORD_EVENT,    "hw.io"           },
    {EVT_SAFETY_LOCKOUT,             OBSERVATION_SEVERITY_CRITICAL, OBSERVATION_RECORD_INCIDENT, "safety"          },
    {EVT_SAFETY_NOMINAL,             OBSERVATION_SEVERITY_INFO,     OBSERVATION_RECORD_STATUS,   "safety"          },
    {EVT_ALARM_TRIGGERED,            OBSERVATION_SEVERITY_ERROR,    OBSERVATION_RECORD_EVENT,    "alarm"           },
    {EVT_ALARM_CLEARED,              OBSERVATION_SEVERITY_INFO,     OBSERVATION_RECORD_EVENT,    "alarm"           },
    {EVT_WASH_ABORTED,               OBSERVATION_SEVERITY_WARN,     OBSERVATION_RECORD_EVENT,    "wash"            },
    {EVT_OP_MODE_CMD_REJECTED,       OBSERVATION_SEVERITY_WARN,     OBSERVATION_RECORD_EVENT,    "op_mode.cmd"     },
    {EVT_OP_MODE_RECOVERY_REQUESTED, OBSERVATION_SEVERITY_WARN,     OBSERVATION_RECORD_EVENT,    "op_mode.recovery"},
    {EVT_OP_MODE_RECOVERY_COMPLETED, OBSERVATION_SEVERITY_INFO,     OBSERVATION_RECORD_EVENT,    "op_mode.recovery"},
    {EVT_CLOUD_DISCONNECTED,         OBSERVATION_SEVERITY_WARN,     OBSERVATION_RECORD_EVENT,    "cloud.link"      },
};

#define OBSERVED_EVENT_COUNT (sizeof(k_observed) / sizeof(k_observed[0]))

static const observed_event_t *find_observed(event_type_t type)
{
    unsigned i;

    for (i = 0U; i < OBSERVED_EVENT_COUNT; i++) {
        if (k_observed[i].type == type) {
            return &k_observed[i];
        }
    }
    return NULL;
}

/**
 * 统一 handler：在 event_dispatch 线程执行，必须轻量。
 * observation_publish 用 trylock + 忙则丢弃，不会阻塞分发。
 */
static void on_observed_event(const event_t *evt)
{
    const observed_event_t   *def;
    observation_record_spec_t spec;
    uint32_t                  param;

    if (evt == NULL) {
        return;
    }

    def = find_observed(evt->type);
    if (def == NULL) {
        return;
    }

    /* 载荷只放事件 param：其语义随事件类型而定（报警码、拒绝原因、点位索引等），
     * 桥接不解释它——解释需要各域的编码知识，那属于消费侧（导出/分析）的事。 */
    param = evt->param;

    spec.kind           = def->kind;
    spec.severity       = def->severity;
    spec.payload_format = OBSERVATION_PAYLOAD_BINARY;
    spec.event_code     = (uint32_t)evt->type;
    spec.source         = def->source;
    spec.payload        = &param;
    spec.payload_size   = sizeof(param);
    spec.context        = NULL; /* 用 observation 模块的当前默认上下文 */

    /* 返回值有意忽略：队列满时丢弃是既定策略（已计入 dropped_busy_count /
     * dropped_*_full_count），在此重试或告警会让观测反过来影响业务时序。 */
    (void)observation_publish(&spec);
}

sw_err_t observation_event_bridge_init(void)
{
    event_subscription_t subs[OBSERVED_EVENT_COUNT];
    unsigned             i;

    for (i = 0U; i < OBSERVED_EVENT_COUNT; i++) {
        subs[i].type    = k_observed[i].type;
        subs[i].handler = on_observed_event;
    }

    sw_err_t ret = event_subscribe_table(subs, OBSERVED_EVENT_COUNT);

    if (ret != SW_OK) {
        LOG_ERROR("observation_event_bridge: 订阅失败 ret=%d", (int)ret);
        return ret;
    }

    LOG_INFO("observation_event_bridge: 已桥接 %u 类事件", (unsigned)OBSERVED_EVENT_COUNT);
    return SW_OK;
}
