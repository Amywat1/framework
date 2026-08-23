/**
 * @file    test_log_concurrent.c
 * @brief   日志并发契约：整行一次写出、sink 回写不死锁
 */

#include "common/log.h"
#include "wdf_test_spec.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum {
    LOG_CONC_THREADS     = 8,
    LOG_CONC_MSGS        = 80,
    LOG_CONC_PAD         = 16,
    LOG_CONC_WAIT_SLICES = 1000,
};

static int  s_saved_stderr = -1;
static char s_capture_path[64];

static int          s_reenter_depth;
static int          s_outer_calls;
static int          s_inner_calls;
static volatile int s_reenter_done;

void setUp(void)
{
    s_saved_stderr    = -1;
    s_capture_path[0] = '\0';
    s_reenter_depth   = 0;
    s_outer_calls     = 0;
    s_inner_calls     = 0;
    s_reenter_done    = 0;
    sw_log_register_sink(NULL);
    sw_log_set_level(SW_LOG_DEBUG);
}

void tearDown(void)
{
    if (s_saved_stderr >= 0) {
        (void)fflush(stderr);
        (void)dup2(s_saved_stderr, STDERR_FILENO);
        (void)close(s_saved_stderr);
        s_saved_stderr = -1;
    }
    if (s_capture_path[0] != '\0') {
        (void)unlink(s_capture_path);
        s_capture_path[0] = '\0';
    }
    sw_log_register_sink(NULL);
    sw_log_set_level(SW_LOG_DEBUG);
}

typedef struct {
    unsigned           id;
    pthread_barrier_t *barrier;
} writer_arg_t;

static void *line_writer(void *raw)
{
    writer_arg_t *arg = (writer_arg_t *)raw;
    char          pad[LOG_CONC_PAD + 1];
    unsigned      seq;

    memset(pad, (int)('A' + arg->id), LOG_CONC_PAD);
    pad[LOG_CONC_PAD] = '\0';
    (void)pthread_barrier_wait(arg->barrier);
    for (seq = 0U; seq < (unsigned)LOG_CONC_MSGS; seq++) {
        sw_log_write(SW_LOG_INFO, "C", "T%u#%04u-%s", arg->id, seq, pad);
    }
    return NULL;
}

static int start_stderr_capture(void)
{
    int fd;

    (void)fflush(stderr);
    s_saved_stderr = dup(STDERR_FILENO);
    if (s_saved_stderr < 0) {
        return -1;
    }
    (void)snprintf(s_capture_path, sizeof(s_capture_path), "/tmp/wdf_log_conc_XXXXXX");
    fd = mkstemp(s_capture_path);
    if (fd < 0) {
        (void)close(s_saved_stderr);
        s_saved_stderr    = -1;
        s_capture_path[0] = '\0';
        return -1;
    }
    if (dup2(fd, STDERR_FILENO) != STDERR_FILENO) {
        (void)close(fd);
        (void)close(s_saved_stderr);
        s_saved_stderr = -1;
        (void)unlink(s_capture_path);
        s_capture_path[0] = '\0';
        return -1;
    }
    (void)close(fd);
    (void)setvbuf(stderr, NULL, _IONBF, 0);
    return 0;
}

static int stop_stderr_capture(void)
{
    int rc;

    (void)fflush(stderr);
    rc = dup2(s_saved_stderr, STDERR_FILENO);
    (void)close(s_saved_stderr);
    s_saved_stderr = -1;
    return rc == STDERR_FILENO ? 0 : -1;
}

