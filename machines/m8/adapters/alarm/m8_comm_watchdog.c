/**
 * @file    m8_comm_watchdog.c
 * @brief   M8 机型周期通讯设备心跳监控实现
 * @author  HUWANGWEI
 * @date    2026-06-28
 */

#include "machines/m8/adapters/alarm/m8_comm_watchdog.h"
#include "ports/safety/alarm_binding_port.h"
#include "common/time_util.h"
#include "common/log.h"
#include <stdbool.h>
#include <stddef.h>

/* -------------------------------------------------------------------------
 * 编译期推导设备数量
 * ------------------------------------------------------------------------- */
/* comm_dev_id_t 已由头文件 X-macro 展开，COMM_DEV_COUNT 即总数 */

/* -------------------------------------------------------------------------
 * 静态配置（超时阈值 + 报警码，由 X-macro 展开）
 * ------------------------------------------------------------------------- */
typedef struct
{
    uint32_t timeout_ms; /**< 心跳超时门限 */
    uint32_t alarm_code; /**< 报警码 */
} watchdog_cfg_t;

static const watchdog_cfg_t s_cfg[] = {
#define X(dev, timeout, cls, idx, nat, lvl, clr, desc) \
    { (timeout), ALARM_CODE_MAKE((cls), (idx), (nat)) },
    M8_COMM_WATCHDOG_TABLE(X)
#undef X
};

/* -------------------------------------------------------------------------
 * 运行时状态（每设备独立时间戳）
 * ------------------------------------------------------------------------- */
static uint32_t s_last_hb_ms[COMM_DEV_COUNT]; /**< 最近一次心跳时间戳 */

/* -------------------------------------------------------------------------
 * 接口实现
 * ------------------------------------------------------------------------- */

sw_err_t m8_comm_watchdog_init(void)
{
    /* 以当前时刻初始化，给各设备 timeout_ms 时间完成首次通讯 */
    uint32_t now = time_util_get_ms();
    for (int i = 0; i < (int)COMM_DEV_COUNT; ++i)
    {
        s_last_hb_ms[i] = now;
    }
    LOG_INFO("m8_comm_watchdog: init ok, devices=%d", (int)COMM_DEV_COUNT);
    return SW_OK;
}

void m8_comm_watchdog_heartbeat(comm_dev_id_t dev)
{
    if ((unsigned)dev < (unsigned)COMM_DEV_COUNT)
    {
        s_last_hb_ms[(int)dev] = time_util_get_ms();
    }
}

void m8_comm_watchdog_poll(void)
{
    const alarm_binding_ops_t *ops = alarm_binding_get_ops();
    if (ops == NULL)
    {
        return;
    }

    uint32_t now = time_util_get_ms();
    for (int i = 0; i < (int)COMM_DEV_COUNT; ++i)
    {
        bool lost = (time_elapsed_ms(s_last_hb_ms[i], now) > s_cfg[i].timeout_ms);
        if (lost)
        {
            (void)ops->trigger(s_cfg[i].alarm_code);
        }
        else
        {
            /* LATCHED 报警的 clear 为 no-op；AUTO_STATIC 报警在此自动清除 */
            (void)ops->clear(s_cfg[i].alarm_code);
        }
    }
}
