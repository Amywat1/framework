/**
 * @file    test_alarm_announce.c
 * @brief   报警提示轴：设计抑制、搁置、检测停用
 */

#include "common/event_types.h"
#include "common/sw_error.h"
#include "common/time_util.h"
#include "domain/safety/alarm_registry/alarm_registry.h"
#include "domain/safety/model/alarm_types.h"
#include "runtime/event_bus/event_bus.h"
#include "wdf_test_spec.h"

#define CODE_OVERLOAD  100401U
#define CODE_TIMEOUT   100405U
#define CODE_ESTOP     201709U
#define CODE_SAFETY_A  201409U
#define CODE_SAFETY_B  201509U
#define CODE_MINOR     900100U

static const alarm_def_t s_catalog[] = {
    {
     .code         = CODE_OVERLOAD,
     .level        = ALARM_LEVEL_MAJOR,
     .lifecycle    = ALARM_LIFECYCLE_LATCH,
     .reeval_group = ALARM_REEVAL_GROUP_NONE,
     .desc         = "过流",
     },
    {
     .code         = CODE_TIMEOUT,
     .level        = ALARM_LEVEL_MAJOR,
     .lifecycle    = ALARM_LIFECYCLE_LATCH,
     .reeval_group = ALARM_REEVAL_GROUP_NONE,
     .desc         = "超时",
     },
    {
     .code         = CODE_ESTOP,
     .level        = ALARM_LEVEL_CRITICAL,
     .lifecycle    = ALARM_LIFECYCLE_FOLLOW_SIGNAL,
     .reeval_group = ALARM_REEVAL_GROUP_NONE,
     .desc         = "急停",
     },
    {
     .code         = CODE_SAFETY_A,
     .level        = ALARM_LEVEL_CRITICAL,
     .lifecycle    = ALARM_LIFECYCLE_FOLLOW_SIGNAL,
     .reeval_group = ALARM_REEVAL_GROUP_NONE,
     .desc         = "安全检测",
     .allow_oos    = true,
     .oos_admit    = ALARM_OOS_ADMIT_RUN,
     },
    {
     .code         = CODE_SAFETY_B,
     .level        = ALARM_LEVEL_CRITICAL,
     .lifecycle    = ALARM_LIFECYCLE_FOLLOW_SIGNAL,
     .reeval_group = ALARM_REEVAL_GROUP_NONE,
     .desc         = "另一安全检测",
     },
    {
     .code           = CODE_MINOR,
     .level          = ALARM_LEVEL_MINOR,
     .lifecycle      = ALARM_LIFECYCLE_FOLLOW_SIGNAL,
     .reeval_group   = ALARM_REEVAL_GROUP_NONE,
     .desc           = "提示",
     .suppress_modes = ALARM_SUPPRESS_MODE_BIT(ALARM_OP_STOPPED),
     },
};

static uint32_t s_ann[ALARM_ACTIVE_MAX];
static unsigned s_ann_n;
static uint32_t s_mute[ALARM_ACTIVE_MAX];
static unsigned s_mute_n;

static void on_ann(const event_t *evt)
{
    if (s_ann_n < ALARM_ACTIVE_MAX) {
        s_ann[s_ann_n++] = evt->param;
    } else {
        s_ann_n++;
    }
}

static void on_mute(const event_t *evt)
{
    if (s_mute_n < ALARM_ACTIVE_MAX) {
        s_mute[s_mute_n++] = evt->param;
    } else {
        s_mute_n++;
    }
}

static alarm_instance_t find_inst(uint32_t code)
{
    alarm_safety_view_t view;
    unsigned            i;

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_copy_safety_view(&view));
    for (i = 0U; i < view.count; ++i) {
        if (view.list[i].code == code) {
            return view.list[i];
        }
    }
    TEST_FAIL_MESSAGE("instance not in view");
    return (alarm_instance_t){0};
}

