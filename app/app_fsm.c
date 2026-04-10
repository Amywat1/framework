/**
 * @file    app_fsm.c
 * @brief   设备顶层有限状态机实现
 * @author  胡望伟
 * @date    2026-04-08
 */

#include "app_fsm.h"
#include "app_cloud.h"
#include "app_wash_engine.h"
#include "component/comp_brush.h"
#include "component/comp_gantry.h"
#include "component/comp_top_lift.h"
#include "component/comp_water.h"
#include "service/svc_alarm.h"
#include "service/svc_param.h"
#include "bsp/bsp_hal.h"
#include "common/log.h"
#include "common/sw_config.h"
#include <pthread.h>
#include <unistd.h>

/* -------------------------------------------------------------------------
 * 内部状态
 * ------------------------------------------------------------------------- */
static DevState_t      s_state     = DEV_STATE_INIT;
static pthread_mutex_t s_cmd_mutex = PTHREAD_MUTEX_INITIALIZER;
static DevCmd_t        s_cmd_queue = DEV_CMD_NONE;
static pthread_t       s_fsm_thread;

/* -------------------------------------------------------------------------
 * FSM 主线程（每 100ms 一次）
 * ------------------------------------------------------------------------- */
static void *fsm_thread_fn(void *arg)
{
    uint32_t report_timer_ms = 0U;
    (void)arg;

    while (true) {
        usleep(100U * 1000U);

        DevCmd_t cmd = DEV_CMD_NONE;
        pthread_mutex_lock(&s_cmd_mutex);
        cmd         = s_cmd_queue;
        s_cmd_queue = DEV_CMD_NONE;
        pthread_mutex_unlock(&s_cmd_mutex);

        /* 急停检测（最高优先级）*/
        if (hal_is_estop_active() && (s_state == DEV_STATE_RUN)) {
            wash_engine_emergency_stop();
            s_state = DEV_STATE_FAULT;
            (void)hal_entry_light_set(ENTRY_LIGHT_RED);
            LOG_WARN("FSM: E-STOP triggered");
            continue;
        }

        /* 报警自动切故障 */
        if (svc_alarm_has_error() && (s_state == DEV_STATE_RUN)) {
            wash_engine_emergency_stop();
            s_state = DEV_STATE_FAULT;
            (void)hal_entry_light_set(ENTRY_LIGHT_RED);
        }

        /* 状态机 */
        switch (s_state) {
            case DEV_STATE_INIT:
                s_state = DEV_STATE_IDLE;
                (void)hal_entry_light_set(ENTRY_LIGHT_GREEN);
                LOG_INFO("FSM: INIT → IDLE");
                break;

            case DEV_STATE_IDLE:
                if (cmd == DEV_CMD_ORDER) {
                    WashMode_t mode = (WashMode_t)svc_param_get_int(
                        PARAM_KEY_WASH_MODE, (int)WASH_MODE_STANDARD);
                    if (wash_engine_start(mode) == SW_OK) {
                        s_state = DEV_STATE_RUN;
                        (void)hal_entry_light_set(ENTRY_LIGHT_RED);
                        (void)hal_rod_close();
                        LOG_INFO("FSM: IDLE → RUN mode=%d", (int)mode);
                    }
                } else if (cmd == DEV_CMD_STOP) {
                    s_state = DEV_STATE_STOP;
                    (void)hal_entry_light_set(ENTRY_LIGHT_OFF);
                }
                break;

            case DEV_STATE_RUN:
                if (wash_engine_is_done()) {
                    s_state = DEV_STATE_COMPLETE;
                    LOG_INFO("FSM: RUN → COMPLETE");
                }
                break;

            case DEV_STATE_COMPLETE:
                (void)hal_rod_open();
                (void)hal_entry_light_set(ENTRY_LIGHT_GREEN);
                s_state = DEV_STATE_IDLE;
                LOG_INFO("FSM: COMPLETE → IDLE");
                break;

            case DEV_STATE_FAULT:
                if (cmd == DEV_CMD_RESET) {
                    svc_alarm_manual_reset();
                    if (!svc_alarm_has_error()) {
                        s_state = DEV_STATE_IDLE;
                        (void)hal_entry_light_set(ENTRY_LIGHT_GREEN);
                        LOG_INFO("FSM: FAULT → IDLE (reset)");
                    }
                }
                break;

            case DEV_STATE_STOP:
                if (cmd == DEV_CMD_RESUME) {
                    s_state = DEV_STATE_IDLE;
                    (void)hal_entry_light_set(ENTRY_LIGHT_GREEN);
                }
                break;

            default:
                break;
        }

        /* 云端上报（500ms 周期，由 app_cloud 判断是否有变化）*/
        report_timer_ms += 100U;
        if (report_timer_ms >= CFG_MQTT_REPORT_PERIOD_MS) {
            report_timer_ms = 0U;
            app_cloud_report(s_state, wash_engine_get_step(),
                             svc_alarm_has_error());
        }
    }

    return NULL;
}

/* -------------------------------------------------------------------------
 * 接口实现
 * ------------------------------------------------------------------------- */
sw_err_t app_fsm_init(void)
{
    sw_err_t ret;

    ret = comp_brush_init();    if (ret != SW_OK) { return ret; }
    ret = comp_gantry_init();   if (ret != SW_OK) { return ret; }
    ret = comp_top_lift_init(); if (ret != SW_OK) { return ret; }
    ret = comp_water_init();    if (ret != SW_OK) { return ret; }
    ret = wash_engine_init();   if (ret != SW_OK) { return ret; }

    s_state     = DEV_STATE_INIT;
    s_cmd_queue = DEV_CMD_NONE;

    if (pthread_create(&s_fsm_thread, NULL, fsm_thread_fn, NULL) != 0) {
        LOG_ERROR("app_fsm_init: FSM thread create failed");
        return SW_ERR_HW;
    }
    pthread_detach(s_fsm_thread);

    LOG_INFO("app_fsm_init ok");
    return SW_OK;
}

DevState_t app_fsm_get_state(void)
{
    return s_state;
}

void app_fsm_post_cmd(DevCmd_t cmd)
{
    pthread_mutex_lock(&s_cmd_mutex);
    s_cmd_queue = cmd;
    pthread_mutex_unlock(&s_cmd_mutex);
}

int app_debug_ctl(char *cmd, char *p1, char *p2)
{
    (void)p1;
    (void)p2;
    if (cmd == NULL) { return 0; }

    if (strcmp(cmd, "status") == 0) {
        LOG_INFO("app debug: state=%d step=%d alarm=%d",
                 (int)s_state, (int)wash_engine_get_step(),
                 (int)svc_alarm_has_error());
        return 1;
    }
    if (strcmp(cmd, "order")  == 0) { app_fsm_post_cmd(DEV_CMD_ORDER);  return 1; }
    if (strcmp(cmd, "stop")   == 0) { app_fsm_post_cmd(DEV_CMD_STOP);   return 1; }
    if (strcmp(cmd, "resume") == 0) { app_fsm_post_cmd(DEV_CMD_RESUME); return 1; }
    if (strcmp(cmd, "reset")  == 0) { app_fsm_post_cmd(DEV_CMD_RESET);  return 1; }
    return 0;
}
