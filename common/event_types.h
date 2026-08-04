/**
 * @file    event_types.h
 * @brief   统一事件类型定义（分类编码 + event_t 结构体）
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    event_type_t 采用「类别 | 类内编号」复合编码：
 *            type = (category << 8) | local_id
 *          每类预留 EVT_PER_CAT_MAX 个槽位，新增事件只在本类 local_id 下扩展。
 *          event_t.timestamp_ms 由 event_bus 入队时自动填充。
 */

#ifndef COMMON_EVENT_TYPES_H
#define COMMON_EVENT_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/trace_context.h"

#include <stdbool.h>
#include <stdint.h>

/* -------------------------------------------------------------------------
 * 事件类别（扩展新域时在此追加，不影响已有类别编号）
 * ------------------------------------------------------------------------- */
typedef enum {
    EVT_CAT_NONE    = 0,
    EVT_CAT_HW      = 1, /**< 硬件异步（DI/VFD/IO 子板/编码器）*/
    EVT_CAT_COMP    = 2, /**< 组件完成（电机/归位/升降/刷子）*/
    EVT_CAT_SAFETY  = 3, /**< 安全域姿态（LOCKOUT/NOMINAL）*/
    EVT_CAT_ALARM   = 4, /**< 报警生命周期（激活/清除）*/
    EVT_CAT_CMD     = 5, /**< 外部命令（云端/CLI/本地）*/
    EVT_CAT_CLOUD   = 6, /**< 云端连接状态 */
    EVT_CAT_WASH    = 7, /**< 洗车流程（步骤/完成/中止）*/
    EVT_CAT_OP_MODE = 8, /**< 运行模式变更 / 命令拒绝 / 恢复请求 */
    EVT_CAT_MAX
} event_category_t;

/** 每个类别预留的类内事件槽位数 */
#define EVT_PER_CAT_MAX 32U

/** 复合编码：category 占高 8 位，local_id 占低 8 位 */
#define EVT_MAKE(category, local_id)                                                                                   \
    ((event_type_t)((((uint16_t)(category) & 0xFFU) << 8) | ((uint16_t)(local_id) & 0xFFU)))

typedef uint16_t event_type_t;

#define EVT_NONE ((event_type_t)0U)

/* -------------------------------------------------------------------------
 * HW 类（adapters/hal 发布）
 * 限位信号由 motor_tick 直接轮询，不经过事件总线。
 * ------------------------------------------------------------------------- */
#define EVT_HW_ID_ESTOP_ON   0U
#define EVT_HW_ID_ESTOP_OFF  1U
#define EVT_HW_ID_IO_OFFLINE 2U
#define EVT_HW_ID_IO_ONLINE  3U
/* 预留 5~31 */

#define EVT_HW_ESTOP_ON   EVT_MAKE(EVT_CAT_HW, EVT_HW_ID_ESTOP_ON)
#define EVT_HW_ESTOP_OFF  EVT_MAKE(EVT_CAT_HW, EVT_HW_ID_ESTOP_OFF)
#define EVT_HW_IO_OFFLINE EVT_MAKE(EVT_CAT_HW, EVT_HW_ID_IO_OFFLINE)
#define EVT_HW_IO_ONLINE  EVT_MAKE(EVT_CAT_HW, EVT_HW_ID_IO_ONLINE)

/* -------------------------------------------------------------------------
 * COMP 类（framework/domain/device_control 发布）
 * 电机单次动作完成（MOTOR_DONE）和刷子启动（BRUSH_STARTED）属于域内完成通知，
 * 改由 motor_set_done_cb 回调传递，不经过事件总线。
 * ------------------------------------------------------------------------- */
#define EVT_COMP_ID_HOME_DONE        0U
#define EVT_COMP_ID_MOTION_COMPLETED 1U
/* 预留 2~31 */

#define EVT_COMP_HOME_DONE        EVT_MAKE(EVT_CAT_COMP, EVT_COMP_ID_HOME_DONE)
#define EVT_COMP_MOTION_COMPLETED EVT_MAKE(EVT_CAT_COMP, EVT_COMP_ID_MOTION_COMPLETED)

/* -------------------------------------------------------------------------
 * SAFETY 类（alarm_event_bridge 按姿态边沿发布）
 * ------------------------------------------------------------------------- */
