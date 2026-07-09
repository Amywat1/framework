/**
 * @file    device_safety_actuator.h
 * @brief   设备级安全停机接口（safety_thread 快速通道）
 * @author  HUWANGWEI
 * @date    2026-07-09
 *
 * @note    safety_thread 热路径委托 safety_cutout_execute()；
 *          完备停机由 safety_deferred_stop() 在 event_dispatch 执行。
 */

#ifndef RUNTIME_PLATFORM_DEVICE_SAFETY_ACTUATOR_H
#define RUNTIME_PLATFORM_DEVICE_SAFETY_ACTUATOR_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  立即停止全部运动/水路/风机等执行器输出
 */
void device_stop_all_actuators(void);

#ifdef __cplusplus
}
#endif

#endif /* RUNTIME_PLATFORM_DEVICE_SAFETY_ACTUATOR_H */
