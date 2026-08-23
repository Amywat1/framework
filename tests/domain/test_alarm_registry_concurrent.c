/**
 * @file    test_alarm_registry_concurrent.c
 * @brief   报警注册表并发契约：安全投影的四项聚合值始终自洽
 *
 * @note    为何要有这个测试：`alarm_registry.c` 在 R16 里登记为「由报警采集线程
 *          驱动」，即它本就是多线程共享聚合，而原有的 registry 与桥接测试全是
 *          单线程的。`copy_safety_view` 当初引入的理由正是「四项数据必须来自同一
 *          次持锁」，但那条主张此前只有单线程断言——单线程下任何实现都成立。
 *
 *          这里的断言不是"并发跑一遍没崩"，而是投影内部的逻辑蕴含关系：
 *          聚合值与活动表来自同一时刻，则两者必须互相印证。若实现退化成分多次
 *          取锁，撕裂会表现为这些蕴含关系被破坏。
 */

#include "common/sw_error.h"
#include "common/time_util.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/model/alarm_types.h"
#include "domain/safety/model/safety_matrix.h"
#include "wdf_test_spec.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

enum {
    CONC_WRITERS    = 4,     /**< 写线程数 */
    CONC_READERS    = 4,     /**< 读线程数 */
    CONC_ITERATIONS = 200000 /**< 每线程迭代次数 */
};

/* 三条报警覆盖三个等级：MINOR 不阻塞不锁定、MAJOR 阻塞不锁定、CRITICAL 两者皆是。
 * 全部 MANUAL_RESET，这样 clear 只翻 condition_active 而不删条目，
 * 让活动表在整场测试中持续变位。 */
#define CONC_CODE_MINOR    901001U
#define CONC_CODE_MAJOR    201101U
#define CONC_CODE_CRITICAL 201709U
/* 专供 ALRM-18 记账：AUTO_STATIC 的 clear 会立即删除条目，故 trigger/clear 成对
 * 必定各产生一条域事件，产生总数精确可算。MANUAL_RESET 的码做不到——clear 只翻
 * condition_active 而不发 CLEARED，下一次 trigger 又命中已活跃条目而不发
 * TRIGGERED，一对 trigger/clear 可能一条事件都不产生。 */
#define CONC_CODE_AUTO     901002U

static const alarm_def_t s_catalog[] = {
    {
     .code         = CONC_CODE_MINOR,
     .level        = ALARM_LEVEL_MINOR,
     .clear        = ALARM_CLEAR_MANUAL_RESET,
     .reeval_group = ALARM_REEVAL_GROUP_NONE,
     .desc         = "conc minor",
     },
    {
     .code         = CONC_CODE_MAJOR,
     .level        = ALARM_LEVEL_MAJOR,
     .clear        = ALARM_CLEAR_MANUAL_RESET,
     .reeval_group = ALARM_REEVAL_GROUP_NONE,
     .desc         = "conc major",
     },
    {
     .code         = CONC_CODE_CRITICAL,
     .level        = ALARM_LEVEL_CRITICAL,
     .clear        = ALARM_CLEAR_MANUAL_RESET,
     .reeval_group = ALARM_REEVAL_GROUP_NONE,
     .desc         = "conc critical",
     },
    {
     .code         = CONC_CODE_AUTO,
     .level        = ALARM_LEVEL_MINOR,
     .clear        = ALARM_CLEAR_AUTO_STATIC,
     .reeval_group = ALARM_REEVAL_GROUP_NONE,
     .desc         = "conc auto",
     },
};

#define CONC_CATALOG_COUNT (sizeof(s_catalog) / sizeof(s_catalog[0]))

/* 投影用例的写线程只翻动前三条 MANUAL_RESET 码：它们 clear 后仍留在活动表里，
 * 能让活动表持续处于非空且变位的状态。CONC_CODE_AUTO 不在此列，它专供 ALRM-18。 */