void setUp(void)
{
    time_util_init();
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_init());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_load_catalog(s_catalog, sizeof(s_catalog) / sizeof(s_catalog[0])));
    alarm_registry_set_op_mode(ALARM_OP_ACTIVE);
    s_ann_n  = 0U;
    s_mute_n = 0U;
    (void)event_subscribe(EVT_ALARM_ANNOUNCE, on_ann);
    (void)event_subscribe(EVT_ALARM_MUTE, on_mute);
}

void tearDown(void)
{
}

static void test_shelve_major_keeps_blocking(void)
{
    alarm_instance_t inst;

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(CODE_OVERLOAD));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
    TEST_ASSERT_TRUE(alarm_registry_has_blocking_active());
    inst = find_inst(CODE_OVERLOAD);
    TEST_ASSERT_TRUE(inst.announced);

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_shelve(CODE_OVERLOAD, 1000U));
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
    TEST_ASSERT_TRUE(alarm_registry_has_blocking_active());
    inst = find_inst(CODE_OVERLOAD);
    TEST_ASSERT_FALSE(inst.announced);
    TEST_ASSERT_EQUAL_INT(ALARM_MUTE_SHELVED, inst.mute_reason);

    alarm_registry_poll(time_util_get_ms() + 2000U);
    TEST_ASSERT_EQUAL_INT(SW_OK, event_bus_drain());
    TEST_ASSERT_TRUE(alarm_registry_has_blocking_active());
    inst = find_inst(CODE_OVERLOAD);
    TEST_ASSERT_TRUE(inst.announced);
}

static void test_idle_timeout_muted_overload_announced(void)
{
    alarm_instance_t inst;

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(CODE_TIMEOUT));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(CODE_OVERLOAD));
    TEST_ASSERT_TRUE(alarm_registry_is_active(CODE_TIMEOUT));
    TEST_ASSERT_TRUE(alarm_registry_has_blocking_active());
    inst = find_inst(CODE_TIMEOUT);
    TEST_ASSERT_FALSE(inst.announced);
    TEST_ASSERT_EQUAL_INT(ALARM_MUTE_RUN, inst.mute_reason);
    inst = find_inst(CODE_OVERLOAD);
    TEST_ASSERT_TRUE(inst.announced);
}

static void test_tripped_timeout_eclipsed_same_object(void)
{
    alarm_instance_t inst;

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_set_object_run(ALM_C_POWER, 4U, ALARM_OBJECT_RUN_COMMANDED));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(CODE_OVERLOAD));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_set_object_run(ALM_C_POWER, 4U, ALARM_OBJECT_RUN_TRIPPED));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(CODE_TIMEOUT));
    TEST_ASSERT_TRUE(alarm_registry_has_blocking_active());
    inst = find_inst(CODE_OVERLOAD);
    TEST_ASSERT_TRUE(inst.announced);
    inst = find_inst(CODE_TIMEOUT);
    TEST_ASSERT_FALSE(inst.announced);
    TEST_ASSERT_EQUAL_INT(ALARM_MUTE_ECLIPSE, inst.mute_reason);
}

static void test_estop_eclipses_other_timeout_not_other_safety(void)
{
    alarm_instance_t inst;

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_set_object_run(ALM_C_POWER, 4U, ALARM_OBJECT_RUN_COMMANDED));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(CODE_ESTOP));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(CODE_TIMEOUT));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(CODE_SAFETY_B));
    inst = find_inst(CODE_TIMEOUT);
    TEST_ASSERT_FALSE(inst.announced);
    inst = find_inst(CODE_SAFETY_B);
    TEST_ASSERT_TRUE(inst.announced);

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_clear(CODE_SAFETY_B));
    inst = find_inst(CODE_TIMEOUT);
    TEST_ASSERT_FALSE(inst.announced);

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_clear(CODE_ESTOP));
    inst = find_inst(CODE_TIMEOUT);
    TEST_ASSERT_TRUE(inst.announced);
}

