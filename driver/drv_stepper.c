/**
 * @file    drv_stepper.c
 * @brief   雷赛步进电机驱动实现
 * @author  HUWANGWEI
 * @date    2026-04-07
 */

#include "drv_stepper.h"
#include "drv_io.h"
#include "common/log.h"
#include "config/machine_config.h"
#include <unistd.h>

/* ENA 引脚极性：雷赛驱动器默认低电平使能（true=LOW=使能）*/
#define STEPPER_ENA_ACTIVE  true
#define STEPPER_ENA_INACTIVE false

sw_err_t drv_stepper_init(void)
{
    /* 上电：ENA 无效（禁用），清除方向和脉冲输出 */
    (void)drv_io_do_set(DO_TOP_LIFT_ENA, STEPPER_ENA_INACTIVE);
    (void)drv_io_do_set(DO_TOP_LIFT_DIR, false);
    (void)drv_io_do_set(DO_TOP_LIFT_PUL, false);

    LOG_INFO("drv_stepper init ok");
    return SW_OK;
}

sw_err_t drv_stepper_enable(void)
{
    (void)drv_io_do_set(DO_TOP_LIFT_ENA, STEPPER_ENA_ACTIVE);
    usleep(5000U);  /* 使能后等待 5ms，让驱动器锁定 */
    return SW_OK;
}

sw_err_t drv_stepper_disable(void)
{
    (void)drv_io_do_set(DO_TOP_LIFT_ENA, STEPPER_ENA_INACTIVE);
    return SW_OK;
}

sw_err_t drv_stepper_move(uint32_t pulses, drv_stepper_dir_t dir, uint32_t pulse_us)
{
    uint32_t i;

    if (pulse_us < CFG_STEPPER_PULSE_US) {
        LOG_ERROR("drv_stepper_move: pulse_us=%u too small (min %u)",
                  (unsigned)pulse_us, (unsigned)CFG_STEPPER_PULSE_US);
        return SW_ERR_PARAM;
    }

    /* 设置方向 */
    (void)drv_io_do_set(DO_TOP_LIFT_DIR,
                        (dir == STEPPER_DIR_DOWN) ? true : false);
    usleep(10U);    /* DIR 建立时间 */

    /* 发送脉冲序列 */
    for (i = 0U; i < pulses; i++) {
        (void)drv_io_do_set(DO_TOP_LIFT_PUL, true);
        usleep(pulse_us);
        (void)drv_io_do_set(DO_TOP_LIFT_PUL, false);
        usleep(pulse_us);
    }

    return SW_OK;
}