#define CONC_WRITE_CODE_COUNT 3U

static const uint32_t k_codes[CONC_WRITE_CODE_COUNT] = {
    CONC_CODE_MINOR,
    CONC_CODE_MAJOR,
    CONC_CODE_CRITICAL,
};

/* 读线程发现的不一致计数。
 *
 * 用互斥量而非裸 volatile 自增：`x++` 不是原子操作，多个读线程并发自增会丢计数。
 * 漏计通常仍会留下非零值，但「测并发不变量的测试自己带着数据竞争」是拧着的。
 * 不在读线程里断言：Unity 的断言会 longjmp，从非主线程跳出属未定义行为。 */
static unsigned        s_torn_blocking;
static unsigned        s_torn_posture;
static unsigned        s_torn_top;
static unsigned        s_torn_count;
static pthread_mutex_t s_torn_mutex = PTHREAD_MUTEX_INITIALIZER;

static void torn_bump(unsigned *counter)
{
    pthread_mutex_lock(&s_torn_mutex);
    (*counter)++;
    pthread_mutex_unlock(&s_torn_mutex);
}

static unsigned torn_read(const unsigned *counter)
{
    unsigned v;

    pthread_mutex_lock(&s_torn_mutex);
    v = *counter;
    pthread_mutex_unlock(&s_torn_mutex);
    return v;
}

static pthread_barrier_t s_barrier;

void setUp(void)
{
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(s_catalog, (unsigned)CONC_CATALOG_COUNT));
    s_torn_blocking = 0U;
    s_torn_posture  = 0U;
    s_torn_top      = 0U;
    s_torn_count    = 0U;
}

void tearDown(void)
{
}

/* 持续翻动活动表：trigger 插入，reset_all 在 condition_active 为假时删除。
 *
 * 至少一个写线程专门紧凑地翻动 CRITICAL 的进出。撕裂只在「活动表与安全姿态取自
 * 不同时刻、而这期间 CRITICAL 恰好变位」时才可观测，所以必须把这个变位做得足够
 * 密集——否则测试会以很低的概率才发现退化实现，成为一条不可靠的门禁。 */
static void *writer_thread(void *raw)
{
    unsigned id = (unsigned)(uintptr_t)raw;
    unsigned i;

    (void)pthread_barrier_wait(&s_barrier);

    /* 0 号写线程只管 CRITICAL：插入、置条件消失、删除，三步紧凑循环 */
    if (id == 0U) {
        for (i = 0U; i < (unsigned)CONC_ITERATIONS; ++i) {
            (void)alarm_registry_trigger(CONC_CODE_CRITICAL);
            (void)alarm_registry_clear(CONC_CODE_CRITICAL);
            alarm_registry_reset_all();
        }
        return NULL;
    }

    for (i = 0U; i < (unsigned)CONC_ITERATIONS; ++i) {
        uint32_t code = k_codes[(i + id) % CONC_WRITE_CODE_COUNT];

        (void)alarm_registry_trigger(code);
        if (((i + id) % 3U) == 0U) {
            (void)alarm_registry_clear(code);
        }
        if (((i + id) % 7U) == 0U) {
            alarm_registry_reset_all();
        }
    }
    return NULL;
}

/**
 * @brief 校验一次投影内部自洽
 *
 * 四项来自同一次持锁，因此下列蕴含关系必须成立。若实现退化成多次取锁，
 * 聚合值与活动表分属不同时刻，这些关系就会被破坏。
 */
