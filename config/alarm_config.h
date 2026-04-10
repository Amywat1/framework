/**
 * @file    alarm_config.h
 * @brief   M8 洗车机报警码静态配置表（只读 const 数据，不含任何逻辑）
 * @author  HUWANGWEI
 * @date    2026-04-07
 *
 * @note    新增报警步骤：
 *          1. 在此表末尾追加一行
 *          2. 在 bsp/bsp_alarm.c 的 signal_poll 或 event_callback 中添加触发逻辑
 *          3. 急停必须保持在第 0 行（ALARM_EMC_TABLE_IDX = 0）
 */

#ifndef ALARM_CONFIG_H
#define ALARM_CONFIG_H

#include "service/svc_alarm.h"

/* 急停条目在表中的固定索引，svc_alarm 硬依赖此位置 */
#define ALARM_EMC_TABLE_IDX  0

/* clang-format off */
static const AlarmEntry_t g_alarm_table[] = {
    /* code   trigger_ms  level           recover_ms  recover                              desc          */
    {8100,    0,    ALARM_LEVEL_ERROR,   0,  ALARM_RECOVER_MANUAL,                        "急停"},
    {8001,    500,  ALARM_LEVEL_ERROR,   500, ALARM_RECOVER_AUTO,                         "龙门前限位异常"},
    {8002,    500,  ALARM_LEVEL_ERROR,   500, ALARM_RECOVER_AUTO,                         "龙门后限位异常"},
    {8003,    500,  ALARM_LEVEL_ERROR,   500, ALARM_RECOVER_MANUAL,                       "顶刷升降上限位异常"},
    {8004,    500,  ALARM_LEVEL_ERROR,   500, ALARM_RECOVER_MANUAL,                       "顶刷升降下限位异常"},
    {8010,    200,  ALARM_LEVEL_ERROR,   1000, ALARM_RECOVER_DRIVE | ALARM_RECOVER_MANUAL,"龙门 VFD 故障"},
    {8011,    200,  ALARM_LEVEL_ERROR,   1000, ALARM_RECOVER_DRIVE | ALARM_RECOVER_MANUAL,"刷子 VFD 故障"},
    {8012,    200,  ALARM_LEVEL_ERROR,   1000, ALARM_RECOVER_DRIVE | ALARM_RECOVER_MANUAL,"顶刷升降步进驱动器故障"},
    {8020,    0,    ALARM_LEVEL_WARNING,  0,  ALARM_RECOVER_AUTO,                         "龙门 Modbus 通信超时"},
    {8021,    0,    ALARM_LEVEL_WARNING,  0,  ALARM_RECOVER_AUTO,                         "刷子 Modbus 通信超时"},
    {8030,    0,    ALARM_LEVEL_NOTICE,   0,  ALARM_RECOVER_AUTO,                         "云端 MQTT 断线"},
};
/* clang-format on */

#define ALARM_TABLE_SIZE  (sizeof(g_alarm_table) / sizeof(g_alarm_table[0]))

#endif /* ALARM_CONFIG_H */
