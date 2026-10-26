/**
 * @file    test_lifetime.c
 * @brief   保养通道表：累秒、边沿次数与落盘
 */

#include "application/lifetime.h"
#include "common/log.h"
#include "common/sw_error.h"
#include "common/sw_mutex.h"
#include "common/time_util.h"
#include "domain/ports/outbound/storage/param_kv.h"
#include "runtime/scheduler/periodic_task.h"
#include "wdf_test_spec.h"

#include <pthread.h>
#include <string.h>

typedef struct {
    const char *key;
    int         value;
} kv_entry_t;

static kv_entry_t s_kv[16];
static unsigned   s_kv_count;
static unsigned   s_kv_save_count;
static bool       s_run;
static bool       s_edge;

static kv_entry_t *kv_find(const char *key)
{
    unsigned i;

    for (i = 0U; i < s_kv_count; i++) {
        if (strcmp(s_kv[i].key, key) == 0) {
            return &s_kv[i];
        }
    }
    return NULL;
}

int param_kv_get_int(const char *key, int default_val)
{
    const kv_entry_t *entry = kv_find(key);

    if (entry == NULL) {
        return default_val;
    }
    return entry->value;
}

sw_err_t param_kv_set_int(const char *key, int val)
{
    kv_entry_t *entry = kv_find(key);

    if (entry != NULL) {
        entry->value = val;
        return SW_OK;
    }
    if (s_kv_count >= (sizeof(s_kv) / sizeof(s_kv[0]))) {
        return SW_ERR_OVERFLOW;
    }
    s_kv[s_kv_count].key   = key;
    s_kv[s_kv_count].value = val;
    s_kv_count++;
    return SW_OK;
}

sw_err_t param_kv_save(void)
{
    s_kv_save_count++;
    return SW_OK;
}

static bool run_is_active(void)
{
    return s_run;
}

static bool edge_is_active(void)
{
    return s_edge;
}

static const lifetime_channel_t s_channels[] = {
    { "runSeconds", run_is_active, false },
    { "edgeCounts", edge_is_active, true },
};

uint64_t time_util_get_ms(void)
{
    return 0U;
}

sw_err_t periodic_task_register(const char        *name,
                                uint32_t           period_ms,
                                periodic_task_fn_t fn,
                                void              *ctx,
                                int                policy,
                                int                prio,
                                size_t             stack)
{
    (void)name;
    (void)period_ms;
    (void)fn;
    (void)ctx;
    (void)policy;
    (void)prio;
    (void)stack;
    return SW_OK;
}

bool sw_mutex_init_prio_inherit(pthread_mutex_t *mutex)
{
    return pthread_mutex_init(mutex, NULL) == 0;
}

void sw_log_write(sw_log_level_t level, const char *source, const char *format, ...)
{
    (void)level;
    (void)source;
    (void)format;
}

const char *sw_log_source_file_name(const char *source_path)
{
    return source_path;
}

static sw_err_t bind_and_init(void)
{
    sw_err_t ret = lifetime_bind(s_channels, 2U);

    if (ret != SW_OK) {
        return ret;
    }
    return lifetime_init();
}

void setUp(void)
{
    memset(s_kv, 0, sizeof(s_kv));
    s_kv_count      = 0U;
    s_kv_save_count = 0U;
    s_run           = false;
    s_edge          = false;
    lifetime_reset_for_test();
}

void tearDown(void)
{
}

static void test_running_adds_elapsed_seconds(void)
{
    int32_t value = -1;

    TEST_ASSERT_EQUAL_INT(SW_OK, bind_and_init());
    s_run = true;
    lifetime_poll(0U);
    lifetime_poll(1500U);
    TEST_ASSERT_EQUAL_INT(SW_OK, lifetime_get_int("runSeconds", &value));
    TEST_ASSERT_EQUAL_INT32(1, value);
    TEST_ASSERT_GREATER_THAN_UINT(0U, s_kv_save_count);
    TEST_ASSERT_EQUAL_INT(1, param_kv_get_int("runSeconds", -1));
}

static void test_idle_does_not_accumulate(void)
{
    int32_t run_s = -1;
    int32_t edges = -1;

    TEST_ASSERT_EQUAL_INT(SW_OK, bind_and_init());
    lifetime_poll(0U);
    lifetime_poll(5000U);
    TEST_ASSERT_EQUAL_INT(SW_OK, lifetime_get_int("runSeconds", &run_s));
    TEST_ASSERT_EQUAL_INT(SW_OK, lifetime_get_int("edgeCounts", &edges));
    TEST_ASSERT_EQUAL_INT32(0, run_s);
    TEST_ASSERT_EQUAL_INT32(0, edges);
    TEST_ASSERT_EQUAL_UINT(0U, s_kv_save_count);
}

static void test_rising_edge_counts_once(void)
{
    int32_t edges = -1;

    TEST_ASSERT_EQUAL_INT(SW_OK, bind_and_init());
    lifetime_poll(0U);
    s_edge = true;
    lifetime_poll(100U);
    lifetime_poll(200U);
    TEST_ASSERT_EQUAL_INT(SW_OK, lifetime_get_int("edgeCounts", &edges));
    TEST_ASSERT_EQUAL_INT32(1, edges);
}

static void test_first_sample_does_not_count_edge(void)
{
    int32_t edges = -1;

    s_edge = true;
    TEST_ASSERT_EQUAL_INT(SW_OK, bind_and_init());
    lifetime_poll(0U);
    lifetime_poll(100U);
    TEST_ASSERT_EQUAL_INT(SW_OK, lifetime_get_int("edgeCounts", &edges));
    TEST_ASSERT_EQUAL_INT32(0, edges);
}

static void test_persisted_values_reload(void)
{
    int32_t run_s = -1;
    int32_t edges = -1;

    TEST_ASSERT_EQUAL_INT(SW_OK, param_kv_set_int("runSeconds", 12));
    TEST_ASSERT_EQUAL_INT(SW_OK, param_kv_set_int("edgeCounts", 7));
    TEST_ASSERT_EQUAL_INT(SW_OK, bind_and_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, lifetime_get_int("runSeconds", &run_s));
    TEST_ASSERT_EQUAL_INT(SW_OK, lifetime_get_int("edgeCounts", &edges));
    TEST_ASSERT_EQUAL_INT32(12, run_s);
    TEST_ASSERT_EQUAL_INT32(7, edges);
}

static void test_duplicate_key_is_rejected(void)
{
    static const lifetime_channel_t dup[] = {
        { "runSeconds", run_is_active, false },
        { "runSeconds", edge_is_active, true },
    };

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, lifetime_bind(dup, 2U));
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_running_adds_elapsed_seconds, "LIFE-01",
                 "在转通道按经过时间累加秒并落盘");
    WDF_RUN_TEST(test_idle_does_not_accumulate, "LIFE-02",
                 "未激活通道不加时间和次数");
    WDF_RUN_TEST(test_rising_edge_counts_once, "LIFE-03",
                 "边沿通道上升沿加一次，持续有效不再加");
    WDF_RUN_TEST(test_first_sample_does_not_count_edge, "LIFE-04",
                 "首拍已为真不加次数");
    WDF_RUN_TEST(test_persisted_values_reload, "LIFE-05",
                 "秒与次数从 param_kv 恢复");
    WDF_RUN_TEST(test_duplicate_key_is_rejected, "LIFE-01",
                 "重复键拒绝登记");
    return UNITY_END();
}
