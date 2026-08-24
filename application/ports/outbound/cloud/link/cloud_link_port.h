/**
 * @file    cloud_link_port.h
 * @brief   云端链路端口（传输 + 连接边沿 + 属性上报）
 * @author  HUWANGWEI
 * @date    2026-07-08
 *
 * @note    遥测投影只依赖 is_online / 连接事件；上报调度器依赖
 *          publish_properties[_delta]；JSON 安装依赖 set_recv_handler。
 */

#ifndef APPLICATION_PORTS_OUTBOUND_CLOUD_LINK_CLOUD_LINK_PORT_H
#define APPLICATION_PORTS_OUTBOUND_CLOUD_LINK_CLOUD_LINK_PORT_H

#include "common/sw_error.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** MQTT 下行 property JSON 回调 */
typedef void (*cloud_link_recv_fn_t)(const char *msg);

/**
 * @brief  云端链路操作表
 */
typedef struct {
    /**
     * @brief  初始化云端传输（deploy 凭证）
     */
    sw_err_t (*init)(void);

    /**
     * @brief  查询链路是否在线
     */
    bool (*is_online)(void);

    /**
     * @brief  检测连接边沿并发布 EVT_CLOUD_*
     */
    void (*poll)(void);

    /**
     * @brief  向指定 Topic 发布 payload
     */
    sw_err_t (*publish)(const char *topic, const char *payload);

    /**
     * @brief  发布一次全量属性上报
     */
    sw_err_t (*publish_properties)(void);

    /**
     * @brief  发布指定点位的增量属性上报
     */
    sw_err_t (*publish_properties_delta)(const char *const *ids, size_t count);

    /**
     * @brief  注册下行消息回调
     */
    void (*set_recv_handler)(cloud_link_recv_fn_t cb);
} cloud_link_ops_t;

sw_err_t                cloud_link_register(const cloud_link_ops_t *ops);
const cloud_link_ops_t *cloud_link_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_PORTS_OUTBOUND_CLOUD_LINK_CLOUD_LINK_PORT_H */