static void test_concurrent_default_sink_keeps_lines_intact(void)
{
    pthread_barrier_t barrier;
    pthread_t         threads[LOG_CONC_THREADS];
    writer_arg_t      args[LOG_CONC_THREADS];
    unsigned char     seen[LOG_CONC_THREADS][LOG_CONC_MSGS];
    FILE             *fp;
    char              line[128];
    unsigned          complete = 0U;
    unsigned          tid;
    int               i;

    TEST_ASSERT_EQUAL_INT(0, start_stderr_capture());
    TEST_ASSERT_EQUAL_INT(0, pthread_barrier_init(&barrier, NULL, (unsigned)LOG_CONC_THREADS));

    for (i = 0; i < LOG_CONC_THREADS; i++) {
        args[i].id      = (unsigned)i;
        args[i].barrier = &barrier;
        TEST_ASSERT_EQUAL_INT(0, pthread_create(&threads[i], NULL, line_writer, &args[i]));
    }
    for (i = 0; i < LOG_CONC_THREADS; i++) {
        TEST_ASSERT_EQUAL_INT(0, pthread_join(threads[i], NULL));
    }
    TEST_ASSERT_EQUAL_INT(0, pthread_barrier_destroy(&barrier));
    TEST_ASSERT_EQUAL_INT(0, stop_stderr_capture());

    memset(seen, 0, sizeof(seen));
    fp = fopen(s_capture_path, "r");
    TEST_ASSERT_NOT_NULL(fp);
    while (fgets(line, (int)sizeof(line), fp) != NULL) {
        size_t   length = strlen(line);
        unsigned seq    = 0U;
        char     pad[LOG_CONC_PAD + 1];
        unsigned pad_i;

        TEST_ASSERT_TRUE(length > 1U);
        TEST_ASSERT_EQUAL_CHAR('\n', line[length - 1U]);
        line[length - 1U] = '\0';
        memset(pad, 0, sizeof(pad));
        TEST_ASSERT_EQUAL_INT(3, sscanf(line, "[INF] [C] T%u#%4u-%16s", &tid, &seq, pad));
        TEST_ASSERT_TRUE(tid < (unsigned)LOG_CONC_THREADS);
        TEST_ASSERT_TRUE(seq < (unsigned)LOG_CONC_MSGS);
        TEST_ASSERT_EQUAL_UINT((unsigned)LOG_CONC_PAD, (unsigned)strlen(pad));
        for (pad_i = 0U; pad_i < (unsigned)LOG_CONC_PAD; pad_i++) {
            TEST_ASSERT_EQUAL_CHAR((char)('A' + tid), pad[pad_i]);
        }
        TEST_ASSERT_EQUAL_INT(0, seen[tid][seq]);
        seen[tid][seq] = 1U;
        complete++;
    }
    (void)fclose(fp);
    TEST_ASSERT_EQUAL_UINT((unsigned)(LOG_CONC_THREADS * LOG_CONC_MSGS), complete);
}

static void reentrant_sink(sw_log_level_t level, const char *component, const char *fmt, va_list ap)
{
    (void)level;
    (void)component;
    (void)fmt;
    (void)ap;

    if (s_reenter_depth > 0) {
        s_inner_calls++;
        return;
    }
    s_reenter_depth++;
    sw_log_write(SW_LOG_INFO, "INNER", "nested");
    s_outer_calls++;
    s_reenter_depth--;
}

static void *reenter_writer(void *raw)
{
    (void)raw;
    sw_log_write(SW_LOG_INFO, "OUTER", "start");
    s_reenter_done = 1;
    return NULL;
}

static void test_reentrant_sink_does_not_deadlock(void)
{
    pthread_t tid;
    int       slices = 0;

    sw_log_register_sink(reentrant_sink);
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&tid, NULL, reenter_writer, NULL));
    while ((s_reenter_done == 0) && (slices < LOG_CONC_WAIT_SLICES)) {
        (void)usleep(1000U);
        slices++;
    }
    TEST_ASSERT_EQUAL_INT(1, s_reenter_done);
    TEST_ASSERT_EQUAL_INT(0, pthread_join(tid, NULL));
    TEST_ASSERT_EQUAL_INT(1, s_outer_calls);
    TEST_ASSERT_EQUAL_INT(1, s_inner_calls);
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_concurrent_default_sink_keeps_lines_intact, "LOG-02", "验证并发日志整行一次写出不交错");
    WDF_RUN_TEST(test_reentrant_sink_does_not_deadlock, "LOG-03", "验证 sink 回写日志不死锁");
    return UNITY_END();
}
