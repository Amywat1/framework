/**
 * @file    event_types.h
 * @brief   统一事件类型定义（分类编码 + event_t 结构体）
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    event_type_t 采用「类别 | 类内编号」复合编码：
 *            type = (category << 8) | local_id
 *          每类预留 EVT_PER_CAT_MAX 个槽位，新增事件只在本类 local_id 下扩展。
 *          event_t.timestamp_ms 由 event_bus 入队时自动填充。
 */

#ifndef EVENT_TYPES_H
#define EVENT_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* -------------------------------------------------------------------------
 * 事件类别（扩展新域时在此追加，不影响已有类别编号）
 * ------------------------------------------------------------------------- */
typedef enum
{
    EVT_CAT_NONE   = 0,
    EVT_CAT_HW     = 1,     /**< 硬件异步（DI/VFD/IO 子板/编码器）*/
    EVT_CAT_COMP   = 2,     /**< 组件完成（电机/归位/升降/刷子）*/
    EVT_CAT_SAFETY = 3,     /**< 安全域状态（LOCKOUT/WARNING/OK）*/
    EVT_CAT_ALARM  = 4,     /**< 报警生命周期（激活/清除）*/
    EVT_CAT_CMD    = 5,     /**< 外部命令（云端/CLI/本地）*/
    EVT_CAT_CLOUD  = 6,     /**< 云端连接状态 */
    EVT_CAT_WASH   = 7,     /**< 洗车流程（步骤/完成/中止）*/
    EVT_CAT_MAX
} event_category_t;

/** 每个类别预留的类内事件槽位数 */
#define EVT_PER_CAT_MAX    32U

/** 复合编码：category 占高 8 位，local_id 占低 8 位 */
#define EVT_MAKE(category, local_id) \
    ((event_type_t)((((uint16_t)(category) & 0xFFU) << 8) | \
                    ((uint16_t)(local_id) & 0xFFU)))

typedef uint16_t event_type_t;

#define EVT_NONE    ((event_type_t)0U)

/* -------------------------------------------------------------------------
 * HW 类（adapters/hal、signal_filter 发布）
 * ------------------------------------------------------------------------- */
#define EVT_HW_ID_ESTOP_ON           0U
#define EVT_HW_ID_ESTOP_OFF          1U
#define EVT_HW_ID_GANTRY_FWD_LIM     2U
#define EVT_HW_ID_GANTRY_REV_LIM     3U
#define EVT_HW_ID_LIFT_UP_LIM        4U
#define EVT_HW_ID_LIFT_DOWN_LIM      5U
#define EVT_HW_ID_ENCODER_ERR        6U
#define EVT_HW_ID_IO_OFFLINE         7U
#define EVT_HW_ID_IO_ONLINE          8U
#define EVT_HW_ID_VFD_BRUSH_FAULT     9U
#define EVT_HW_ID_VFD_GANTRY_FAULT   10U
/* 预留 11~31 */

#define EVT_HW_ESTOP_ON          EVT_MAKE(EVT_CAT_HW, EVT_HW_ID_ESTOP_ON)
#define EVT_HW_ESTOP_OFF         EVT_MAKE(EVT_CAT_HW, EVT_HW_ID_ESTOP_OFF)
#define EVT_HW_GANTRY_FWD_LIM    EVT_MAKE(EVT_CAT_HW, EVT_HW_ID_GANTRY_FWD_LIM)
#define EVT_HW_GANTRY_REV_LIM    EVT_MAKE(EVT_CAT_HW, EVT_HW_ID_GANTRY_REV_LIM)
#define EVT_HW_LIFT_UP_LIM       EVT_MAKE(EVT_CAT_HW, EVT_HW_ID_LIFT_UP_LIM)
#define EVT_HW_LIFT_DOWN_LIM     EVT_MAKE(EVT_CAT_HW, EVT_HW_ID_LIFT_DOWN_LIM)
#define EVT_HW_ENCODER_ERR       EVT_MAKE(EVT_CAT_HW, EVT_HW_ID_ENCODER_ERR)
#define EVT_HW_IO_OFFLINE        EVT_MAKE(EVT_CAT_HW, EVT_HW_ID_IO_OFFLINE)
#define EVT_HW_IO_ONLINE         EVT_MAKE(EVT_CAT_HW, EVT_HW_ID_IO_ONLINE)
#define EVT_HW_VFD_BRUSH_FAULT   EVT_MAKE(EVT_CAT_HW, EVT_HW_ID_VFD_BRUSH_FAULT)
#define EVT_HW_VFD_GANTRY_FAULT  EVT_MAKE(EVT_CAT_HW, EVT_HW_ID_VFD_GANTRY_FAULT)

/* -------------------------------------------------------------------------
 * COMP 类（domain/device 发布）
 * ------------------------------------------------------------------------- */
#define EVT_COMP_ID_MOTOR_DONE       0U
#define EVT_COMP_ID_HOME_DONE        1U
#define EVT_COMP_ID_LIFT_DONE        2U
#define EVT_COMP_ID_BRUSH_STARTED    3U

