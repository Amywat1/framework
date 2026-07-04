/**
 * @file    report_port.h
 * @brief   云端状态上报端口接口
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    application/report_aggregator 通过此接口上报设备状态，
 *          不感知具体云平台（阿里云、华为云等）。
 */

#ifndef PORTS_CLOUD_REPORT_PORT_H
#define PORTS_CLOUD_REPORT_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/common/sw_error.h"
#include <stdint.h>
#include <stdbool.h>

/* -------------------------------------------------------------------------
 * 上报载荷（report_aggregator 从 dev_ctx 聚合后填充此结构）
 * ------------------------------------------------------------------------- */
typedef struct
{
    uint8_t  dev_state;      /* dev_state_t 枚举值 */
    uint8_t  wash_mode;      /* wash_mode_t 枚举值 */
    int32_t  gantry_pos;     /* 龙门当前位置（脉冲数）*/
    bool     cloud_connected;
    uint8_t  safety_state;   /* safety_state_t 枚举值 */
    bool     has_alarm;      /* 是否存在活跃报警 */
    uint32_t alarm_code;     /* 当前最高等级活跃报警码（无则 0）*/
} cloud_report_payload_t;

/* -------------------------------------------------------------------------
 * 云端上报操作表
 * ------------------------------------------------------------------------- */
typedef struct
{
    /**
     * @brief  上报当前设备状态快照
     * @param  payload  聚合后的状态载荷
     * @retval SW_OK / SW_ERR_COMM（云端未连接时忽略，不返回错误）
     */
    sw_err_t (*report)(const cloud_report_payload_t *payload);

    /**
     * @brief  查询云端连接状态
     * @retval true = MQTT 已连接
     */
    bool (*is_connected)(void);
} cloud_report_ops_t;

/* -------------------------------------------------------------------------
 * 注册 / 获取
 * ------------------------------------------------------------------------- */
void                     cloud_report_register(const cloud_report_ops_t *ops);
const cloud_report_ops_t *cloud_report_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* PORTS_CLOUD_REPORT_PORT_H */
