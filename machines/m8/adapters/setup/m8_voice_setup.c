/**
 * @file    m8_voice_setup.c
 * @brief   M8 机型语音模块实例绑定与告警事件接线
 * @author  HUWANGWEI
 * @date    2026-06-29
 */

#include "machines/m8/adapters/setup/m8_voice_setup.h"
#include "adapters/hal/linux_hw/hal_voice_linux.h"
#include "adapters/hal/linux_hw/drv/drv_voice.h"
#include "machines/m8/config/m8_voice_table.h"
#include "machines/m8/config/m8_alarm_table.h"
#include "ports/safety/alarm_binding_port.h"
#include "domain/model/alarm_code.h"
#include "common/log.h"

/* 语音模块通信失败报警码（ALM_C_CTRL=4，ALM_CTRL_VOICE=5，ALM_N_COMM_LOST=2）*/
#define VOICE_ALM_COMM_LOST ALARM_CODE_MAKE(ALM_C_CTRL, ALM_CTRL_VOICE, ALM_N_COMM_LOST)

/* 通信状态事件回调：在 drv_voice 操作调用点同步触发/清除告警 */
static void voice_event_cb(int event_code)
{
    const alarm_binding_ops_t *alm = alarm_binding_get_ops();

    if (alm == NULL) {
        return;
    }
    if (event_code == DRV_VOICE_EVT_COMM_LOST) {
        (void)alm->trigger(VOICE_ALM_COMM_LOST);
    } else if (event_code == DRV_VOICE_EVT_COMM_RESTORED) {
        (void)alm->clear(VOICE_ALM_COMM_LOST);
    }
}

sw_err_t m8_voice_setup(void)
{
    sw_err_t ret;

    ret = hal_voice_linux_init(M8_VOICE_SERIAL_PORT, M8_VOICE_BAUD, M8_VOICE_MODBUS_ADDR);
    if (ret != SW_OK) {
        LOG_ERROR("m8_voice_setup: init failed ret=%d", (int)ret);
        return ret;
    }

    /* 注册告警联动回调；回调在操作调用线程同步执行，无后台线程依赖 */
    const hal_voice_ops_t *ops = hal_voice_get_ops();
    if ((ops != NULL) && (ops->register_event_cb != NULL)) {
        ops->register_event_cb(voice_event_cb);
    }

    LOG_INFO("m8_voice_setup ok");
    return SW_OK;
}
