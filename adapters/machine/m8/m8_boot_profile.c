/**
 * @file    m8_boot_profile.c
 * @brief   M8 上电安全初始化
 * @author  胡望伟
 * @date    2026-04-10
 *
 * @note    在 bootstrap 早期执行。
 *          通过轮询等待 IO 子板就绪，并在正常启动前强制所有输出进入安全状态。
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
 * @brief  等待所有 IO 子板就绪
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
 *
 * 返回值约定（设计为返回 SW_OK 即使 IO 超时）：
 *   - 若 IO 子板在超时内就绪：执行安全清零，返回 SW_OK。
 *   - 若 IO 超时：记录告警，仍执行安全清零，返回 SW_OK。
 *     理由：设备上电时 IO 子板可能启动较慢；完全拒绝启动风险更高，
 *           允许带告警继续启动，后续报警系统会感知 IO 离线状态。
 *   - 如需让调用方感知 IO 就绪失败，可改为传入 out 参数 bool *io_ready。
 *
 * @retval SW_OK（始终）
 */
sw_err_t m8_boot_profile_init(void)
{
    sw_err_t io_ret;

    /* 1. 等待 IO 子板就绪 */
    io_ret = m8_wait_io_ready();
    if (io_ret != SW_OK)
    {
        /* 超时：告警但继续，后续报警系统会检测 IO 离线并发布 EVT_HW_IO_OFFLINE */
        LOG_WARN("m8_boot: IO board not ready within %u ms, proceeding with alarm",
                 BOOT_IO_TIMEOUT_MS);
    }

    /* 2. 强制所有输出为安全状态（无论 IO 是否就绪都执行）*/
    m8_set_all_outputs_safe();

    LOG_INFO("m8_boot_profile_init done (io_ready=%s)",
             (io_ret == SW_OK) ? "yes" : "timeout");
    return SW_OK; /* 始终返回 SW_OK，见函数头注释 */
}
