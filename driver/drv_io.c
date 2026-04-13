/**
 * @file    drv_io.c
 * @brief   CAN IO 子板驱动实现
 * @author  HUWANGWEI
 * @date    2026-04-07
 *
 * @note    本文件负责：
 *          - IO 子板在线检测
 *          - 输入缓存刷新
 *          - 输出缓冲写出
 *          - 输入变化调试通知
 *          - 全板离线时的安全停机联动
 */

#include "drv_io.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common/log.h"
#include "config/machine/m8_machine_config.h"
#include "io_exp/slave.h"

/* -------------------------------------------------------------------------
 * 内部常量
 * ------------------------------------------------------------------------- */
#define IO_BOARD_MAX    7U      /* 最大子板数，含 0 号占位 */
#define IO_PIN_COUNT    32U     /* 每块子板最多 32 个 IO 点 */

typedef struct
{
    const char *name;
    uint16_t    raw;
} drv_io_name_entry_t;

/* -------------------------------------------------------------------------
 * 通过总表生成名称映射
 * ------------------------------------------------------------------------- */
static const drv_io_name_entry_t s_di_name_table[] = {
#define DRV_IO_DI_DEF(name, board, pin, desc) { "DI_" #name, DRV_IO_HANDLE_MAKE(DRV_IO_KIND_DI, board, pin) },
#include "driver/drv_io_def.h"
#undef DRV_IO_DI_DEF
};

static const drv_io_name_entry_t s_do_name_table[] = {
#define DRV_IO_DO_DEF(name, board, pin, desc) { "DO_" #name, DRV_IO_HANDLE_MAKE(DRV_IO_KIND_DO, board, pin) },
#include "driver/drv_io_def.h"
#undef DRV_IO_DO_DEF
};

/* -------------------------------------------------------------------------
 * 内部状态
 * ------------------------------------------------------------------------- */
static volatile unsigned int  s_input_buf[IO_BOARD_MAX]    = {0};
static volatile unsigned int  s_output_buf[IO_BOARD_MAX]   = {0};
static volatile bool          s_board_online[IO_BOARD_MAX] = {0};
static pthread_mutex_t        s_output_mutex               = PTHREAD_MUTEX_INITIALIZER;

/* 调试测试覆盖：索引直接使用 pin_id，因此第二维保留 0 号位不用 */
static bool s_test_enable[IO_BOARD_MAX][IO_PIN_COUNT + 1U] = {{false}};
static bool s_test_value[IO_BOARD_MAX][IO_PIN_COUNT + 1U]  = {{false}};

/* 调试 / 状态回调 */
static drv_io_debug_input_cb_t s_debug_input_cb = NULL;
static void (*s_board_error_cb)(int board_id, bool offline) = NULL;
static void (*s_panic_cb)(void) = NULL;

/* -------------------------------------------------------------------------
 * 内部辅助
 * ------------------------------------------------------------------------- */
static bool drv_io_is_valid_di_raw(uint16_t raw)
{
    int board_id = (int)drv_io_handle_board(raw);
    int pin_id   = (int)drv_io_handle_pin(raw);

    return (drv_io_handle_kind(raw) == DRV_IO_KIND_DI)
        && (board_id > 0)
        && (board_id < (int)IO_BOARD_MAX)
        && (pin_id > 0)
        && (pin_id <= (int)IO_PIN_COUNT);
}

static bool drv_io_is_valid_do_raw(uint16_t raw)
{
    int board_id = (int)drv_io_handle_board(raw);
    int pin_id   = (int)drv_io_handle_pin(raw);

    return (drv_io_handle_kind(raw) == DRV_IO_KIND_DO)
        && (board_id > 0)
        && (board_id < (int)IO_BOARD_MAX)
        && (pin_id > 0)
        && (pin_id <= (int)IO_PIN_COUNT);
}

static const char *drv_io_strip_machine_prefix(const char *name)
{
    if (name == NULL)
    {
        return NULL;
    }

    if (strncmp(name, "M8_", 3) == 0)
    {
        return name + 3;
    }

    return name;
}

