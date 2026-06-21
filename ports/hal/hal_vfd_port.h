/**
 * @file    hal_vfd_port.h
 * @brief   变频器 HAL 端口接口（模块对外唯一入口）
 * @author  HUWANGWEI
 * @date    2026-06-01
 *
 * @note    业务层与其它 HAL 适配器仅通过本接口访问 VFD；
 *          平台实现（hal_vfd_linux / hal_vfd_sim）内部对接 drv_vfd 或仿真状态。
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
    /** @brief  初始化全部 VFD 实例（Modbus 连接 + DO 安全态） */
    sw_err_t (*init)(void);

    sw_err_t (*run_fwd)(hal_vfd_id_t id, uint16_t freq_hz);
    sw_err_t (*run_rev)(hal_vfd_id_t id, uint16_t freq_hz);
    sw_err_t (*stop)(hal_vfd_id_t id);
    sw_err_t (*fault_reset)(hal_vfd_id_t id);

    hal_vfd_state_t (*get_state)(hal_vfd_id_t id);

    sw_err_t (*get_fault_code)(hal_vfd_id_t id, uint16_t *p_code);
    sw_err_t (*read_current)(hal_vfd_id_t id, uint16_t *p_current);
    sw_err_t (*read_status)(hal_vfd_id_t id, uint16_t *p_status);

    void (*register_event_cb)(hal_vfd_id_t id, void (*cb)(int event_code));
} hal_vfd_ops_t;

void                    hal_vfd_register(const hal_vfd_ops_t *ops);
const hal_vfd_ops_t    *hal_vfd_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* PORTS_HAL_VFD_PORT_H */
