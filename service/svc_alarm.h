/**
 * @file    svc_alarm.h
 * @brief   通用报警引擎接口（数据与引擎分离设计）
 * @author  HUWANGWEI
 * @date    2026-04-07
 *
 * @note    报警配置表在 config/alarm_config.h 中定义（只读 const）
 *          机型特定触发逻辑在 bsp/bsp_alarm.c 中通过回调注入
 */

#ifndef SVC_ALARM_H
#define SVC_ALARM_H

#include "common/sw_types.h"
#include "common/sw_error.h"
#include <stdint.h>

/* -------------------------------------------------------------------------
 * 报警等级
 * ------------------------------------------------------------------------- */
typedef enum
{
    ALARM_LEVEL_ERROR   = 0,    /* 立即停机，需手动处理后清除 */
    ALARM_LEVEL_WARNING = 1,    /* 当前工作完成后停机 */
    ALARM_LEVEL_NOTICE  = 2,    /* 仅上报，不停机 */
} AlarmLevel_t;

/* -------------------------------------------------------------------------
 * 恢复方式（可组合）
 * ------------------------------------------------------------------------- */
#define ALARM_RECOVER_AUTO      (1U << 0)   /* 条件消失后自动恢复 */
#define ALARM_RECOVER_DRIVE     (1U << 1)   /* 驱动复位后恢复 */
#define ALARM_RECOVER_MANUAL    (1U << 2)   /* 仅允许人工复位 */

/* -------------------------------------------------------------------------
 * 报警配置表条目（config/alarm_config.h 中填写）
 * ------------------------------------------------------------------------- */
typedef struct
{
    uint16_t     code;          /* 报警代码 */
    int          trigger_ms;    /* 触发延时（0=立即）*/
    AlarmLevel_t level;         /* 报警等级 */
    int          recover_ms;    /* 恢复延时（ms）*/
    uint32_t     recover;       /* 恢复方式（ALARM_RECOVER_xxx 组合）*/
    const char  *desc;          /* 描述（日志用）*/
} AlarmEntry_t;

/* -------------------------------------------------------------------------
 * 运行时状态标志（由 app 层写入，bsp_alarm 读取）
 * ------------------------------------------------------------------------- */
typedef struct
{
    bool is_machine_close;      /* 机器关闭运营（停止轮询）*/
    bool is_dev_running;        /* 设备洗车中 */
    bool is_dev_stopped;        /* 设备停机 */
    bool is_mqtt_connected;     /* 是否联网（触发上报）*/
    bool is_emc_reset_req;      /* 急停复位请求 */

    /* 硬件功能安装标志（影响报警降级逻辑）*/
    bool is_brush_top_installed;
    bool is_brush_side_installed;
    bool is_top_lift_installed;
} SvcAlarmFlags_t;

/* -------------------------------------------------------------------------
 * 回调类型（由 bsp_alarm 注入）
 * ------------------------------------------------------------------------- */
typedef void (*AlarmSignalPollFn)(void);        /* IO 信号轮询 */
typedef void (*AlarmEmcResetFn)(void);          /* 急停复位动作 */
typedef int  (*AlarmVfdReadFn)(uint16_t *code); /* 读 VFD 故障码 */

/* -------------------------------------------------------------------------
 * 引擎接口
 * ------------------------------------------------------------------------- */

/**
 * @brief  初始化报警引擎（从 config/alarm_config.h 加载配置表）
 * @retval SW_OK
 */
sw_err_t svc_alarm_init(void);

/**
 * @brief  注册信号轮询回调（bsp_alarm 注入，每 10ms 调用）
 */
void svc_alarm_register_poll_fn(AlarmSignalPollFn fn);

/**
 * @brief  注册急停复位回调
 */
void svc_alarm_register_emc_reset_fn(AlarmEmcResetFn fn);

/**
 * @brief  注册 VFD 故障读取回调
 */
void svc_alarm_register_vfd_read_fn(AlarmVfdReadFn fn);

/**
 * @brief  设置 IO 轮询类报警的原始触发状态（需防抖，由引擎计时）
 * @param  code         报警代码
 * @param  triggered    当前是否触发
 * @param  just_notice  true=强制降级为 NOTICE（硬件未安装）
 */
void svc_alarm_set_raw_trigger(uint16_t code, bool triggered, bool just_notice);

/**
 * @brief  直接置位/清除报警状态（驱动层事件，已防抖，直接有效）
 * @param  code         报警代码
 * @param  active       true=激活，false=清除
 * @param  just_notice  true=强制降级为 NOTICE
 */
void svc_alarm_set_state(uint16_t code, bool active, bool just_notice);

/**
 * @brief  查询报警是否激活
 * @retval true=有效报警
 */
bool svc_alarm_is_active(uint16_t code);

/**
 * @brief  查询是否有任何 ERROR 级别报警激活
 */
bool svc_alarm_has_error(void);

/**
 * @brief  手动复位所有 MANUAL 恢复类型的报警（按下复位按钮后调用）
 */
void svc_alarm_manual_reset(void);

/**
 * @brief  获取运行时状态标志指针（app 层通过此指针更新状态）
 */
SvcAlarmFlags_t *svc_alarm_flags(void);

/**
 * @brief  CLI 调试接口（alarm 命令域）
 * 命令：status / reset
 */
int alarm_debug_ctl(char *cmd, char *p1, char *p2);

#endif /* SVC_ALARM_H */
