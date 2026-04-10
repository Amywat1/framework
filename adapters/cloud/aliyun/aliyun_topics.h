/**
 * @file    aliyun_topics.h
 * @brief   阿里云 MQTT Topic 与 JSON 字段名定义
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    简化版 Topic，用于 M8 设备与云端通信。
 *          生产环境可替换为标准阿里云物模型 Topic。
 */

#ifndef ADAPTERS_CLOUD_ALIYUN_TOPICS_H
#define ADAPTERS_CLOUD_ALIYUN_TOPICS_H

/* -------------------------------------------------------------------------
 * MQTT Topics
 * ------------------------------------------------------------------------- */
#define M8_TOPIC_PROPERTY_UP   "/m8/property/up"   /* 设备→云：属性上报 */
#define M8_TOPIC_CMD_DOWN      "/m8/cmd"            /* 云→设备：命令下行 */

/* -------------------------------------------------------------------------
 * 上报 JSON 字段名
 * ------------------------------------------------------------------------- */
#define REPORT_FIELD_DEV_STATE     "devState"
#define REPORT_FIELD_WASH_MODE     "washMode"
#define REPORT_FIELD_WASH_STEP     "washStep"
#define REPORT_FIELD_GANTRY_POS    "gantryPos"
#define REPORT_FIELD_HAS_ALARM     "hasAlarm"
#define REPORT_FIELD_ALARM_CODE    "alarmCode"
#define REPORT_FIELD_CLOUD_CONN    "cloudConn"

/* -------------------------------------------------------------------------
 * 命令 JSON 字段名
 * ------------------------------------------------------------------------- */
#define CMD_FIELD_ACTION    "action"
#define CMD_FIELD_MODE      "mode"

/* 命令 action 值 */
#define CMD_ACTION_START_WASH       "startWash"
#define CMD_ACTION_STOP_WASH        "stopWash"
#define CMD_ACTION_STOP_OPERATION   "stopOperation"
#define CMD_ACTION_RESUME_OPERATION "resumeOperation"
#define CMD_ACTION_RESET_FAULT      "resetFault"
#define CMD_ACTION_HOME_DEVICE      "homeDevice"

#endif /* ADAPTERS_CLOUD_ALIYUN_TOPICS_H */
