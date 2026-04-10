/**
 * @file    drv_io.c
 * @brief   CAN IO 子板驱动实现（后台轮询线程、在线检测、输入变化通知）
 * @author  HUWANGWEI
 * @date    2026-04-07
 *
 * @note    IO 地址编码：io_id = board_id × 100 + pin（pin 从 1 开始）
 *          后台线程每 CFG_IO_UPDATE_FREQ_MS ms 刷新一次输入/输出缓冲；
 *          连续 CFG_IO_OFFLINE_CNT 次无响应后触发掉线回调。
 */

#include "drv_io.h"
#include "common/log.h"
#include "config/machine_config.h"
#include "io_exp/slave.h"
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

/* -------------------------------------------------------------------------
 * 内部常量
 * ------------------------------------------------------------------------- */
#define IO_BOARD_MAX    7U      /* 支持的最大子板数（含 ID=0 占位）*/
#define IO_PIN_MAX      32U     /* 每块子板最多引脚数 */

/* -------------------------------------------------------------------------
 * 内部状态
 * ------------------------------------------------------------------------- */
static volatile unsigned int  s_input_buf[IO_BOARD_MAX]   = {0};
static volatile unsigned int  s_output_buf[IO_BOARD_MAX]  = {0};
static volatile bool          s_board_online[IO_BOARD_MAX] = {0};
static pthread_mutex_t        s_output_mutex = PTHREAD_MUTEX_INITIALIZER;

/* 测试覆盖（调试用） */
static bool s_test_enable[IO_BOARD_MAX][IO_PIN_MAX] = {{false}};
static bool s_test_value[IO_BOARD_MAX][IO_PIN_MAX]  = {{false}};

/* 回调 */
static void (*s_input_cb)(int io_id, bool state)    = NULL;
static void (*s_board_error_cb)(int board_id, bool offline) = NULL;

/* -------------------------------------------------------------------------
 * 后台读写线程
 * 平台在子板配置超过4块时，只能使用sdo，未避免频繁读写IO造成阻塞，通过此处线程定时读写各板子IO，业务读写IO值均为临时存储值
 * ------------------------------------------------------------------------- */
static void *io_rw_thread(void *arg)
{
    uint16_t loop_cnt  = 0U;
    uint8_t  offline_cnt[IO_BOARD_MAX] = {0U};
    bool     is_board_check_offline[IO_BOARD_MAX] = {false};
    int      check_interval_ms = CFG_IO_CHECK_OFFLINE_MS;

    (void)arg;
    /* 各板初始为离线状态，首次探测成功后才置在线（与 m8_boot_profile 轮询配合）*/

    while (1) {
        loop_cnt++;
        bool all_offline = true;
        bool all_online  = true;

        for (int i = 1; i <= CFG_IO_BOARD_COUNT; i++) {
            /* ---- 在线检测（每 check_interval_ms 触发一次）---- */
            int check_loops = check_interval_ms / CFG_IO_UPDATE_FREQ_MS;
            if (check_loops < 1) {
                check_loops = 1;
            }

            if ((loop_cnt % (uint16_t)check_loops) == 0U) {
                if (io_online_get(i) > 0) {
                    /* 探测成功：重置计数，首次/恢复时置在线并回调 */
                    offline_cnt[i] = 0U;
                    is_board_check_offline[i] = false;
                    if (!s_board_online[i]) {
                        s_board_online[i] = true;
                        if (s_board_error_cb != NULL) {
                            s_board_error_cb(i, false); /* false = 恢复在线 */
                        }
                        LOG_INFO("drv_io: board %d online", i);
                    }
                } else {
                    /* 探测失败：计数，达到阈值后确认掉线 */
                    if (offline_cnt[i] < (uint8_t)CFG_IO_OFFLINE_CNT) {
                        offline_cnt[i]++;
                        if (offline_cnt[i] >= (uint8_t)CFG_IO_OFFLINE_CNT) {
                            s_board_online[i] = false;
                            is_board_check_offline[i] = true;
                            if (s_board_error_cb != NULL) {
                                s_board_error_cb(i, true);
                            }
                            LOG_ERROR("drv_io: board %d offline", i);
                        }
                    }
                }
            }

            /* ---- 数据读写（仅在线子板参与）---- */
            if (s_board_online[i]) {
                unsigned int prev = s_input_buf[i];
                s_input_buf[i] = (unsigned int)io_read_input_s(i);  //sdo读写一次约1~2ms时间（读取失败时返回值还是之前的值）
                pthread_mutex_lock(&s_output_mutex);
                unsigned int out_val = s_output_buf[i];
                pthread_mutex_unlock(&s_output_mutex);
                io_write_all_s(i, (int)out_val);

                /* 检测输入变化，逐位通知 */
                unsigned int changed = prev ^ s_input_buf[i];
                if ((changed != 0U) && (s_input_cb != NULL)) {
                    for (int bit = 0; bit < (int)IO_PIN_MAX; bit++) {
                        if ((changed >> bit) & 1U) {
                            int  io_id    = i * DRV_IO_BOARD_VAL + (bit + 1);
                            bool new_state = (bool)((s_input_buf[i] >> bit) & 1U);
                            s_input_cb(io_id, new_state);
                        }
                    }
                }

                all_offline = false;
            }

            if (!is_board_check_offline[i]) {
                all_offline = false;
            } else if (!s_board_online[i]) {
                all_online = false;
            }

            usleep((unsigned int)(CFG_IO_UPDATE_FREQ_MS * 1000) /
                   (unsigned int)CFG_IO_BOARD_COUNT);
        }

        /* 全部子板掉线：等待后重启进程 */
        if (all_offline) {
            LOG_ERROR("drv_io: all boards offline, restarting in 30s");
            sleep(10);
            for (int i = 1; i <= CFG_IO_BOARD_COUNT; i++) {
                if (s_board_error_cb != NULL) {
                    s_board_error_cb(i, true);
                }
            }
            sleep(20);
            system("killall ./M8 && sleep 5 && ./M8");
        }

        check_interval_ms = all_online ? CFG_IO_CHECK_OFFLINE_MS
                                       : CFG_IO_CHECK_ONLINE_MS;
    }

    return NULL;
}

