/**
 * @file    m8_alarm_adapt.c
 * @brief   M8 机型报警绑定适配实现
 * @author  HUWANGWEI
 * @date    2026-06-26
 */

#include "adapters/machine/m8/m8_alarm_adapt.h"
#include "ports/safety/alarm_binding_port.h"
#include "ports/hal/hal_io_port.h"
#include "domain/model/alarm_code.h"
#include "config/machine/m8_io_pins.h"
#include "common/log.h"
#include <stdbool.h>

/* -------------------------------------------------------------------------
 * 硬件 DI 报警总表（增删改只改此处，一行对应一个报警源）
 *
 * 列：pin          active_low  trig_max  rel_max
 *     大类  编号  性质  等级                   清除方式                 描述
 * ------------------------------------------------------------------------- */
#define M8_HW_ALARM_TABLE(X) \
    X(M8_IO_DI_ESTOP,               true,  1U, 3U, 2, 17, 9, ALARM_LEVEL_CRITICAL, ALARM_CLEAR_AUTO_STATIC, "急停按钮触发") \
    X(M8_IO_DI_SIDE_BRUSH_OVERLOAD, false, 3U, 3U, 2, 11, 1, ALARM_LEVEL_MAJOR,    ALARM_CLEAR_LATCHED,     "侧刷电机过载") \
    X(M8_IO_DI_FAN_ALARM,           false, 3U, 3U, 4,  3, 3, ALARM_LEVEL_MINOR,    ALARM_CLEAR_AUTO_STATIC, "风机报警反馈")

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
 * 报警定义表（传给 alarm_binding_port.load_catalog，替换兜底目录）
 * ------------------------------------------------------------------------- */
static const alarm_def_t s_alarm_defs[] = {
#define X(pin, al, tr, rl, maj, idx, nat, lvl, clr, desc) \
    { ALARM_CODE_MAKE(maj, idx, nat), (lvl), (clr), (desc) },
    M8_HW_ALARM_TABLE(X)
#undef X
};

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
    const alarm_binding_ops_t *ops = alarm_binding_get_ops();

    if (ops == NULL)
    {
        LOG_ERROR("m8_alarm_adapt: alarm_binding_port not registered");
        return SW_ERR_NOT_INIT;
    }

    /* 向 alarm_core 注册本机型完整报警目录，替换内置兜底目录 */
    sw_err_t r = ops->load_catalog(s_alarm_defs, (unsigned)M8_HW_ALARM_COUNT);
    if (r != SW_OK)
    {
        LOG_ERROR("m8_alarm_adapt: load_catalog failed ret=%d", (int)r);
        return r;
    }

    /* 预热滤波：读取当前 DI 值直接填满计数器，消除上电延迟 */
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
