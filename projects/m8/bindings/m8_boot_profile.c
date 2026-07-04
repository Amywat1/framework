/**
 * @file    m8_boot_profile.c
 * @brief   M8 上电安全初始化
 * @author  HUWANGWEI
 * @date    2026-04-10
 *
 * @note    在 bootstrap 早期执行。
 *          通过轮询等待 IO 子板就绪，并在正常启动前强制所有输出进入安全状态。
 */

#include "framework/ports/outbound/hal/hal_io_port.h"
#include "framework/common/log.h"
#include "framework/common/sw_error.h"

#define BOOT_IO_TIMEOUT_MS  3000U   /* IO 子板就绪最长等待时间（ms）*/

static sw_err_t boot_do_set(io_do_t pin, bool val)
{
    const hal_io_ops_t *ops = hal_io_get_ops();

    if ((ops == NULL) || (ops->do_set == NULL))
    {
        return SW_ERR_NOT_INIT;
    }
    return ops->do_set(pin, val);
}

static sw_err_t boot_flush_outputs(void)
{
    const hal_io_ops_t *ops = hal_io_get_ops();

    if ((ops == NULL) || (ops->flush_outputs_now == NULL))
    {
        return SW_ERR_NOT_INIT;
    }
    return ops->flush_outputs_now();
}

/* X-macro 从 m8_io_table.h 自动展开所有 DO 引脚；新增引脚无需额外维护 */
static const io_do_t s_all_do_pins[] = {
#define DRV_IO_DO_DEF(name, board, pin, desc) IO_DO((board), (pin)),
#include "projects/m8/config/m8_io_table.h"
#undef DRV_IO_DO_DEF
};

/**
 * @brief  将所有数字输出置安全状态（统一关断并同步刷新到硬件）
 *
 * 遍历全部 DO 引脚统一置 false，然后立即 flush。
 * 在 drv_io 全板离线 panic 路径中，flush 因子板离线为 no-op，
 * 但 drv_io 在 panic_cb 返回后会无条件写缓冲，硬件仍进入安全态。
 */
void m8_assert_safe_outputs(void)
{
    for (unsigned i = 0U; i < (unsigned)(sizeof(s_all_do_pins) / sizeof(s_all_do_pins[0])); i++)
    {
        (void)boot_do_set(s_all_do_pins[i], false);
    }

    (void)boot_flush_outputs();
}

/**
 * @brief  等待所有 IO 子板就绪
 * @retval SW_OK          所有子板就绪
 * @retval SW_ERR_TIMEOUT 超时
 * @retval SW_ERR_NOT_INIT HAL 未注册
 */
static sw_err_t m8_wait_io_ready(void)
{
    const hal_io_ops_t *ops = hal_io_get_ops();

    if ((ops == NULL) || (ops->wait_boards_online == NULL))
    {
        return SW_ERR_NOT_INIT;
    }

    return ops->wait_boards_online(BOOT_IO_TIMEOUT_MS);
}

/**
 * @brief  M8 上电启动安全初始化（由 bootstrap 在 hal_io init 之后调用）
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

    /* 1. 等待 IO 子板就绪（HAL wait_boards_online 同步轮询，后台线程启动前可安全调用）*/
    io_ret = m8_wait_io_ready();
    if (io_ret != SW_OK)
    {
        /* 超时：告警但继续，后续由 hal_io / alarm 链路检测 IO 离线 */
        LOG_WARN("m8_boot: IO board not ready within %u ms, proceeding with alarm",
                 BOOT_IO_TIMEOUT_MS);
    }

    /* 2. 强制所有输出为安全状态（无论 IO 是否就绪都执行）*/
    m8_assert_safe_outputs();

    LOG_INFO("m8_boot_profile_init done (io_ready=%s)",
             (io_ret == SW_OK) ? "yes" : "timeout");
    return SW_OK; /* 始终返回 SW_OK，见函数头注释 */
}