static void check_view_consistent(const alarm_instance_t *list,
                                  unsigned                n,
                                  bool                    blocking,
                                  uint32_t                top,
                                  safety_posture_t        posture)
{
    bool     saw_blocking = false;
    bool     saw_lockout  = false;
    bool     saw_top      = false;
    unsigned i;

    for (i = 0U; i < n; ++i) {
        if (alarm_level_blocks_wash(list[i].level)) {
            saw_blocking = true;
        }
        if (alarm_level_forces_lockout(list[i].level)) {
            saw_lockout = true;
        }
        if (list[i].code == top) {
            saw_top = true;
        }
    }

    /* 未截断时（n 即全部活动条目）聚合值必须与表内容完全互推 */
    if (blocking != saw_blocking) {
        torn_bump(&s_torn_blocking);
    }
    if ((posture == SAFETY_POSTURE_LOCKOUT) != saw_lockout) {
        torn_bump(&s_torn_posture);
    }
    /* 表非空则最高码必须落在表内；表空则必须是 NONE */
    if (n > 0U) {
        if (!saw_top) {
            torn_bump(&s_torn_top);
        }
    } else if (top != ALARM_CODE_NONE) {
        torn_bump(&s_torn_top);
    }
    /* LOCKOUT 蕴含 blocking：CRITICAL 同时具备两个属性 */
    if ((posture == SAFETY_POSTURE_LOCKOUT) && !blocking) {
        torn_bump(&s_torn_count);
    }
}

static void *reader_thread(void *raw)
{
    alarm_instance_t list[ALARM_ACTIVE_MAX];
    unsigned         i;

    (void)raw;
    (void)pthread_barrier_wait(&s_barrier);

    for (i = 0U; i < (unsigned)CONC_ITERATIONS; ++i) {
        bool             blocking = false;
        uint32_t         top      = ALARM_CODE_NONE;
        safety_posture_t posture  = SAFETY_POSTURE_NOMINAL;
        unsigned         n;

        n = alarm_registry_copy_safety_view(list, ALARM_ACTIVE_MAX, &blocking, &top, &posture);
        /* 写线程只碰 k_codes 里那几条，活动表不可能超过目录规模，
         * 故这里一定没有截断，可以直接对比聚合值与表内容。 */
        if (n > CONC_CATALOG_COUNT) {
            torn_bump(&s_torn_count);
            continue;
        }
        check_view_consistent(list, n, blocking, top, posture);
    }
    return NULL;
}

static void test_safety_view_is_never_torn_under_concurrency(void)
{
    pthread_t writers[CONC_WRITERS];
    pthread_t readers[CONC_READERS];
    unsigned  i;

    TEST_ASSERT_EQUAL_INT(0, pthread_barrier_init(&s_barrier, NULL, CONC_WRITERS + CONC_READERS));

    for (i = 0U; i < (unsigned)CONC_WRITERS; ++i) {
        TEST_ASSERT_EQUAL_INT(0, pthread_create(&writers[i], NULL, writer_thread, (void *)(uintptr_t)i));
    }
    for (i = 0U; i < (unsigned)CONC_READERS; ++i) {
        TEST_ASSERT_EQUAL_INT(0, pthread_create(&readers[i], NULL, reader_thread, NULL));
    }

    for (i = 0U; i < (unsigned)CONC_WRITERS; ++i) {
        TEST_ASSERT_EQUAL_INT(0, pthread_join(writers[i], NULL));
    }
    for (i = 0U; i < (unsigned)CONC_READERS; ++i) {
        TEST_ASSERT_EQUAL_INT(0, pthread_join(readers[i], NULL));
    }
    (void)pthread_barrier_destroy(&s_barrier);

    TEST_ASSERT_EQUAL_UINT_MESSAGE(0U, torn_read(&s_torn_blocking), "blocking 标志与活动表不一致");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0U, torn_read(&s_torn_posture), "安全姿态与活动表不一致");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0U, torn_read(&s_torn_top), "最高告警码不在活动表内");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0U, torn_read(&s_torn_count), "投影计数或蕴含关系被破坏");
}

