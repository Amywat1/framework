/**
 * @file    m8_cloud_device_cmd.h
 * @brief   M8 云端设备生命周期命令 handler
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#ifndef PROJECTS_M8_ADAPTERS_CLOUD_M8_CLOUD_DEVICE_CMD_H
#define PROJECTS_M8_ADAPTERS_CLOUD_M8_CLOUD_DEVICE_CMD_H

#include "framework/ports/inbound/command/command_port.h"
#include "framework/common/point_table/point_table.h"
#include "framework/common/sw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  向 framework command_port 提交设备命令
 * @param  type  设备命令类型
 * @retval SW_OK / SW_ERR_NOT_INIT / command_guard 拒绝码
 */
sw_err_t m8_device_cmd_submit(cmd_type_t type);

sw_err_t m8_cloud_set_cmd_home(const point_value_t *in);
sw_err_t m8_cloud_set_cmd_custom_stop(const point_value_t *in);

#ifdef __cplusplus
}
#endif

#endif /* PROJECTS_M8_ADAPTERS_CLOUD_M8_CLOUD_DEVICE_CMD_H */