static bool drv_io_name_matches(const char *input, const char *canonical)
{
    const char *name = drv_io_strip_machine_prefix(input);

    if ((name == NULL) || (canonical == NULL))
    {
        return false;
    }

    if (strcmp(name, canonical) == 0)
    {
        return true;
    }

    /* 允许省略 DI_/DO_ 前缀，只输入核心名字 */
    return strcmp(name, canonical + 3) == 0;
}

static bool drv_io_find_name(const drv_io_name_entry_t *table,
                             size_t                     table_size,
                             const char                *name,
                             uint16_t                  *out_raw)
{
    if ((table == NULL) || (name == NULL) || (out_raw == NULL))
    {
        return false;
    }

    for (size_t i = 0; i < table_size; ++i)
    {
        if (drv_io_name_matches(name, table[i].name))
        {
            *out_raw = table[i].raw;
            return true;
        }
    }

    return false;
}

static const char *drv_io_find_canonical_name(const drv_io_name_entry_t *table,
                                              size_t                     table_size,
                                              uint16_t                   raw)
{
    if (table == NULL)
    {
        return NULL;
    }

    for (size_t i = 0; i < table_size; ++i)
    {
        if (table[i].raw == raw)
        {
            return table[i].name;
        }
    }

    return NULL;
}

/* -------------------------------------------------------------------------
 * 名称解析 / 可读名称
 * ------------------------------------------------------------------------- */
bool drv_io_try_parse_di(const char *name, drv_io_di_t *out)
{
    uint16_t raw = DRV_IO_NULL;

    if (!drv_io_find_name(s_di_name_table,
                          sizeof(s_di_name_table) / sizeof(s_di_name_table[0]),
                          name,
                          &raw))
    {
        return false;
    }

    if (out != NULL)
    {
        out->raw = raw;
    }

    return true;
}

bool drv_io_try_parse_do(const char *name, drv_io_do_t *out)
{
    uint16_t raw = DRV_IO_NULL;

    if (!drv_io_find_name(s_do_name_table,
                          sizeof(s_do_name_table) / sizeof(s_do_name_table[0]),
                          name,
                          &raw))
    {
        return false;
    }

    if (out != NULL)
    {
        out->raw = raw;
    }

    return true;
}

const char *drv_io_di_name(drv_io_di_t pin)
{
    return drv_io_find_canonical_name(s_di_name_table,
                                      sizeof(s_di_name_table) / sizeof(s_di_name_table[0]),
                                      drv_io_di_raw(pin));
}

const char *drv_io_do_name(drv_io_do_t pin)
{
    return drv_io_find_canonical_name(s_do_name_table,
                                      sizeof(s_do_name_table) / sizeof(s_do_name_table[0]),
                                      drv_io_do_raw(pin));
}

/* -------------------------------------------------------------------------
 * IO 轮询线程
 * 线程由 scheduler 统一创建，本文件只提供线程入口，不再在 drv_io_init() 内部自建线程。
 * ------------------------------------------------------------------------- */
