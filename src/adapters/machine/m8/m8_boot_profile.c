/**
 * @file    m8_boot_profile.c
 * @brief   M8 上电安全初始化（所有输出置安全状态 + IO 子板就绪等待）
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    在 bootstrap 序列中尽早调用，替代原 bsp_init.c 中的 sleep(2) 硬等待。
 *          关键改进：轮询检测 IO 子板就绪，而非无条件等待固定时间。
 */

#include "adapters/machine/m8/m8_machine_map.h"
#include "driver/drv_io.h"
#include "common/log.h"
#include "common/sw_error.h"

#include <unistd.h>

#define BOOT_IO_POLL_INTERVAL_MS    50U     /* 就绪轮询间隔（ms）*/
#define BOOT_IO_TIMEOUT_MS          3000U   /* 最长等待时间（ms）*/

/**
 * @brief  将所有数字输出置安全状态（关断）
 *
 * 上电后硬件输出不确定，此函数强制清零所有 DO，
 * 确保电机、水泵、接触器等处于安全状态后再执行后续初始化。
 */
static void m8_set_all_outputs_safe(void)
{
    /* 入口指示灯全灭 */
    (void)drv_io_do_set(M8_DO_ENTRY_GREEN1, false);
    (void)drv_io_do_set(M8_DO_ENTRY_GREEN2, false);
    (void)drv_io_do_set(M8_DO_ENTRY_RED,    false);
    (void)drv_io_do_set(M8_DO_ENTRY_YELLOW, false);

    /* 入口挡杆：伸出（拦截）*/
    (void)drv_io_do_set(M8_DO_ROD_RETRACT, false);
    (void)drv_io_do_set(M8_DO_ROD_EXTEND,  true);

    /* 接触器全部断开 */
    (void)drv_io_do_set(M8_DO_TOP_BRUSH_ACT,  false);
    (void)drv_io_do_set(M8_DO_SIDE_BRUSH_ACT, false);

    /* 龙门 VFD 控制信号清零 */
    (void)drv_io_do_set(M8_DO_GANTRY_FWD, false);
    (void)drv_io_do_set(M8_DO_GANTRY_REV, false);
    (void)drv_io_do_set(M8_DO_GANTRY_RST, false);

    /* 刷子 VFD 控制信号清零 */
    (void)drv_io_do_set(M8_DO_BRUSH_FWD, false);
    (void)drv_io_do_set(M8_DO_BRUSH_RST, false);

    /* 步进电机：禁用 */
    (void)drv_io_do_set(M8_DO_LIFT_ENA, false);
    (void)drv_io_do_set(M8_DO_LIFT_DIR, false);
    (void)drv_io_do_set(M8_DO_LIFT_PUL, false);

    /* 水路全关 */
    (void)drv_io_do_set(M8_DO_WATER_PUMP,     false);
    (void)drv_io_do_set(M8_DO_WATER_CURTAIN,  false);
    (void)drv_io_do_set(M8_DO_WATER_FOAM,     false);
    (void)drv_io_do_set(M8_DO_WATER_BRUSH,    false);
    (void)drv_io_do_set(M8_DO_WATER_HIGHPRES, false);
}

/**
 * @brief  等待所有 IO 子板上线（轮询替代 sleep(2)）
 * @retval SW_OK        所有子板就绪
 * @retval SW_ERR_TIMEOUT 超时（可降级运行或报错）
 */
static sw_err_t m8_wait_io_ready(void)
{
    uint32_t elapsed_ms = 0U;

    while (elapsed_ms < BOOT_IO_TIMEOUT_MS)
    {
        bool all_online = true;
        for (int i = 1; i <= M8_IO_BOARD_COUNT; i++)
        {
            if (!drv_io_board_is_online(i))
            {
                all_online = false;
                break;
            }
        }

        if (all_online)
        {
            LOG_INFO("m8_boot: all IO boards online in %u ms", elapsed_ms);
            return SW_OK;
        }

        usleep((unsigned long)BOOT_IO_POLL_INTERVAL_MS * 1000UL);
        elapsed_ms += BOOT_IO_POLL_INTERVAL_MS;
    }

    LOG_ERROR("m8_boot: IO board not ready after %u ms", BOOT_IO_TIMEOUT_MS);
    return SW_ERR_TIMEOUT;
}

/**
 * @brief  M8 上电启动安全初始化（由 bootstrap 在 drv_io_init 之后调用）
 * @retval SW_OK / SW_ERR_TIMEOUT
 */
sw_err_t m8_boot_profile_init(void)
{
    sw_err_t ret;

    /* 1. 等待 IO 子板就绪（替代原 sleep(2)）*/
    ret = m8_wait_io_ready();
    if (ret != SW_OK)
    {
        /* 超时：记录日志但继续启动，避免设备因 IO 子板缓慢启动而永远卡住 */
        LOG_WARN("m8_boot: IO timeout, continuing with partial init");
    }

    /* 2. 强制所有输出为安全状态 */
    m8_set_all_outputs_safe();

    LOG_INFO("m8_boot_profile_init ok");
    return SW_OK;
}
