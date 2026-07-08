/**
 * @file    connection_port.h
 * @brief   云端连接状态端口接口
 * @author  HUWANGWEI
 * @date    2026-07-08
 *
 * @note    连接事实由 outbound cloud provider 维护；application / dev_ctx 只读查询。
 *          连接边沿变化由 provider 唯一发布 EVT_CLOUD_CONNECTED / DISCONNECTED。
 */

#ifndef PORTS_CLOUD_CONNECTION_PORT_H
#define PORTS_CLOUD_CONNECTION_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/**
 * @brief  云端连接操作表
 */
typedef struct
{
    /**
     * @brief  查询云端传输层是否在线
     * @retval true  已连接，可进行属性上报
     * @retval false 未连接或 provider 未就绪
     */
    bool (*is_connected)(void);
} cloud_connection_ops_t;

/**
 * @brief  注册云端连接 port 实现
 */
void cloud_connection_register(const cloud_connection_ops_t *ops);

/**
 * @brief  获取已注册的云端连接 port 实现
 * @retval NULL  尚未注册
 */
const cloud_connection_ops_t *cloud_connection_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* PORTS_CLOUD_CONNECTION_PORT_H */
