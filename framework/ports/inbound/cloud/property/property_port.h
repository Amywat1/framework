/**
 * @file    property_port.h
 * @brief   云端属性下发端口接口
 * @author  HUWANGWEI
 * @date    2026-07-08
 */

#ifndef PORTS_INBOUND_CLOUD_PROPERTY_PORT_H
#define PORTS_INBOUND_CLOUD_PROPERTY_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/common/point_table/point_table.h"
#include "framework/common/sw_error.h"

typedef struct
{
    sw_err_t (*on_property_set)(const char *json_payload,
                                 point_apply_result_t *result);
    sw_err_t (*reply_property_set)(const char *request_json,
                                    const point_apply_result_t *result);
} cloud_property_ops_t;

void cloud_property_register(const cloud_property_ops_t *ops);
const cloud_property_ops_t *cloud_property_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* PORTS_INBOUND_CLOUD_PROPERTY_PORT_H */
