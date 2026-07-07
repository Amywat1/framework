/**
 * @file    m8_io_adapter.c
 * @brief   数字 IO HAL 端口 M8 机型绑定（引脚名称表 + 注册 snack_io_adapter）
 * @author  HUWANGWEI
 * @date    2026-07-07
 */

#include "framework/adapters/outbound/hal/providers/snack/io_exp/snack_io_adapter.h"
#include "projects/m8/config/m8_machine_config.h"
#include "projects/m8/config/m8_io_table.h"

/* -------------------------------------------------------------------------
 * M8 IO 名称映射表（通过 X-macro 展开 m8_io_table.h 生成）
 * ------------------------------------------------------------------------- */
static const drv_io_name_entry_t s_di_table[] = {
#define DRV_IO_DI_DEF(name, board, pin, desc) \
    { "DI_" #name, IO_HANDLE_MAKE(IO_KIND_DI, board, pin) },
#include "projects/m8/config/m8_io_table.h"
#undef DRV_IO_DI_DEF
};

static const drv_io_name_entry_t s_do_table[] = {
#define DRV_IO_DO_DEF(name, board, pin, desc) \
    { "DO_" #name, IO_HANDLE_MAKE(IO_KIND_DO, board, pin) },
#include "projects/m8/config/m8_io_table.h"
#undef DRV_IO_DO_DEF
};

void m8_io_adapter_register(void)
{
    const drv_io_cfg_t cfg = {
        .board_count = CFG_IO_BOARD_COUNT,
        .pin_count   = CFG_IO_PIN_COUNT,
        .di_table    = s_di_table,
        .di_count    = sizeof(s_di_table) / sizeof(s_di_table[0]),
        .do_table    = s_do_table,
        .do_count    = sizeof(s_do_table) / sizeof(s_do_table[0]),
    };
    snack_io_adapter_register(&cfg);
}