void *drv_io_poll_loop(void *arg)
{
    uint16_t loop_cnt = 0U;
    uint8_t  offline_cnt[IO_BOARD_MAX]              = {0U};
    bool     offline_confirmed[IO_BOARD_MAX]        = {false};
    int      check_interval_ms                      = CFG_IO_CHECK_OFFLINE_MS;

    (void)arg;

    while (1)
    {
        int online_count            = 0;
        int offline_confirmed_count = 0;

        ++loop_cnt;

        for (int i = 1; i <= CFG_IO_BOARD_COUNT; ++i)
        {
            int check_loops = check_interval_ms / CFG_IO_UPDATE_FREQ_MS;
            if (check_loops < 1)
            {
                check_loops = 1;
            }

            /* ---- 在线检测：每隔指定周期探测一次 ---- */
            if ((loop_cnt % (uint16_t)check_loops) == 0U)
            {
                if (io_online_get(i) > 0)
                {
                    offline_cnt[i]       = 0U;
                    offline_confirmed[i] = false;

                    if (!s_board_online[i])
                    {
                        s_board_online[i] = true;
                        if (s_board_error_cb != NULL)
                        {
                            s_board_error_cb(i, false);
                        }
                        LOG_INFO("drv_io: board %d online", i);
                    }
                }
                else
                {
                    if (offline_cnt[i] < (uint8_t)CFG_IO_OFFLINE_CNT)
                    {
                        ++offline_cnt[i];
                        if (offline_cnt[i] >= (uint8_t)CFG_IO_OFFLINE_CNT)
                        {
                            s_board_online[i]    = false;
                            offline_confirmed[i] = true;

                            if (s_board_error_cb != NULL)
                            {
                                s_board_error_cb(i, true);
                            }
                            LOG_ERROR("drv_io: board %d offline", i);
                        }
                    }
                }
            }

            /* ---- 仅在线子板参与读写 ---- */
            if (s_board_online[i])
            {
                unsigned int prev    = s_input_buf[i];
                unsigned int out_val = 0U;

                /* SDO 读写一次约 1~2ms；读失败时 SDK 会保留上次值 */
                s_input_buf[i] = (unsigned int)io_read_input_s(i);

                pthread_mutex_lock(&s_output_mutex);
                out_val = s_output_buf[i];
                pthread_mutex_unlock(&s_output_mutex);

                io_write_all_s(i, (int)out_val);

                /* 输入变化仅用于调试观察，不参与正式业务判断 */
                if (s_debug_input_cb != NULL)
                {
                    unsigned int changed = prev ^ s_input_buf[i];

                    if (changed != 0U)
                    {
                        for (int bit = 0; bit < (int)IO_PIN_COUNT; ++bit)
                        {
                            if (((changed >> bit) & 1U) != 0U)
                            {
                                drv_io_di_t pin      = drv_io_di_make((uint16_t)i, (uint16_t)(bit + 1));
                                bool        new_state = (bool)((s_input_buf[i] >> bit) & 1U);
                                s_debug_input_cb(pin, new_state);
                            }
                        }
                    }
                }
            }

            if (s_board_online[i])
            {
                ++online_count;
            }
            else if (offline_confirmed[i])
            {
                ++offline_confirmed_count;
            }

            usleep((unsigned int)(CFG_IO_UPDATE_FREQ_MS * 1000) / (unsigned int)CFG_IO_BOARD_COUNT);
        }

        /* 全板离线：先尽力置安全态，再交给 systemd 拉起进程 */
        if (offline_confirmed_count == CFG_IO_BOARD_COUNT)
        {
            LOG_ERROR("drv_io: all boards offline, safe stop and abort");

            for (int i = 1; i <= CFG_IO_BOARD_COUNT; ++i)
            {
                if (s_board_error_cb != NULL)
                {
                    s_board_error_cb(i, true);
                }
            }

            if (s_panic_cb != NULL)
            {
                s_panic_cb();
            }

            (void)drv_io_flush_outputs_now();
            sleep(2);
            abort();
        }

        check_interval_ms = (online_count == CFG_IO_BOARD_COUNT)
                          ? CFG_IO_CHECK_OFFLINE_MS
                          : CFG_IO_CHECK_ONLINE_MS;
    }
}

/* -------------------------------------------------------------------------
 * 接口实现
 * ------------------------------------------------------------------------- */
sw_err_t drv_io_init(void)
{
    memset((void *)s_input_buf,     0, sizeof(s_input_buf));
    memset((void *)s_output_buf,    0, sizeof(s_output_buf));
    memset((void *)s_board_online,  0, sizeof(s_board_online));
    memset(s_test_enable,           0, sizeof(s_test_enable));
    memset(s_test_value,            0, sizeof(s_test_value));

    /* 仅在系统启动阶段调用：这里会清空已注册回调，不作为运行期 reset 接口使用。 */
    s_debug_input_cb = NULL;
    s_board_error_cb = NULL;
    s_panic_cb       = NULL;

    LOG_INFO("drv_io init ok, board_count=%d", CFG_IO_BOARD_COUNT);
    return SW_OK;
}

