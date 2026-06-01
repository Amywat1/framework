/**
 * @file    m8_signal_filter.c
 * @brief   M8 表驱动信号滤波实现
 * @author  胡望伟
 * @date    2026-04-13
 */

#include "adapters/machine/m8/m8_signal_filter.h"

#include "common/log.h"
#include "core/event_bus/event_bus.h"
#include "ports/safety/alarm_binding_port.h"
#include "ports/hal/hal_io_port.h"

/* -------------------------------------------------------------------------
 * 运行时状态
 * 与 m8_signal_table[] 按下标一一对应。
 * ------------------------------------------------------------------------- */
typedef struct
{
    bool    confirmed;      /* 防抖确认后的稳定状态 */
    bool    last_raw;       /* 上次原始状态（完成极性转换后） */
    uint8_t stable_count;   /* 连续相同原始状态计数 */
} signal_rt_t;

static signal_rt_t s_rt[M8_SIGNAL_TABLE_SIZE];
static bool        s_ops_error_logged = false;

void m8_signal_filter_init(void)
{
    for (int i = 0; i < M8_SIGNAL_TABLE_SIZE; ++i)
    {
        s_rt[i].confirmed    = false;
        s_rt[i].last_raw     = false;
        s_rt[i].stable_count = 0U;
    }

    LOG_INFO("m8_signal_filter: init ok, signals=%d", M8_SIGNAL_TABLE_SIZE);
}

void m8_signal_filter_tick(void)
{
    const hal_io_ops_t *io = hal_io_get_ops();

    if ((io == NULL) || (io->di_read == NULL))
    {
        if (!s_ops_error_logged)
        {
            LOG_ERROR("m8_signal_filter: hal_io ops not ready");
            s_ops_error_logged = true;
        }
        return;
    }

    s_ops_error_logged = false;

    for (int i = 0; i < M8_SIGNAL_TABLE_SIZE; ++i)
    {
        const m8_signal_cfg_t *cfg = &m8_signal_table[i];
        signal_rt_t           *rt  = &s_rt[i];
        bool                   di_val;
        bool                   raw_active;
        uint8_t                threshold;
        bool                   state_changed = false;

        /* 1. 读取输入并完成极性转换 */
        di_val     = io->di_read(cfg->io_id);
        raw_active = cfg->active_low ? (!di_val) : di_val;

        /* 2. 连续计数防抖 */
        if (raw_active == rt->last_raw)
        {
            if (rt->stable_count < 255U)
            {
                rt->stable_count++;
            }
        }
        else
        {
            rt->last_raw     = raw_active;
            rt->stable_count = 1U;
        }

        /* 3. 达到阈值后切换稳定态 */
        threshold = raw_active ? cfg->trig_count : cfg->release_count;
        if ((rt->stable_count >= threshold) && (rt->confirmed != raw_active))
        {
            rt->confirmed = raw_active;
            state_changed = true;
        }

        /* 4. 仅在稳定态变化时发布事件 */
        if (state_changed)
        {
            event_type_t evt = raw_active ? cfg->evt_active : cfg->evt_inactive;
            if (evt != EVT_NONE)
            {
                (void)event_publish(evt, 0U);
            }

            LOG_DEBUG("m8_signal_filter: sig=%d state=%s",
                      i, raw_active ? "ACTIVE" : "INACTIVE");
        }

        /* 5. 将稳定态同步到报警引擎，报警层继续做策略级防抖 */
        /* alarm_code == 0 表示该信号只参与滤波和事件发布，
         * 不直接同步到 alarm_core；报警归属由更高层机器逻辑统一决定。 */
        if (cfg->alarm_code != 0U)
        {
            alarm_core_set_raw_trigger(cfg->alarm_code, rt->confirmed, false);
        }
    }
}

bool m8_signal_is_active(m8_signal_id_t sig_id)
{
    if (((int)sig_id < 0) || ((int)sig_id >= M8_SIGNAL_TABLE_SIZE))
    {
        return false;
    }

    return s_rt[(int)sig_id].confirmed;
}
