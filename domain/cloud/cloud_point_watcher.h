/**
 * @file    cloud_point_watcher.h
 * @brief   云端物模型 on_change 点位变更检测
 * @author  HUWANGWEI
 * @date    2026-07-08
 *
 * @note    周期 poll 比对 shadow，变更记入脏集合；由调度器 take 后批量上报。
 *          不发布事件。
 */

#ifndef DOMAIN_CLOUD_CLOUD_POINT_WATCHER_H
#define DOMAIN_CLOUD_CLOUD_POINT_WATCHER_H

#include "domain/cloud/cloud_point.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  初始化变更检测（对每个 on_change 可读点位建立 shadow，不置脏）
 */
sw_err_t cloud_point_watcher_init(const cloud_point_entry_t *entries, size_t count);

/**
 * @brief  周期检测变更并置脏
 */
void cloud_point_watcher_poll(void);

/**
 * @brief  取出脏点 id 并清除对应脏标记
 * @param  ids 输出缓冲，写入的是点位表内的静态 id 指针
 * @param  cap 缓冲容量
 * @return 实际写出的 id 数；未取出的脏点保留到下一拍
 */
size_t cloud_point_watcher_take_dirty(const char **ids, size_t cap);

/**
 * @brief  把取出但上报失败的 id 重新置脏
 */
void cloud_point_watcher_restore_dirty(const char *const *ids, size_t count);

/**
 * @brief  清空 watcher 状态（仅供单元测试）
 */
void cloud_point_watcher_reset_for_test(void);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_CLOUD_CLOUD_POINT_WATCHER_H */
