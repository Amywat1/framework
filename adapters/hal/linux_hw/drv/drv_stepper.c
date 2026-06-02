/**
 * @file    drv_stepper.c
 * @brief   雷赛步进电机驱动实现
 * @author  HUWANGWEI
 * @date    2026-04-07
 */

#include "drv_stepper.h"
#include "drv_io.h"
#include "config/machine/m8_io_pins.h"
#include "common/log.h"
#include "config/machine/m8_machine_config.h"
#include <time.h>
#include <unistd.h>

/* nanosleep 包装：将微秒延时转换为 nanosleep 调用
 * 相比 usleep，nanosleep 在 SCHED_FIFO 线程中精度更高（约 10-50µs vs 1-5ms）*/
static void delay_us(uint32_t us)
{
    struct timespec ts;
    struct timespec rem;
    ts.tv_sec  = 0;
    ts.tv_nsec = (long)us * 1000L;
    /* 循环处理 EINTR：信号打断后用剩余时间继续等待，防止长脉冲序列节拍失真 */
    while (nanosleep(&ts, &rem) != 0)
    {
        ts = rem;
    }
}

/* ENA 引脚极性：雷赛驱动器默认低电平使能（true=LOW=使能）*/
#define STEPPER_ENA_ACTIVE  true
#define STEPPER_ENA_INACTIVE false

sw_err_t drv_stepper_init(void)
{
    /* 上电：ENA 无效（禁用），清除方向和脉冲输出 */
    (void)drv_io_do_set(M8_IO_DO_TOP_LIFT_ENA, STEPPER_ENA_INACTIVE);
    (void)drv_io_do_set(M8_IO_DO_TOP_LIFT_DIR, false);
    (void)drv_io_do_set(M8_IO_DO_TOP_LIFT_PUL, false);

    LOG_INFO("drv_stepper init ok");
    return SW_OK;
}

sw_err_t drv_stepper_enable(void)
{
    (void)drv_io_do_set(M8_IO_DO_TOP_LIFT_ENA, STEPPER_ENA_ACTIVE);
    delay_us(5000U);  /* 使能后等待 5ms，让驱动器锁定 */
    return SW_OK;
}

sw_err_t drv_stepper_disable(void)
{
    (void)drv_io_do_set(M8_IO_DO_TOP_LIFT_ENA, STEPPER_ENA_INACTIVE);
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
    (void)drv_io_do_set(M8_IO_DO_TOP_LIFT_DIR,
                        (dir == STEPPER_DIR_DOWN) ? true : false);
    delay_us(10U);   /* DIR 建立时间 */

    /* 发送脉冲序列（nanosleep 精度优于 usleep，适合 SCHED_FIFO 线程）*/
    for (i = 0U; i < pulses; i++) {
        (void)drv_io_do_set(M8_IO_DO_TOP_LIFT_PUL, true);
        delay_us(pulse_us);
        (void)drv_io_do_set(M8_IO_DO_TOP_LIFT_PUL, false);
        delay_us(pulse_us);
    }

    return SW_OK;
}
