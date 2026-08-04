/**
 * @file    device_snapshot_internal.h
 * @brief   统一遥测快照内部写接口（仅 telemetry_projection 使用）
 * @author  HUWANGWEI
 * @date    2026-07-14
 */

#ifndef DOMAIN_TELEMETRY_DEVICE_SNAPSHOT_INTERNAL_H
#define DOMAIN_TELEMETRY_DEVICE_SNAPSHOT_INTERNAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "domain/telemetry/device_snapshot.h"

/**
 * @brief  更新运行模式子域快照
 */
void device_snapshot_update_op(const operational_snapshot_t *op);

/**
 * @brief  更新安全/报警子域快照
 */
void device_snapshot_update_safety(const safety_snapshot_t *safety);

/**
 * @brief  更新洗车模式（会话启动时调用）
 */
void device_snapshot_set_wash_mode(wash_mode_t mode);

/**
 * @brief  更新云连接状态
 * @note   由遥测投影从云端口读取后写入，使读侧与其他子域在同一次快照中一致。
 */
void device_snapshot_set_cloud_connected(bool connected);

/**
 * @brief  清空快照全部子域（仅测试使用）
 *
 * @note   本模块没有 init 入口，快照靠加载期零初始化，进程内无从复位。
 *         单元测试若断言快照的绝对值，就会隐式依赖用例执行顺序；本函数供
 *         setUp 调用以消除该依赖。
 * @note   禁止在生产路径调用：快照被云上报与状态投影读取，运行期清零会让
 *         下游看到一个设备从未处于过的状态。
 */
void device_snapshot_reset_for_test(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_TELEMETRY_DEVICE_SNAPSHOT_INTERNAL_H */