static void test_oos_safety_compensating_minor(void)
{
    uint32_t            comp = alarm_registry_oos_compensating_code(CODE_SAFETY_A);
    alarm_safety_view_t view;

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_set_oos(CODE_SAFETY_A, true));
    TEST_ASSERT_TRUE(alarm_registry_is_oos(CODE_SAFETY_A));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(CODE_SAFETY_A));
    TEST_ASSERT_FALSE(alarm_registry_is_active(CODE_SAFETY_A));
    TEST_ASSERT_TRUE(alarm_registry_is_active(comp));
    TEST_ASSERT_FALSE(alarm_registry_has_blocking_active());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_copy_safety_view(&view));
    TEST_ASSERT_EQUAL_UINT(1U, view.oos_count);
    TEST_ASSERT_EQUAL_UINT(CODE_SAFETY_A, view.oos_list[0]);
}

static void test_load_rejects_standby_suppress(void)
{
    alarm_def_t bad = {
        .code           = 100401U,
        .level          = ALARM_LEVEL_MAJOR,
        .lifecycle      = ALARM_LIFECYCLE_LATCH,
        .reeval_group   = ALARM_REEVAL_GROUP_NONE,
        .desc           = "bad",
        .suppress_modes = ALARM_SUPPRESS_MODE_BIT(ALARM_OP_STANDBY),
    };

    TEST_ASSERT_EQUAL_INT(SW_ERR_PARAM, alarm_registry_load_catalog(&bad, 1U));
}

static void test_stopped_suppresses_catalog_minor(void)
{
    alarm_instance_t inst;

    alarm_registry_set_op_mode(ALARM_OP_STOPPED);
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(CODE_MINOR));
    inst = find_inst(CODE_MINOR);
    TEST_ASSERT_FALSE(inst.announced);
    TEST_ASSERT_EQUAL_INT(ALARM_MUTE_MODE, inst.mute_reason);
}

static void test_commanded_timeout_announces(void)
{
    alarm_instance_t inst;

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_set_object_run(ALM_C_POWER, 4U, ALARM_OBJECT_RUN_COMMANDED));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(CODE_TIMEOUT));
    inst = find_inst(CODE_TIMEOUT);
    TEST_ASSERT_TRUE(inst.announced);
}

static void test_repeat_trigger_keeps_shelve(void)
{
    alarm_instance_t inst;

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(CODE_OVERLOAD));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_shelve(CODE_OVERLOAD, 60000U));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(CODE_OVERLOAD));
    inst = find_inst(CODE_OVERLOAD);
    TEST_ASSERT_FALSE(inst.announced);
    TEST_ASSERT_EQUAL_INT(ALARM_MUTE_SHELVED, inst.mute_reason);
    TEST_ASSERT_TRUE(inst.shelved_until_ms != 0U);
}

static void test_latch_reraise_unshelves(void)
{
    alarm_instance_t inst;

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(CODE_OVERLOAD));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_shelve(CODE_OVERLOAD, 60000U));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_clear(CODE_OVERLOAD));
    inst = find_inst(CODE_OVERLOAD);
    TEST_ASSERT_FALSE(inst.condition_active);
    TEST_ASSERT_EQUAL_INT(ALARM_MUTE_SHELVED, inst.mute_reason);
    TEST_ASSERT_TRUE(inst.shelved_until_ms != 0U);

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(CODE_OVERLOAD));
    inst = find_inst(CODE_OVERLOAD);
    TEST_ASSERT_TRUE(inst.condition_active);
    TEST_ASSERT_TRUE(inst.announced);
    TEST_ASSERT_EQUAL_UINT64(0U, inst.shelved_until_ms);
}

