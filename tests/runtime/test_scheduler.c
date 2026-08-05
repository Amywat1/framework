/**
 * @file    test_scheduler.c
 * @brief   scheduler / thread_registry / periodic_task 单元测试
 *
 * @note    thread_registry 无 reset 接口，用例按注册顺序执行并累加计数。
 */

/* pthread_getname_np 是 glibc 扩展，需在任何头文件之前开启 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "common/sw_error.h"
#include "runtime/scheduler/periodic_task.h"
#include "runtime/scheduler/scheduler.h"
#include "runtime/scheduler/thread_registry.h"
#include "wdf_test_spec.h"

#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static volatile int s_one_shot_done;
static volatile int s_periodic_tick_count;

static void *one_shot_thread_fn(void *arg)
{
    (void)arg;
    s_one_shot_done = 1;
    return NULL;
}

/* -------------------------------------------------------------------------
 * OS 线程名探针
 *
 * scheduler_start_all() 返回时已对全部线程调用过 pthread_setname_np，
 * 但线程一经 detach 便无法从外部取得 pthread_t。故让线程自报 pthread_self()
 * 后驻留，主线程读名完毕再置 stop 令其退出，避免读取已退出线程的句柄。
 * ------------------------------------------------------------------------- */
typedef struct {
    pthread_t    tid;
    volatile int ready;
    volatile int stop;
} name_probe_t;

static void *name_probe_thread_fn(void *arg)
{
    name_probe_t *p = (name_probe_t *)arg;

    p->tid   = pthread_self();
    p->ready = 1;

    while (p->stop == 0) {
        usleep(1000U);
    }
    return NULL;
}

/* 等待探针线程登记 pthread_self()，最长约 1s */
static int name_probe_wait_ready(const name_probe_t *p)
{
    int i;

    for (i = 0; i < 1000; i++) {
        if (p->ready != 0) {
            return 1;
        }
        usleep(1000U);
    }
    return 0;
}

static void periodic_tick_fn(void *ctx)
{
    (void)ctx;
    s_periodic_tick_count++;
}

void setUp(void)
{
    /* 复位线程登记表，使各用例不依赖执行顺序 */
    thread_registry_reset_for_test();
}

void tearDown(void)
{
}

static void test_registry_count_initial_zero(void)
{
    TEST_ASSERT_EQUAL_INT(0, thread_registry_count());
}

static void test_register_rejects_null_name(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, thread_register(NULL, one_shot_thread_fn, SCHED_OTHER, 0, 4096U));
    TEST_ASSERT_EQUAL_INT(0, thread_registry_count());
}

static void test_register_rejects_null_fn(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, thread_register("bad", NULL, SCHED_OTHER, 0, 4096U));
    TEST_ASSERT_EQUAL_INT(0, thread_registry_count());
}

static void test_register_and_get_entry(void)
{
    sw_err_t ret;

    ret = thread_register("worker", one_shot_thread_fn, SCHED_OTHER, 0, 8192U);
    TEST_ASSERT_EQUAL_INT(SW_OK, ret);
    TEST_ASSERT_EQUAL_INT(1, thread_registry_count());

    {
        const thread_entry_t *entry = thread_registry_get(0);
        TEST_ASSERT_NOT_NULL(entry);
        TEST_ASSERT_EQUAL_STRING("worker", entry->name);
        TEST_ASSERT_EQUAL_PTR(one_shot_thread_fn, entry->fn);
        TEST_ASSERT_NULL(entry->arg);
        TEST_ASSERT_EQUAL_INT(SCHED_OTHER, entry->sched_policy);
        TEST_ASSERT_EQUAL_INT(0, entry->prio);
        TEST_ASSERT_EQUAL_UINT32(8192U, (unsigned)entry->stack_size);
    }
}

static void test_register_arg_passes_context(void)
{
    static int ctx_value = 42;
    sw_err_t   ret;

    ret = thread_register_arg("ctx_worker", one_shot_thread_fn, &ctx_value, SCHED_OTHER, 0, 4096U);
    TEST_ASSERT_EQUAL_INT(SW_OK, ret);
    TEST_ASSERT_EQUAL_INT(1, thread_registry_count());
    TEST_ASSERT_EQUAL_PTR(&ctx_value, thread_registry_get(0)->arg);
}

static void test_registry_get_out_of_range(void)
{
    TEST_ASSERT_NULL(thread_registry_get(-1));
    TEST_ASSERT_NULL(thread_registry_get(thread_registry_count()));
}

static void test_periodic_task_register_rejects_invalid_args(void)
{
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM,
                          periodic_task_register(NULL, 10U, periodic_tick_fn, NULL, SCHED_OTHER, 0, 4096U));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM,
                          periodic_task_register("tick", 0U, periodic_tick_fn, NULL, SCHED_OTHER, 0, 4096U));
    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, periodic_task_register("tick", 10U, NULL, NULL, SCHED_OTHER, 0, 4096U));
    /* 三次非法注册都不应占用登记槽 */
    TEST_ASSERT_EQUAL_INT(0, thread_registry_count());
}

