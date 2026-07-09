/**
 * @file    m8_comm_watchdog.h
 * @brief   M8 机型周期通讯设备心跳监控
 * @author  HUWANGWEI
 * @date    2026-06-28
 *
 * @note    覆盖 m8_comm_watchdog_table.h 中列出的所有周期通讯设备（VFD、IO 子板等）。
 *          各 HAL adapter 在收到有效报文后调用 m8_comm_watchdog_heartbeat() 更新时间戳；
 *          io_poll 线程周期调用 m8_comm_watchdog_poll() 检查超时并触发/清除报警。
 *
 *          按需通讯设备（语音模块等）不在此监控范围，其 trigger/clear
 *          由各自 HAL adapter 在操作调用点同步处理。
 */

#ifndef ADAPTERS_MACHINE_M8_COMM_WATCHDOG_H
#define ADAPTERS_MACHINE_M8_COMM_WATCHDOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/common/sw_error.h"
#include "projects/m8/config/m8_alarm_comm_table.h"

/* -------------------------------------------------------------------------
 * 设备标识枚举（由 M8_COMM_WATCHDOG_TABLE 展开）
 * ------------------------------------------------------------------------- */
typedef enum
{
#define X(dev, timeout, cls, idx, nat, lvl, resp, clr, cut, desc) dev,
    M8_COMM_WATCHDOG_TABLE(X)
#undef X
    COMM_DEV_COUNT
} comm_dev_id_t;

/**
 * @brief  初始化心跳监控（将所有设备时间戳设为当前时刻，给设备留 timeout_ms 窗口首次通讯）
 * @retval SW_OK
 * @note   须在 m8_alarm_init() 之后、poll 线程启动前调用
 */
sw_err_t m8_comm_watchdog_init(void);

/**
 * @brief  更新设备心跳时间戳（由对应 HAL adapter 在收到有效报文时调用）
 * @param  dev  设备标识
 */
void m8_comm_watchdog_heartbeat(comm_dev_id_t dev);

/**
 * @brief  执行一次心跳超时检测（由 io_poll 线程周期调用）
 * @note   对超时设备 trigger（immediate_cutout 项同步 cutout）；心跳恢复后停止重复 trigger
 */
void m8_comm_watchdog_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_MACHINE_M8_COMM_WATCHDOG_H */