#define EVT_SAFETY_ID_LOCKOUT         0U
#define EVT_SAFETY_ID_NOMINAL         1U
#define EVT_SAFETY_ID_ABORT_HOME_DONE 2U /* 中止归位完成（abort_home_coordinator 发布；数值沿用原 HOME_DONE）*/

#define EVT_SAFETY_LOCKOUT  EVT_MAKE(EVT_CAT_SAFETY, EVT_SAFETY_ID_LOCKOUT)
#define EVT_SAFETY_NOMINAL  EVT_MAKE(EVT_CAT_SAFETY, EVT_SAFETY_ID_NOMINAL)
#define EVT_ABORT_HOME_DONE EVT_MAKE(EVT_CAT_SAFETY, EVT_SAFETY_ID_ABORT_HOME_DONE)

/* -------------------------------------------------------------------------
 * ALARM 类（alarm_event_bridge 发布）
 * ------------------------------------------------------------------------- */
#define EVT_ALARM_ID_TRIGGERED 0U
#define EVT_ALARM_ID_CLEARED   1U

#define EVT_ALARM_TRIGGERED EVT_MAKE(EVT_CAT_ALARM, EVT_ALARM_ID_TRIGGERED)
#define EVT_ALARM_CLEARED   EVT_MAKE(EVT_CAT_ALARM, EVT_ALARM_ID_CLEARED)

/* -------------------------------------------------------------------------
 * CMD 类（遗留事件 ID 仅保留测试用 ORDER；生产命令经 device_command_port.submit）
 * ------------------------------------------------------------------------- */
#define EVT_CMD_ID_ORDER        0U
#define EVT_CMD_ID_GATEWAY_WAKE 6U

#define EVT_CMD_ORDER        EVT_MAKE(EVT_CAT_CMD, EVT_CMD_ID_ORDER)
#define EVT_CMD_GATEWAY_WAKE EVT_MAKE(EVT_CAT_CMD, EVT_CMD_ID_GATEWAY_WAKE)

/* -------------------------------------------------------------------------
 * CLOUD 类（snack_cloud_link_adapter 发布）
 * ------------------------------------------------------------------------- */
#define EVT_CLOUD_ID_CONNECTED    0U
#define EVT_CLOUD_ID_DISCONNECTED 1U
#define EVT_CLOUD_ID_POINT_DIRTY  2U

#define EVT_CLOUD_CONNECTED    EVT_MAKE(EVT_CAT_CLOUD, EVT_CLOUD_ID_CONNECTED)
#define EVT_CLOUD_DISCONNECTED EVT_MAKE(EVT_CAT_CLOUD, EVT_CLOUD_ID_DISCONNECTED)
#define EVT_CLOUD_POINT_DIRTY  EVT_MAKE(EVT_CAT_CLOUD, EVT_CLOUD_ID_POINT_DIRTY)

/* -------------------------------------------------------------------------
 * WASH 类（项目洗车编排器发布）
 * ------------------------------------------------------------------------- */
#define EVT_WASH_ID_DONE               1U
#define EVT_WASH_ID_ABORTED            2U
#define EVT_WASH_ID_SESSION_STARTED    3U
#define EVT_WASH_ID_CHECKPOINT_REACHED 4U
#define EVT_WASH_ID_CUSTOMER_GONE      5U /**< 洗车区域已清空，客户离场 */

#define EVT_WASH_DONE               EVT_MAKE(EVT_CAT_WASH, EVT_WASH_ID_DONE)
#define EVT_WASH_ABORTED            EVT_MAKE(EVT_CAT_WASH, EVT_WASH_ID_ABORTED)
#define EVT_WASH_SESSION_STARTED    EVT_MAKE(EVT_CAT_WASH, EVT_WASH_ID_SESSION_STARTED)
#define EVT_WASH_CHECKPOINT_REACHED EVT_MAKE(EVT_CAT_WASH, EVT_WASH_ID_CHECKPOINT_REACHED)
#define EVT_WASH_CUSTOMER_GONE      EVT_MAKE(EVT_CAT_WASH, EVT_WASH_ID_CUSTOMER_GONE)

/* -------------------------------------------------------------------------
 * OP_MODE 类（OperationalMode 聚合发布）
 * ------------------------------------------------------------------------- */