sw_err_t drv_io_do_set(drv_io_do_t pin, bool val)
{
    uint16_t raw      = drv_io_do_raw(pin);
    int      board_id = (int)drv_io_handle_board(raw);
    int      pin_id   = (int)drv_io_handle_pin(raw);
    int      bit      = pin_id - 1;

    if (!drv_io_is_valid_do_raw(raw))
    {
        LOG_ERROR("drv_io_do_set: invalid DO raw=0x%04X", (unsigned)raw);
        return SW_ERR_PARAM;
    }

    pthread_mutex_lock(&s_output_mutex);
    if (val)
    {
        s_output_buf[board_id] |= (1U << bit);
    }
    else
    {
        s_output_buf[board_id] &= ~(1U << bit);
    }
    pthread_mutex_unlock(&s_output_mutex);

    return SW_OK;
}

sw_err_t drv_io_flush_outputs_now(void)
{
    unsigned int snapshot[IO_BOARD_MAX] = {0U};
    bool         online[IO_BOARD_MAX]   = {false};

    /* 快照输出缓冲和在线状态，缩短持锁时间 */
    pthread_mutex_lock(&s_output_mutex);
    for (int i = 1; i <= CFG_IO_BOARD_COUNT; ++i)
    {
        snapshot[i] = s_output_buf[i];
        online[i]   = s_board_online[i];
    }
    pthread_mutex_unlock(&s_output_mutex);

    /* 只对在线子板执行写操作，避免 CAN 超时阻塞 */
    for (int i = 1; i <= CFG_IO_BOARD_COUNT; ++i)
    {
        if (online[i])
        {
            io_write_all_s(i, (int)snapshot[i]);
        }
    }

    return SW_OK;
}

bool drv_io_di_read(drv_io_di_t pin)
{
    uint16_t raw      = drv_io_di_raw(pin);
    int      board_id = (int)drv_io_handle_board(raw);
    int      pin_id   = (int)drv_io_handle_pin(raw);

    if (!drv_io_is_valid_di_raw(raw))
    {
        return false;
    }

    /* 测试覆盖优先 */
    if (s_test_enable[board_id][pin_id])
    {
        return s_test_value[board_id][pin_id];
    }

    return (bool)((s_input_buf[board_id] >> (pin_id - 1)) & 1U);
}

void drv_io_register_debug_input_cb(drv_io_debug_input_cb_t cb)
{
    s_debug_input_cb = cb;
}

bool drv_io_board_is_online(int board_id)
{
    if ((board_id <= 0) || (board_id >= (int)IO_BOARD_MAX))
    {
        return false;
    }

    return s_board_online[board_id];
}

void drv_io_register_board_error_cb(void (*cb)(int board_id, bool offline))
{
    s_board_error_cb = cb;
}

void drv_io_register_panic_cb(void (*cb)(void))
{
    s_panic_cb = cb;
}

void drv_io_set_test_override(drv_io_di_t pin, int value)
{
    uint16_t raw      = drv_io_di_raw(pin);
    int      board_id = (int)drv_io_handle_board(raw);
    int      pin_id   = (int)drv_io_handle_pin(raw);

    if (!drv_io_is_valid_di_raw(raw))
    {
        return;
    }

    if ((value == 0) || (value == 1))
    {
        s_test_value[board_id][pin_id]  = (bool)value;
        s_test_enable[board_id][pin_id] = true;
    }
    else
    {
        s_test_enable[board_id][pin_id] = false;
    }
}

void drv_io_clear_test_override(drv_io_di_t pin)
{
    uint16_t raw      = drv_io_di_raw(pin);
    int      board_id = (int)drv_io_handle_board(raw);
    int      pin_id   = (int)drv_io_handle_pin(raw);

    if (!drv_io_is_valid_di_raw(raw))
    {
        return;
    }

    s_test_enable[board_id][pin_id] = false;
}
