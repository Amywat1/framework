/**
 * @file    m8_alarm_adapt.c
 * @brief   M8 机型报警绑定适配实现
 * @author  HUWANGWEI
 * @date    2026-06-26
 */

#include "adapters/machine/m8/m8_alarm_adapt.h"
#include "ports/safety/alarm_binding_port.h"
#include "ports/hal/hal_io_port.h"
#include "config/machine/m8_alarm_table.h"
#include "common/log.h"
#include <stdbool.h>

/* -------------------------------------------------------------------------
 * 编译期推导报警源数量
 * ------------------------------------------------------------------------- */
typedef enum
{
#define X(pin, al, tr, rl, maj, idx, nat, lvl, clr, desc) ALARM_IDX_##pin,
    M8_HW_ALARM_TABLE(X)
#undef X
    M8_HW_ALARM_COUNT
} m8_alarm_idx_t;

/* -------------------------------------------------------------------------
 * 静态配置（pin / 极性 / 防抖参数 / 报警码，供 poll 使用）
 * ------------------------------------------------------------------------- */
typedef struct
{
    io_di_t  pin;
    bool     active_low;
    uint8_t  trig_max;   /**< 触发确认次数 */
    uint8_t  rel_max;    /**< 释放确认次数 */
    uint32_t code;
} alarm_src_cfg_t;

static const alarm_src_cfg_t s_cfg[] = {
#define X(pin, al, tr, rl, maj, idx, nat, lvl, clr, desc) \
    { (pin), (al), (tr), (rl), ALARM_CODE_MAKE(maj, idx, nat) },
    M8_HW_ALARM_TABLE(X)
#undef X
};

/* -------------------------------------------------------------------------
 * 每路独立滤波运行时状态
 * ------------------------------------------------------------------------- */
typedef struct
{
    uint8_t trig_cnt; /**< 触发计数器 */
    uint8_t rel_cnt;  /**< 释放计数器 */
    bool    active;   /**< 当前防抖后激活态（边沿检测基准）*/
} alarm_filter_t;

static alarm_filter_t s_filter[M8_HW_ALARM_COUNT];

/* -------------------------------------------------------------------------
 * 接口实现
 * ------------------------------------------------------------------------- */

sw_err_t m8_alarm_adapt_init(void)
{
    /* 目录由 m8_alarm_init() 已注入，此处只做 DI 防抖预热：
     * 读取当前 DI 值直接填满计数器，消除上电边沿误触发 */
    const hal_io_ops_t *io = hal_io_get_ops();
    for (int i = 0; i < (int)M8_HW_ALARM_COUNT; ++i)
    {
        bool raw = ((io != NULL) && (io->di_read != NULL))
                   ? (io->di_read(s_cfg[i].pin) ^ s_cfg[i].active_low)
                   : false;
        if (raw)
        {
            s_filter[i].trig_cnt = s_cfg[i].trig_max;
            s_filter[i].rel_cnt  = 0U;
            s_filter[i].active   = true;
        }
        else
        {
            s_filter[i].trig_cnt = 0U;
            s_filter[i].rel_cnt  = s_cfg[i].rel_max;
            s_filter[i].active   = false;
        }
    }

    LOG_INFO("m8_alarm_adapt: init ok, sources=%d", (int)M8_HW_ALARM_COUNT);
    return SW_OK;
}

void m8_alarm_adapt_poll(void)
{
    const alarm_binding_ops_t *ops = alarm_binding_get_ops();
    const hal_io_ops_t        *io  = hal_io_get_ops();

    if ((ops == NULL) || (io == NULL) || (io->di_read == NULL))
    {
        return;
    }

    for (int i = 0; i < (int)M8_HW_ALARM_COUNT; ++i)
    {
        /* 读原始 DI，处理极性 */
        bool raw = io->di_read(s_cfg[i].pin) ^ s_cfg[i].active_low;

        /* 独立防抖：触发/释放计数器互斥递增 */
        alarm_filter_t *f = &s_filter[i];
        if (raw)
        {
            if (f->trig_cnt < s_cfg[i].trig_max) { f->trig_cnt++; }
            f->rel_cnt = 0U;
        }
        else
        {
            if (f->rel_cnt < s_cfg[i].rel_max) { f->rel_cnt++; }
            f->trig_cnt = 0U;
        }

        bool debounced = (f->trig_cnt >= s_cfg[i].trig_max);

        if (debounced == f->active) { continue; } /* 无边沿，跳过 */

        f->active = debounced;
        if (debounced) { (void)ops->trigger(s_cfg[i].code); }
        else           { (void)ops->clear(s_cfg[i].code);   }
    }
}
