/**
 * @file    vfd_types.h
 * @brief   VFD 中立类型与事件码（供 ports 与 adapters 共用）
 * @author  胡望伟
 * @date    2026-06-01
 */

#ifndef COMMON_VFD_TYPES_H
#define COMMON_VFD_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/** VFD 运行状态 */
typedef enum
{
    HAL_VFD_STATE_STOPPED = 0,
    HAL_VFD_STATE_FWD,
    HAL_VFD_STATE_REV,
    HAL_VFD_STATE_FAULT,
} hal_vfd_state_t;

#define DRV_VFD_STATE_STOPPED   HAL_VFD_STATE_STOPPED
#define DRV_VFD_STATE_FWD       HAL_VFD_STATE_FWD
#define DRV_VFD_STATE_REV       HAL_VFD_STATE_REV
#define DRV_VFD_STATE_FAULT     HAL_VFD_STATE_FAULT

/** VFD 事件码（传递给 register_event_cb 回调） */
#define HAL_VFD_EVT_COMM_LOST       1   /* Modbus 通信丢失 */
#define HAL_VFD_EVT_COMM_RESTORED   2   /* Modbus 通信恢复 */
#define HAL_VFD_EVT_FAULT_DETECTED  3   /* VFD 检测到故障（fault_code 从 0 变为非零）*/
#define HAL_VFD_EVT_FAULT_CLEARED   4   /* VFD 故障消除（fault_code 恢复为 0）*/
#define HAL_VFD_EVT_CURRENT_UPDATE  5   /* 电流值已更新（读缓存 cached_current 获取）*/

/** 驱动层与 port 层共用的事件码别名 */
#define DRV_VFD_EVT_COMM_LOST       HAL_VFD_EVT_COMM_LOST
#define DRV_VFD_EVT_COMM_RESTORED   HAL_VFD_EVT_COMM_RESTORED
#define DRV_VFD_EVT_FAULT_DETECTED  HAL_VFD_EVT_FAULT_DETECTED
#define DRV_VFD_EVT_FAULT_CLEARED   HAL_VFD_EVT_FAULT_CLEARED
#define DRV_VFD_EVT_CURRENT_UPDATE  HAL_VFD_EVT_CURRENT_UPDATE

#ifdef __cplusplus
}
#endif

#endif /* COMMON_VFD_TYPES_H */
