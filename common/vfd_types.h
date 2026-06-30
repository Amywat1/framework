/**
 * @file    vfd_types.h
 * @brief   VFD 中立类型与事件码（供 ports 与 adapters 共用）
 * @author  HUWANGWEI
 * @date    2026-06-01
 */

#ifndef COMMON_VFD_TYPES_H
#define COMMON_VFD_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * VFD 速度挡位类型：正值=正转，负值=反转，0=停止；绝对值为挡位号（1=最低档）
 */
typedef int8_t hal_vfd_gear_t;

/**
 * VFD 寄存器操作枚举（供 hal_vfd_ops_t read / get_cached 使用）
 * 各操作支持的 reg 值见各函数注释
 */
typedef enum {
    HAL_VFD_REG_STATE,        /* VFD 运行状态字（只读）*/
    HAL_VFD_REG_FAULT_CODE,   /* 故障码（只读，monitor 周期缓存）*/
    HAL_VFD_REG_CURRENT,      /* 输出电流，0.01A（只读，monitor 周期缓存）*/
    HAL_VFD_REG_FREQ,         /* 目标频率 Hz（只写；须厂商定义 VFD_REG_FREQ_SET）*/
    HAL_VFD_REG_CLEAR_FAULT,  /* 清除故障（只写；须厂商定义 VFD_REG_CLEAR_FAULT）*/
} hal_vfd_reg_t;

/** VFD 运行状态 */
typedef enum
{
    HAL_VFD_STATE_STOPPED = 0,
    HAL_VFD_STATE_FWD,
    HAL_VFD_STATE_REV,
    HAL_VFD_STATE_FAULT,
} hal_vfd_state_t;

/** VFD 事件码（传递给 register_event_cb 回调） */
#define HAL_VFD_EVT_COMM_LOST       1   /* Modbus 通信丢失 */
#define HAL_VFD_EVT_COMM_RESTORED   2   /* Modbus 通信恢复 */
#define HAL_VFD_EVT_FAULT_DETECTED  3   /* VFD 检测到故障（fault_code 从 0 变为非零）*/
#define HAL_VFD_EVT_FAULT_CLEARED   4   /* VFD 故障消除（fault_code 恢复为 0）*/
#define HAL_VFD_EVT_CURRENT_UPDATE  5   /* 电流值已更新（读缓存 cached_current 获取）*/

#ifdef __cplusplus
}
#endif

#endif /* COMMON_VFD_TYPES_H */
