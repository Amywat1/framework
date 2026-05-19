#ifndef APPLICATION_COORDINATORS_BACKGROUND_ALARM_SETTINGS_H
#define APPLICATION_COORDINATORS_BACKGROUND_ALARM_SETTINGS_H

#include <stdbool.h>

/**
 * @file background_alarm_settings.h
 * @brief 定义后台报警监控的通用设置。
 */

/**
 * @brief 后台报警监控通用设置。
 */
typedef struct background_alarm_settings_t
{
    bool enabled;                      /**< 是否启用后台报警监控。 */
    unsigned long io_sample_period_ms; /**< IO 线程读取传感器快照的周期，单位毫秒。 */
    unsigned long detect_period_ms;    /**< 检测线程等待新快照通知的最大超时周期，单位毫秒。 */
} background_alarm_settings_t;

#endif