static void test_periodic_task_register_and_scheduler_start(void)
{
    sw_err_t ret;

    /* 自行注册两类线程，不依赖其他用例的遗留登记 */
    ret = thread_register("one_shot", one_shot_thread_fn, SCHED_OTHER, 0, 8192U);
    TEST_ASSERT_EQUAL_INT(SW_OK, ret);

    ret = periodic_task_register("tick", 10U, periodic_tick_fn, NULL, SCHED_OTHER, 0, 8192U);
    TEST_ASSERT_EQUAL_INT(SW_OK, ret);
    TEST_ASSERT_EQUAL_INT(2, thread_registry_count());

    s_one_shot_done       = 0;
    s_periodic_tick_count = 0;

    ret = scheduler_start_all();
    TEST_ASSERT_EQUAL_INT(SW_OK, ret);

    usleep(80000U);

    TEST_ASSERT_EQUAL_INT(1, s_one_shot_done);
    TEST_ASSERT_TRUE(s_periodic_tick_count >= 2);
}

static void test_scheduler_sets_os_thread_name(void)
{
    static name_probe_t probe;
    char                os_name[32];

    memset(&probe, 0, sizeof(probe));

    TEST_ASSERT_EQUAL_INT(SW_OK,
                          thread_register_arg("short_name", name_probe_thread_fn, &probe, SCHED_OTHER, 0, 32768U));
    TEST_ASSERT_EQUAL_INT(SW_OK, scheduler_start_all());
    TEST_ASSERT_TRUE(name_probe_wait_ready(&probe));

    memset(os_name, 0, sizeof(os_name));
    TEST_ASSERT_EQUAL_INT(0, pthread_getname_np(probe.tid, os_name, sizeof(os_name)));
    TEST_ASSERT_EQUAL_STRING("short_name", os_name);

    probe.stop = 1;
}

static void test_scheduler_truncates_long_os_thread_name(void)
{
    /* 21 字符，远超 Linux 的 16 字节上限；不截断则 setname 静默失败，
     * OS 层名字会残留为进程名（如 test_scheduler）。 */
    static const char   long_name[] = "top_brush_profile_cli";
    static name_probe_t probe;
    char                os_name[32];

    memset(&probe, 0, sizeof(probe));

    TEST_ASSERT_EQUAL_INT(SW_OK, thread_register_arg(long_name, name_probe_thread_fn, &probe, SCHED_OTHER, 0, 32768U));
    TEST_ASSERT_EQUAL_INT(SW_OK, scheduler_start_all());
    TEST_ASSERT_TRUE(name_probe_wait_ready(&probe));

    memset(os_name, 0, sizeof(os_name));
    TEST_ASSERT_EQUAL_INT(0, pthread_getname_np(probe.tid, os_name, sizeof(os_name)));
    /* 保留前 15 字符 + '\0' */
    TEST_ASSERT_EQUAL_INT(15, (int)strlen(os_name));
    TEST_ASSERT_EQUAL_STRING("top_brush_profi", os_name);

    probe.stop = 1;
}

static void test_registry_overflow(void)
{
    int i;

    /* setUp 已复位登记表，从 0 填到满即可，无需读取当前值 */
    for (i = 0; i < THREAD_REGISTRY_MAX; i++) {
        TEST_ASSERT_EQUAL_INT(SW_OK, thread_register("fill", one_shot_thread_fn, SCHED_OTHER, 0, 4096U));
    }

    TEST_ASSERT_EQUAL_INT(THREAD_REGISTRY_MAX, thread_registry_count());
    TEST_ASSERT_EQUAL_INT(SW_ERR_OVERFLOW, thread_register("overflow", one_shot_thread_fn, SCHED_OTHER, 0, 4096U));
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_registry_count_initial_zero, "", "验证注册表数量初始零");
    WDF_RUN_TEST(test_register_rejects_null_name, "", "验证注册拒绝空指针名称");
    WDF_RUN_TEST(test_register_rejects_null_fn, "", "验证注册拒绝空指针函数");
    WDF_RUN_TEST(test_register_and_get_entry, "", "验证注册并获取条目");
    WDF_RUN_TEST(test_register_arg_passes_context, "", "验证注册参数通过上下文");
    WDF_RUN_TEST(test_registry_get_out_of_range, "", "验证读取注册表范围外条目时返回错误");
    WDF_RUN_TEST(test_periodic_task_register_rejects_invalid_args, "", "验证周期任务注册拒绝无效参数");
    WDF_RUN_TEST(test_periodic_task_register_and_scheduler_start, "", "验证周期任务注册后由调度器启动");
    WDF_RUN_TEST(test_scheduler_sets_os_thread_name, "", "验证调度器把注册名同步为 OS 线程名");
    WDF_RUN_TEST(test_scheduler_truncates_long_os_thread_name, "", "验证超长注册名截断到 15 字符后仍设置成功");
    WDF_RUN_TEST(test_registry_overflow, "", "验证注册表溢出");
    return UNITY_END();
}
