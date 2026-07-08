/**
 * @file    report_aggregator.h
 * @brief   云端状态上报聚合器接口
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    周期触发全量属性上报；重连与报警变化时触发即时上报。
 *          上报内容由项目注入 report port 的 builder 决定。
 */

#ifndef APPLICATION_REPORT_AGGREGATOR_H
#define APPLICATION_REPORT_AGGREGATOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "framework/common/sw_error.h"

/**
 * @brief  初始化上报聚合器（注册 cloud_thread、订阅云端连接与报警事件）
 */
sw_err_t report_aggregator_init(void);

/**
 * @brief  立即执行一次全量属性上报，不等待周期定时器
 * @note   供 cmd_sync 等场景调用；云端未连接时忽略
 */
void report_aggregator_request_resync(void);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_REPORT_AGGREGATOR_H */
