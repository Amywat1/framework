/**
 * @file    hal_vfd_port.h
 * @brief   变频器 HAL 端口接口（契约类型 + ops，模块对外唯一入口）
 * @author  HUWANGWEI
 * @date    2026-06-01
 *
 * @note    业务层与其它 HAL 适配器仅通过本接口访问 VFD；
 *          组合层 components/vfd_manager 实现完整语义，providers/<xx> / sim 注入 backend。
 */

#ifndef DOMAIN_PORTS_OUTBOUND_HAL_HAL_VFD_PORT_H
#define DOMAIN_PORTS_OUTBOUND_HAL_HAL_VFD_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief  VFD 速度挡位：正值=正转，负值=反转，0=停止；绝对值为挡位号（1=最低档）
 */
typedef int8_t hal_vfd_gear_t;

/**
 * @brief VFD 频率命令，正值=正转，负值=反转，0=停止，绝对值单位为 0.01 Hz。
 */
typedef int32_t hal_vfd_frequency_t;

/**
 * @brief  VFD 寄存器操作枚举（供 read / get_cached / write 使用）
 * @note   各操作支持的 reg 值见 hal_vfd_ops_t 各函数注释
 */
typedef enum {
    HAL_VFD_REG_STATE,      /**< VFD 运行状态字（只读）*/
    HAL_VFD_REG_FAULT_CODE, /**< 故障码（只读，monitor 周期缓存）*/
    HAL_VFD_REG_CURRENT,    /**< 输出电流，0.01A（只读，monitor 周期缓存）*/
} hal_vfd_reg_t;

/** @brief  VFD 运行状态 */
typedef enum {
    HAL_VFD_STATE_STOPPED = 0,
    HAL_VFD_STATE_FWD,
    HAL_VFD_STATE_REV,
    HAL_VFD_STATE_FAULT,
} hal_vfd_state_t;

/** @brief  VFD 事件码（传递给 register_event_cb 回调） */
#define HAL_VFD_EVT_COMM_LOST      1 /**< Modbus 通信丢失 */
#define HAL_VFD_EVT_COMM_RESTORED  2 /**< Modbus 通信恢复 */
#define HAL_VFD_EVT_FAULT_DETECTED 3 /**< VFD 检测到故障（fault_code 从 0 变为非零）*/
#define HAL_VFD_EVT_FAULT_CLEARED  4 /**< VFD 故障消除（fault_code 恢复为 0）*/
#define HAL_VFD_EVT_CURRENT_UPDATE 5 /**< 电流值已更新（读缓存 cached_current 获取）*/

/**
 * @brief  VFD 实例标识（整数，具体值由机型配置层定义）
 * @note   各项目在自己的 config 层 VFD 表头文件中定义具体实例标识
 *         （例如把龙门、毛刷等机构各分配一个 hal_vfd_id_t 常量）
 */
typedef int hal_vfd_id_t;

typedef struct {
    /** @brief  初始化全部 VFD 实例运行时状态 */
    sw_err_t (*init)(void);

    /**
     * @brief 按速度 IO 挡位运行，不写频率寄存器
     * @param id VFD 实例编号
     * @param gear 正值正转、负值反转、0 停止，绝对值为挡位号
     * @retval SW_OK / SW_ERR_PARAM / SW_ERR_NOT_INIT / SW_ERR_STATE / SW_ERR_HW
     * @note 运行中从频率模式切换会返回 SW_ERR_STATE，调用方须先停止。
     */
    sw_err_t (*set_gear)(hal_vfd_id_t id, hal_vfd_gear_t gear);
    /**
     * @brief 按寄存器频率运行，不修改速度 IO
     * @param id VFD 实例编号
     * @param frequency_centi_hz 正值正转、负值反转、0 停止，绝对值单位为 0.01 Hz
     * @retval SW_OK / SW_ERR_PARAM / SW_ERR_NOT_INIT / SW_ERR_STATE / SW_ERR_COMM / SW_ERR_HW
     * @note 运行中从挡位模式切换会返回 SW_ERR_STATE，调用方须先停止。
     */
    sw_err_t (*set_frequency)(hal_vfd_id_t id, hal_vfd_frequency_t frequency_centi_hz);
    /** @brief 停止并关断方向与已配置的速度 IO。 */
    sw_err_t (*stop)(hal_vfd_id_t id);
    sw_err_t (*fault_reset)(hal_vfd_id_t id);

    hal_vfd_state_t (*get_state)(hal_vfd_id_t id);

    /** @brief  实时读寄存器（发起 Modbus IO），支持 STATE / FAULT_CODE / CURRENT */
    sw_err_t (*read)(hal_vfd_id_t id, hal_vfd_reg_t reg, uint16_t *p_val);
    /** @brief  读缓存值（无 Modbus IO），支持 FAULT_CODE / CURRENT */
    sw_err_t (*get_cached)(hal_vfd_id_t id, hal_vfd_reg_t reg, uint16_t *p_val);

    void (*register_event_cb)(hal_vfd_id_t id, void (*cb)(int event_code));
} hal_vfd_ops_t;

sw_err_t             hal_vfd_register(const hal_vfd_ops_t *ops);
const hal_vfd_ops_t *hal_vfd_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_PORTS_OUTBOUND_HAL_HAL_VFD_PORT_H */
