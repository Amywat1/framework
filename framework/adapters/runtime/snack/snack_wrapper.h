/**
 * @file    snack_wrapper.h
 * @brief   snack SDK C 封装总入口（聚合日志、MQTT、播放器、BLE 子头文件）
 * @author  HUWANGWEI
 * @date    2026-04-08
 *
 * @note    新模块请直接包含所需的细粒度头文件，而非本文件：
 *            - 日志：    framework/adapters/runtime/snack/snack_log.h
 *            - MQTT：    framework/adapters/runtime/snack/snack_mqtt.h
 *            - 播放器：  framework/adapters/runtime/snack/snack_player.h
 *            - BLE：     framework/adapters/runtime/snack/snack_ble.h
 *            - CLI：     framework/adapters/runtime/snack/snack_cli.h
 */

#ifndef SNACK_WRAPPER_H
#define SNACK_WRAPPER_H

#include "framework/adapters/runtime/snack/snack_log.h"
#include "framework/adapters/runtime/snack/snack_mqtt.h"
#include "framework/adapters/runtime/snack/snack_player.h"
#include "framework/adapters/runtime/snack/snack_ble.h"
#include "framework/adapters/runtime/snack/snack_cli.h"

#endif /* SNACK_WRAPPER_H */