/* -------------------------------------------------------------------------
 * ALRM-18：事件不得重复交出，也不得凭空丢失
 *
 * 用单生产者，期望值才是可精确判定的：901002 是 AUTO_STATIC，一次
 * trigger + clear 恰好产生 TRIGGERED + CLEARED 两条域事件，故总产生数就是
 * 2 * PULL_ITERATIONS。多个写线程并发写同一组码时，trigger 命中已活跃条目
 * 不产生事件，期望值随调度浮动、算不出来——那样的上界断言（本轮初版就是）
 * 宽到丢掉绝大多数事件也能通过，等于没有断言。
 *
 * 多个消费者并发 pull：每条事件必须恰好被一个消费者拿到一次。
 * 交出数 + 丢弃数 == 产生数，多一条即重复交出，少一条即丢账。
 * ------------------------------------------------------------------------- */

#define PULL_ITERATIONS 20000U
#define PULL_EXPECTED   (2U * PULL_ITERATIONS)

static unsigned        s_pulled_total;
static pthread_mutex_t s_tally_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile int    s_produce_done;

static void tally_add(unsigned v)
{
    pthread_mutex_lock(&s_tally_mutex);
    s_pulled_total += v;
    pthread_mutex_unlock(&s_tally_mutex);
}

/* 单生产者：成对 trigger/clear，产生数精确可知 */
static void *single_producer_fn(void *raw)
{
    unsigned i;

    (void)raw;
    (void)pthread_barrier_wait(&s_barrier);

    for (i = 0U; i < PULL_ITERATIONS; ++i) {
        (void)alarm_registry_trigger(CONC_CODE_AUTO);
        (void)alarm_registry_clear(CONC_CODE_AUTO);
    }
    s_produce_done = 1;
    return NULL;
}

static void *puller_fn(void *raw)
{
    alarm_domain_event_t batch[ALARM_PENDING_EVENT_MAX];

    (void)raw;
    (void)pthread_barrier_wait(&s_barrier);

    /* 生产者停了之后再空转一轮，确保尾部事件也被取走 */
    for (;;) {
        uint32_t dropped = 0U;
        unsigned n;
        int      done = s_produce_done;

        n = alarm_registry_pull_events(batch, ALARM_PENDING_EVENT_MAX, &dropped);
        if ((n > 0U) || (dropped > 0U)) {
            tally_add(n + (unsigned)dropped);
        }
        if ((done != 0) && (n == 0U)) {
            return NULL;
        }
    }
}

static void test_pull_events_accounts_every_event_exactly_once(void)
{
    pthread_t producer;
    pthread_t pullers[CONC_READERS];
    unsigned  i;

    s_pulled_total = 0U;
    s_produce_done = 0;
    TEST_ASSERT_EQUAL_INT(0, pthread_barrier_init(&s_barrier, NULL, 1U + CONC_READERS));

    TEST_ASSERT_EQUAL_INT(0, pthread_create(&producer, NULL, single_producer_fn, NULL));
    for (i = 0U; i < (unsigned)CONC_READERS; ++i) {
        TEST_ASSERT_EQUAL_INT(0, pthread_create(&pullers[i], NULL, puller_fn, NULL));
    }

    TEST_ASSERT_EQUAL_INT(0, pthread_join(producer, NULL));
    for (i = 0U; i < (unsigned)CONC_READERS; ++i) {
        TEST_ASSERT_EQUAL_INT(0, pthread_join(pullers[i], NULL));
    }
    (void)pthread_barrier_destroy(&s_barrier);

    /* 精确记账：不多一条（重复交出）、不少一条（丢账） */
    TEST_ASSERT_EQUAL_UINT_MESSAGE(PULL_EXPECTED, s_pulled_total, "交出数与丢弃数之和不等于产生数");
}

int main(void)
{
    UNITY_BEGIN();

    WDF_RUN_TEST(test_safety_view_is_never_torn_under_concurrency, "ALRM-17", "验证并发下安全投影四项聚合值始终自洽");
    WDF_RUN_TEST(test_pull_events_accounts_every_event_exactly_once, "ALRM-18", "验证并发拉取事件不重复不丢账");

    return UNITY_END();
}