static void test_clear_overrides_resets_object_run(void)
{
    alarm_instance_t inst;

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_set_object_run(ALM_C_POWER, 4U, ALARM_OBJECT_RUN_COMMANDED));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(CODE_TIMEOUT));
    inst = find_inst(CODE_TIMEOUT);
    TEST_ASSERT_TRUE(inst.announced);

    alarm_registry_clear_announce_overrides();
    inst = find_inst(CODE_TIMEOUT);
    TEST_ASSERT_TRUE(alarm_registry_is_active(CODE_TIMEOUT));
    TEST_ASSERT_FALSE(inst.announced);
    TEST_ASSERT_EQUAL_INT(ALARM_MUTE_RUN, inst.mute_reason);
}

static void test_oos_survives_recover_sequence(void)
{
    uint32_t comp = alarm_registry_oos_compensating_code(CODE_SAFETY_A);

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_set_oos(CODE_SAFETY_A, true));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(CODE_SAFETY_A));
    alarm_registry_clear_announce_overrides();
    alarm_registry_reset_non_safety();
    TEST_ASSERT_TRUE(alarm_registry_is_oos(CODE_SAFETY_A));
    TEST_ASSERT_FALSE(alarm_registry_is_active(CODE_SAFETY_A));
    TEST_ASSERT_TRUE(alarm_registry_is_active(comp));
    TEST_ASSERT_FALSE(alarm_registry_has_blocking_active());
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(CODE_SAFETY_A));
    TEST_ASSERT_FALSE(alarm_registry_is_active(CODE_SAFETY_A));
    TEST_ASSERT_TRUE(alarm_registry_is_active(comp));
    TEST_ASSERT_FALSE(alarm_registry_has_blocking_active());
}

static void test_clear_oos_drops_compensating(void)
{
    uint32_t comp = alarm_registry_oos_compensating_code(CODE_SAFETY_A);

    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_set_oos(CODE_SAFETY_A, true));
    TEST_ASSERT_EQUAL_INT(SW_OK, alarm_registry_trigger(CODE_SAFETY_A));
    alarm_registry_clear_oos();
    TEST_ASSERT_FALSE(alarm_registry_is_oos(CODE_SAFETY_A));
    TEST_ASSERT_FALSE(alarm_registry_is_active(comp));
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_shelve_major_keeps_blocking, "", "搁置 MAJOR 不放开洗闸且到期再提示");
    WDF_RUN_TEST(test_idle_timeout_muted_overload_announced, "", "IDLE 超时不提示、过流仍提示");
    WDF_RUN_TEST(test_tripped_timeout_eclipsed_same_object, "", "同对象过流后超时互挡");
    WDF_RUN_TEST(test_estop_eclipses_other_timeout_not_other_safety, "", "急停挡住其它轴超时但不挡另一安全触发");
    WDF_RUN_TEST(test_oos_safety_compensating_minor, "", "安全检测停用后原码不进表、补偿 MINOR 不挡开洗");
    WDF_RUN_TEST(test_load_rejects_standby_suppress, "", "接单态写入 suppress_modes 装载失败");
    WDF_RUN_TEST(test_stopped_suppresses_catalog_minor, "", "停机按目录抑制仅提示码");
    WDF_RUN_TEST(test_commanded_timeout_announces, "", "受令后超时提示");
    WDF_RUN_TEST(test_repeat_trigger_keeps_shelve, "", "条件仍成立时重复 trigger 不取消搁置");
    WDF_RUN_TEST(test_latch_reraise_unshelves, "", "锁存条件再现视为新事件并下架搁置");
    WDF_RUN_TEST(test_clear_overrides_resets_object_run, "ALRM-26", "清覆盖后对象开停回到 IDLE，过程量不再提示");
    WDF_RUN_TEST(test_oos_survives_recover_sequence, "ALRM-27", "恢复起步不清停用，补偿码仍在且不挡开洗");
    WDF_RUN_TEST(test_clear_oos_drops_compensating, "ALRM-27", "关总开关清停用后补偿码删除");
    return UNITY_END();
}
