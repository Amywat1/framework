/**
 * @file    hal_sensor_port.h
 * @brief   传感器与状态查询 HAL 端口接口
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    包含限位开关、急停、VFD 故障码查询等接口。
 */

#ifndef PORTS_HAL_SENSOR_PORT_H
#define PORTS_HAL_SENSOR_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common/sw_error.h"
#include <stdbool.h>
#include <stdint.h>

/* -------------------------------------------------------------------------
 * VFD 标识（用于 get_vfd_fault_code）
 * ------------------------------------------------------------------------- */
typedef enum
{
    HAL_VFD_GANTRY = 0, /* 龙门变频器 */
    HAL_VFD_BRUSH  = 1, /* 刷子变频器 */
} hal_vfd_id_t;

/* -------------------------------------------------------------------------
 * 传感器查询操作表
 * ------------------------------------------------------------------------- */
typedef struct
{
    /* 龙门限位 */
    bool (*gantry_at_fwd_limit)(void); /* 前限位是否触发 */
    bool (*gantry_at_rev_limit)(void); /* 后限位是否触发 */

    /* 顶刷升降限位 */
    bool (*lift_at_top)(void);    /* 上限位是否触发 */
    bool (*lift_at_bottom)(void); /* 下限位是否触发 */

    /* 急停（常闭接法，true = 急停有效） */
    bool (*is_estop_active)(void);

    /**
     * @brief  轮询输入硬件事件并发布事件总线消息
     * @note   用于正式运行期的硬件输入事件提取，例如急停边沿、限位触发。
     */
    void (*poll_input_events)(void);

    /**
     * @brief  读取 VFD 故障码
     * @param  vfd_id   目标 VFD
     * @param  p_code   输出故障码（0 = 无故障；不可为 NULL）
     * @retval SW_OK / SW_ERR_COMM / SW_ERR_PARAM
     */
    sw_err_t (*get_vfd_fault_code)(hal_vfd_id_t vfd_id, uint16_t *p_code);

} hal_sensor_ops_t;

/* -------------------------------------------------------------------------
 * 注册 / 获取
 * ------------------------------------------------------------------------- */
void                    hal_sensor_register(const hal_sensor_ops_t *ops);
const hal_sensor_ops_t *hal_sensor_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* PORTS_HAL_SENSOR_PORT_H */