#define EVT_COMP_MOTOR_DONE      EVT_MAKE(EVT_CAT_COMP, EVT_COMP_ID_MOTOR_DONE)
#define EVT_COMP_HOME_DONE       EVT_MAKE(EVT_CAT_COMP, EVT_COMP_ID_HOME_DONE)
#define EVT_COMP_LIFT_DONE       EVT_MAKE(EVT_CAT_COMP, EVT_COMP_ID_LIFT_DONE)
#define EVT_COMP_BRUSH_STARTED   EVT_MAKE(EVT_CAT_COMP, EVT_COMP_ID_BRUSH_STARTED)

/* -------------------------------------------------------------------------
 * SAFETY 类（domain/safety/safety_fsm 发布）
 * ------------------------------------------------------------------------- */
#define EVT_SAFETY_ID_LOCKOUT        0U
#define EVT_SAFETY_ID_WARNING        1U
#define EVT_SAFETY_ID_CLEARED        2U

#define EVT_SAFETY_LOCKOUT       EVT_MAKE(EVT_CAT_SAFETY, EVT_SAFETY_ID_LOCKOUT)
#define EVT_SAFETY_WARNING       EVT_MAKE(EVT_CAT_SAFETY, EVT_SAFETY_ID_WARNING)
#define EVT_SAFETY_CLEARED       EVT_MAKE(EVT_CAT_SAFETY, EVT_SAFETY_ID_CLEARED)

/* -------------------------------------------------------------------------
 * ALARM 类（domain/safety/alarm_core 发布）
 * ------------------------------------------------------------------------- */
#define EVT_ALARM_ID_TRIGGERED       0U
#define EVT_ALARM_ID_CLEARED         1U

#define EVT_ALARM_TRIGGERED      EVT_MAKE(EVT_CAT_ALARM, EVT_ALARM_ID_TRIGGERED)
#define EVT_ALARM_CLEARED        EVT_MAKE(EVT_CAT_ALARM, EVT_ALARM_ID_CLEARED)

/* -------------------------------------------------------------------------
 * CMD 类（adapters/ui、adapters/cloud 发布）
 * ------------------------------------------------------------------------- */
#define EVT_CMD_ID_ORDER             0U
#define EVT_CMD_ID_STOP_WASH         1U
#define EVT_CMD_ID_STOP_OPERATION    2U
#define EVT_CMD_ID_RESUME_OPERATION  3U
#define EVT_CMD_ID_RESET_FAULT       4U
#define EVT_CMD_ID_HOME_DEVICE       5U

#define EVT_CMD_ORDER              EVT_MAKE(EVT_CAT_CMD, EVT_CMD_ID_ORDER)
#define EVT_CMD_STOP_WASH          EVT_MAKE(EVT_CAT_CMD, EVT_CMD_ID_STOP_WASH)
#define EVT_CMD_STOP_OPERATION     EVT_MAKE(EVT_CAT_CMD, EVT_CMD_ID_STOP_OPERATION)
#define EVT_CMD_RESUME_OPERATION   EVT_MAKE(EVT_CAT_CMD, EVT_CMD_ID_RESUME_OPERATION)
#define EVT_CMD_RESET_FAULT        EVT_MAKE(EVT_CAT_CMD, EVT_CMD_ID_RESET_FAULT)
#define EVT_CMD_HOME_DEVICE        EVT_MAKE(EVT_CAT_CMD, EVT_CMD_ID_HOME_DEVICE)

/* -------------------------------------------------------------------------
 * CLOUD 类（adapters/cloud 发布）
 * ------------------------------------------------------------------------- */
#define EVT_CLOUD_ID_CONNECTED       0U
#define EVT_CLOUD_ID_DISCONNECTED    1U

#define EVT_CLOUD_CONNECTED      EVT_MAKE(EVT_CAT_CLOUD, EVT_CLOUD_ID_CONNECTED)
#define EVT_CLOUD_DISCONNECTED   EVT_MAKE(EVT_CAT_CLOUD, EVT_CLOUD_ID_DISCONNECTED)

/* -------------------------------------------------------------------------
 * WASH 类（application/wash_orchestrator 发布）
 * ------------------------------------------------------------------------- */
#define EVT_WASH_ID_STEP_DONE        0U
#define EVT_WASH_ID_DONE             1U
#define EVT_WASH_ID_ABORTED          2U

#define EVT_WASH_STEP_DONE       EVT_MAKE(EVT_CAT_WASH, EVT_WASH_ID_STEP_DONE)
#define EVT_WASH_DONE            EVT_MAKE(EVT_CAT_WASH, EVT_WASH_ID_DONE)
#define EVT_WASH_ABORTED         EVT_MAKE(EVT_CAT_WASH, EVT_WASH_ID_ABORTED)

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

    if (type == EVT_NONE)
    {
        return false;
    }

    cat      = event_type_category(type);
    local_id = event_type_local_id(type);

    return (cat > EVT_CAT_NONE) &&
           (cat < EVT_CAT_MAX) &&
           (local_id < EVT_PER_CAT_MAX);
}

/* -------------------------------------------------------------------------
 * 事件结构体
 * ------------------------------------------------------------------------- */
typedef struct
{
    event_type_t type;          /**< 事件类型（复合编码）*/
    uint32_t     param;         /**< 载荷：报警码、错误码、模式等（无载荷时为 0）*/
    uint32_t     timestamp_ms;  /**< 入队时间戳（由 event_bus 填充）*/
} event_t;

#ifdef __cplusplus
}
#endif

#endif /* EVENT_TYPES_H */
