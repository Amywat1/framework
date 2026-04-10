/**
 * @file    sw_config.h
 * @brief   应用级编译配置（硬件参数见 config/machine/m8_machine_config.h）
 * @author  HUWANGWEI
 * @date    2026-04-07
 */

#ifndef SW_CONFIG_H
#define SW_CONFIG_H

/* -------------------------------------------------------------------------
 * 应用数据存储路径
 * ------------------------------------------------------------------------- */
#define CFG_DATA_DIR            "/home/neardi/m8/"
#define CFG_PARAM_FILE          CFG_DATA_DIR "params.json"
#define CFG_LOG_DIR             CFG_DATA_DIR "log/"

/* -------------------------------------------------------------------------
 * 报警引擎轮询周期（ms，不建议修改）
 * ------------------------------------------------------------------------- */
#define CFG_ALARM_POLL_PERIOD_MS    10U

/* -------------------------------------------------------------------------
 * 云端属性上报周期（ms）
 * ------------------------------------------------------------------------- */
#define CFG_MQTT_REPORT_PERIOD_MS   500U

#endif /* SW_CONFIG_H */
