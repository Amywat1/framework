/**
 * @file    snack_wrapper.h
 * @brief   snack SDK C 封装总入口（聚合日志、MQTT、播放器、BLE 子头文件）
 * @author  HUWANGWEI
 * @date    2026-04-08
 *
 * @note    新模块请直接包含所需的细粒度头文件，而非本文件：
 *            - 日志：    adapters/runtime/snack/snack_log.h
 *            - MQTT：    adapters/runtime/snack/snack_mqtt.h
 *            - 播放器：  adapters/runtime/snack/snack_player.h
 *            - BLE：     adapters/runtime/snack/snack_ble.h
 *            - CLI：     adapters/runtime/snack/snack_cli.h
 */

#ifndef SNACK_WRAPPER_H
#define SNACK_WRAPPER_H

#include "adapters/runtime/snack/snack_log.h"
#include "adapters/runtime/snack/snack_mqtt.h"
#include "adapters/runtime/snack/snack_player.h"
#include "adapters/runtime/snack/snack_ble.h"
#include "adapters/runtime/snack/snack_cli.h"

#endif /* SNACK_WRAPPER_H */