/* -------------------------------------------------------------------------
 * 接口实现
 * ------------------------------------------------------------------------- */
sw_err_t drv_io_init(void)
{
    memset(s_input_buf,           0, sizeof(s_input_buf));
    memset(s_output_buf,          0, sizeof(s_output_buf));
    memset(s_board_online,        0, sizeof(s_board_online));
    memset(s_test_enable,         0, sizeof(s_test_enable));
    memset(s_test_value,          0, sizeof(s_test_value));

    pthread_t tid;
    if (pthread_create(&tid, NULL, io_rw_thread, NULL) != 0) {
        LOG_ERROR("drv_io_init: failed to create rw thread");
        return SW_ERR_HW;
    }
    pthread_detach(tid);

    LOG_INFO("drv_io init ok, board_count=%d", CFG_IO_BOARD_COUNT);
    return SW_OK;
}

sw_err_t drv_io_do_set(drv_io_do_t pin, bool val)
{
    int board_id = DRV_IO_BOARD_ID((int)pin);
    int pin_id   = DRV_IO_PIN_ID((int)pin);

    if ((board_id <= 0) || (board_id >= (int)IO_BOARD_MAX) ||
        (pin_id  <= 0) || (pin_id  >= (int)IO_PIN_MAX)) {
        LOG_ERROR("drv_io_do_set: invalid pin %d", (int)pin);
        return SW_ERR_PARAM;
    }

    int bit = pin_id - 1;               //引脚id从1开始
    pthread_mutex_lock(&s_output_mutex);
    if (val) {
        s_output_buf[board_id] |=  (1U << bit);
    } else {
        s_output_buf[board_id] &= ~(1U << bit);
    }
    pthread_mutex_unlock(&s_output_mutex);

    return SW_OK;
}

bool drv_io_di_read(drv_io_di_t pin)
{
    int board_id = DRV_IO_BOARD_ID((int)pin);
    int pin_id   = DRV_IO_PIN_ID((int)pin);

    if ((board_id <= 0) || (board_id >= (int)IO_BOARD_MAX) ||
        (pin_id  <= 0) || (pin_id  >= (int)IO_PIN_MAX)) {
        return false;
    }

    /* 测试覆盖优先 */
    if (s_test_enable[board_id][pin_id]) {
        return s_test_value[board_id][pin_id];
    }

    return (bool)((s_input_buf[board_id] >> (pin_id - 1)) & 1U);
}

void drv_io_register_input_cb(void (*cb)(int io_id, bool state))
{
    s_input_cb = cb;
}

bool drv_io_board_is_online(int board_id)
{
    if ((board_id <= 0) || (board_id >= (int)IO_BOARD_MAX)) {
        return false;
    }
    return s_board_online[board_id];
}

void drv_io_register_board_error_cb(void (*cb)(int board_id, bool offline))
{
    s_board_error_cb = cb;
}

void drv_io_set_test_override(int io_id, int value)
{
    int board_id = DRV_IO_BOARD_ID(io_id);
    int pin_id   = DRV_IO_PIN_ID(io_id);

    if ((board_id <= 0) || (board_id >= (int)IO_BOARD_MAX) ||
        (pin_id  <= 0) || (pin_id  >= (int)IO_PIN_MAX)) {
        return;
    }

    if (value == 0 || value == 1) {
        s_test_value[board_id][pin_id]  = (bool)value;
        s_test_enable[board_id][pin_id] = true;
    } else {
        s_test_enable[board_id][pin_id] = false;
    }
}

void drv_io_clear_test_override(int io_id)
{
    int board_id = DRV_IO_BOARD_ID(io_id);
    int pin_id   = DRV_IO_PIN_ID(io_id);

    if ((board_id <= 0) || (board_id >= (int)IO_BOARD_MAX) ||
        (pin_id  <= 0) || (pin_id  >= (int)IO_PIN_MAX)) {
        return;
    }

    s_test_enable[board_id][pin_id] = false;
}
