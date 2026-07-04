/**
 * @file    hal_vfd_port.h
 * @brief   变频器 HAL 端口接口（模块对外唯一入口）
 * @author  HUWANGWEI
 * @date    2026-06-01
 *
 * @note    业务层与其它 HAL 适配器仅通过本接口访问 VFD；
 *          组合层 generic/hal_vfd 实现完整语义，linux_hw / sim_hw 注入 backend。
 */

#ifndef PORTS_HAL_VFD_PORT_H
#define PORTS_HAL_VFD_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include "common/vfd_types.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief  VFD 实例标识（整数，具体值由机型配置层定义）
 * @note   M8 机型在 config/machine/m8_vfd_table.h 中定义 HAL_VFD_GANTRY / HAL_VFD_BRUSH
 */
typedef int hal_vfd_id_t;

typedef struct
{
    /** @brief  初始化全部 VFD 实例运行时状态 */
    sw_err_t (*init)(void);

    /** @brief  推进 RST 脉冲与通信监测（由调度层周期调用） */
    void (*tick)(void);

    sw_err_t (*run)(hal_vfd_id_t id, hal_vfd_gear_t gear);
    sw_err_t (*set_freq)(hal_vfd_id_t id, uint16_t freq_hz);
    sw_err_t (*stop)(hal_vfd_id_t id);
    sw_err_t (*fault_reset)(hal_vfd_id_t id);

    hal_vfd_state_t (*get_state)(hal_vfd_id_t id);

    /** @brief  实时读寄存器（发起 Modbus IO），支持 STATE / FAULT_CODE / CURRENT */
    sw_err_t (*read)(hal_vfd_id_t id, hal_vfd_reg_t reg, uint16_t *p_val);
    /** @brief  读缓存值（无 Modbus IO），支持 FAULT_CODE / CURRENT */
    sw_err_t (*get_cached)(hal_vfd_id_t id, hal_vfd_reg_t reg, uint16_t *p_val);

    void (*register_event_cb)(hal_vfd_id_t id, void (*cb)(int event_code));
} hal_vfd_ops_t;

void                    hal_vfd_register(const hal_vfd_ops_t *ops);
const hal_vfd_ops_t    *hal_vfd_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* PORTS_HAL_VFD_PORT_H */