#define EVT_OP_MODE_ID_CHANGED              0U
#define EVT_OP_MODE_ID_CMD_REJECTED         1U
#define EVT_OP_MODE_ID_RECOVERY_REQUESTED   2U
#define EVT_OP_MODE_ID_RECOVERY_COMPLETED   3U
#define EVT_OP_MODE_ID_SELF_CHECK_COMPLETED 4U
#define EVT_OP_MODE_ID_CONTEXT_SYNC         5U
#define EVT_OP_MODE_ID_CMD_HANDLED          6U /**< 命令处理完成（kind/status/reason 编码于 param）*/
#define EVT_OP_MODE_ID_HOME_COMPLETED       7U /**< 内部归位完成（param=1 成功）*/
#define EVT_OP_MODE_ID_ABORT_HOME_REQUESTED 8U /**< 中止归位请求（进入 ABORT_HOMING 时发布）*/

#define EVT_OP_MODE_CHANGED              EVT_MAKE(EVT_CAT_OP_MODE, EVT_OP_MODE_ID_CHANGED)
#define EVT_OP_MODE_CMD_REJECTED         EVT_MAKE(EVT_CAT_OP_MODE, EVT_OP_MODE_ID_CMD_REJECTED)
#define EVT_OP_MODE_RECOVERY_REQUESTED   EVT_MAKE(EVT_CAT_OP_MODE, EVT_OP_MODE_ID_RECOVERY_REQUESTED)
#define EVT_OP_MODE_RECOVERY_COMPLETED   EVT_MAKE(EVT_CAT_OP_MODE, EVT_OP_MODE_ID_RECOVERY_COMPLETED)
#define EVT_OP_MODE_SELF_CHECK_COMPLETED EVT_MAKE(EVT_CAT_OP_MODE, EVT_OP_MODE_ID_SELF_CHECK_COMPLETED)
#define EVT_OP_MODE_CONTEXT_SYNC         EVT_MAKE(EVT_CAT_OP_MODE, EVT_OP_MODE_ID_CONTEXT_SYNC)
#define EVT_OP_MODE_CMD_HANDLED          EVT_MAKE(EVT_CAT_OP_MODE, EVT_OP_MODE_ID_CMD_HANDLED)
#define EVT_OP_MODE_HOME_COMPLETED       EVT_MAKE(EVT_CAT_OP_MODE, EVT_OP_MODE_ID_HOME_COMPLETED)
#define EVT_ABORT_HOME_REQUESTED         EVT_MAKE(EVT_CAT_OP_MODE, EVT_OP_MODE_ID_ABORT_HOME_REQUESTED)

/* -------------------------------------------------------------------------
 * 编解码辅助
 * ------------------------------------------------------------------------- */

/**
 * @brief  提取事件类别
 */
static inline event_category_t event_type_category(event_type_t type)
{
    return (event_category_t)((type >> 8) & 0xFFU);
}

/**
 * @brief  提取类内编号
 */
static inline uint8_t event_type_local_id(event_type_t type)
{
    return (uint8_t)(type & 0xFFU);
}

/**
 * @brief  判断事件类型是否在合法编码范围内
 */
static inline bool event_type_is_valid(event_type_t type)
{
    event_category_t cat;
    uint8_t          local_id;

    if (type == EVT_NONE) {
        return false;
    }

    cat      = event_type_category(type);
    local_id = event_type_local_id(type);

    return (cat > EVT_CAT_NONE) && (cat < EVT_CAT_MAX) && (local_id < EVT_PER_CAT_MAX);
}

/* -------------------------------------------------------------------------
 * 事件结构体
 * ------------------------------------------------------------------------- */
typedef struct {
    event_type_t    type;         /**< 事件类型（复合编码）*/
    uint32_t        param;        /**< 载荷：报警码、错误码、模式等（无载荷时为 0）*/
    uint64_t        timestamp_ms; /**< 入队时间戳（由 event_bus 填充）*/
    uint64_t        event_id;     /**< 启动周期内唯一事件编号 */
    trace_context_t trace;        /**< 发布时捕获的业务因果上下文 */
} event_t;

#ifdef __cplusplus
}
#endif

#endif /* COMMON_EVENT_TYPES_H */
