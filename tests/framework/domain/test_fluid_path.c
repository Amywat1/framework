/**
 * @file    test_fluid_path.c
 * @brief   fluid_path 模式单元测试（poll 模式）
 */

#include "unity.h"
#include "framework/domain/device_control/patterns/fluid_path.h"
#include <string.h>

#define TEST_CH_SHARED   0U
#define TEST_CH_VALVE_A  1U
#define TEST_CH_VALVE_B  2U
#define TEST_CH_COUNT    3U
#define TEST_PATH_A      0U
#define TEST_PATH_B      1U

static bool s_slot_state[TEST_CH_COUNT][FLUID_PATH_SLOT_COUNT];

static sw_err_t mock_slot_set(fluid_path_channel_idx_t ch, fluid_path_slot_t slot, bool on)
{
    if (((unsigned)ch >= TEST_CH_COUNT) || ((unsigned)slot >= FLUID_PATH_SLOT_COUNT))
    {
        return SW_ERR_PARAM;
    }
    s_slot_state[ch][slot] = on;
    return SW_OK;
}

static sw_err_t mock_all_off(void)
{
    memset(s_slot_state, 0, sizeof(s_slot_state));
    return SW_OK;
}

static const fluid_path_actuator_key_t s_deps_a[] = {
    { TEST_CH_VALVE_A, FLUID_PATH_SLOT_WATER_VALVE },
    { TEST_CH_SHARED,  FLUID_PATH_SLOT_PUMP },
};
static const fluid_path_actuator_key_t s_deps_b[] = {
    { TEST_CH_VALVE_B, FLUID_PATH_SLOT_WATER_VALVE },
    { TEST_CH_SHARED,  FLUID_PATH_SLOT_PUMP },
};

static const fluid_path_def_t s_paths[] = {
    { TEST_PATH_A, s_deps_a, 2U },
    { TEST_PATH_B, s_deps_b, 2U },
};

static const fluid_path_cfg_t s_cfg = {
    .valve_open_delay_ms = 0U,
    .pump_stop_delay_ms  = 0U,
    .channel_count       = TEST_CH_COUNT,
};

static void drain(uint64_t *now_ms)
{
    unsigned guard = 0U;

    while (!fluid_path_is_settled())
    {
        fluid_path_poll(*now_ms);
        *now_ms += 10U;
        guard++;
        TEST_ASSERT_LESS_THAN(100U, guard);
    }
}

void setUp(void)
{
    memset(s_slot_state, 0, sizeof(s_slot_state));
}

void tearDown(void)
{
    uint64_t now_ms = 0U;

    (void)fluid_path_all_off();
    drain(&now_ms);
}

void test_fluid_path_init_all_off(void)
{
    uint64_t now_ms = 0U;

    TEST_ASSERT_EQUAL(SW_OK, fluid_path_init(&s_cfg,
                                        &(fluid_path_actuator_ops_t){
                                            .slot_set = mock_slot_set,
                                            .all_off  = mock_all_off,
                                        },
                                        s_paths, 2U));
    drain(&now_ms);
    TEST_ASSERT_FALSE(s_slot_state[TEST_CH_SHARED][FLUID_PATH_SLOT_PUMP]);
    TEST_ASSERT_FALSE(s_slot_state[TEST_CH_VALVE_A][FLUID_PATH_SLOT_WATER_VALVE]);
}

void test_fluid_path_open_single_path(void)
{
    uint64_t now_ms = 0U;

    TEST_ASSERT_EQUAL(SW_OK, fluid_path_init(&s_cfg,
                                        &(fluid_path_actuator_ops_t){
                                            .slot_set = mock_slot_set,
                                            .all_off  = mock_all_off,
                                        },
                                        s_paths, 2U));
    drain(&now_ms);

    TEST_ASSERT_EQUAL(SW_OK, fluid_path_set(FLUID_PATH_MASK(TEST_PATH_A)));
    drain(&now_ms);

    TEST_ASSERT_TRUE(s_slot_state[TEST_CH_VALVE_A][FLUID_PATH_SLOT_WATER_VALVE]);
    TEST_ASSERT_TRUE(s_slot_state[TEST_CH_SHARED][FLUID_PATH_SLOT_PUMP]);
}

void test_fluid_path_parallel_paths_share_pump(void)
{
    uint64_t now_ms = 0U;

    TEST_ASSERT_EQUAL(SW_OK, fluid_path_init(&s_cfg,
                                        &(fluid_path_actuator_ops_t){
                                            .slot_set = mock_slot_set,
                                            .all_off  = mock_all_off,
                                        },
                                        s_paths, 2U));
    drain(&now_ms);

    TEST_ASSERT_EQUAL(SW_OK, fluid_path_set(FLUID_PATH_MASK(TEST_PATH_A)
                                            | FLUID_PATH_MASK(TEST_PATH_B)));
    drain(&now_ms);
    TEST_ASSERT_TRUE(s_slot_state[TEST_CH_VALVE_A][FLUID_PATH_SLOT_WATER_VALVE]);
    TEST_ASSERT_TRUE(s_slot_state[TEST_CH_VALVE_B][FLUID_PATH_SLOT_WATER_VALVE]);
    TEST_ASSERT_TRUE(s_slot_state[TEST_CH_SHARED][FLUID_PATH_SLOT_PUMP]);

    TEST_ASSERT_EQUAL(SW_OK, fluid_path_set(FLUID_PATH_MASK(TEST_PATH_B)));
    drain(&now_ms);
    TEST_ASSERT_FALSE(s_slot_state[TEST_CH_VALVE_A][FLUID_PATH_SLOT_WATER_VALVE]);
    TEST_ASSERT_TRUE(s_slot_state[TEST_CH_VALVE_B][FLUID_PATH_SLOT_WATER_VALVE]);
    TEST_ASSERT_TRUE(s_slot_state[TEST_CH_SHARED][FLUID_PATH_SLOT_PUMP]);

    TEST_ASSERT_EQUAL(SW_OK, fluid_path_set(0U));
    drain(&now_ms);
    TEST_ASSERT_FALSE(s_slot_state[TEST_CH_SHARED][FLUID_PATH_SLOT_PUMP]);
}

void test_fluid_path_nonblocking_request(void)
{
    uint64_t now_ms = 0U;

    TEST_ASSERT_EQUAL(SW_OK, fluid_path_init(&s_cfg,
                                        &(fluid_path_actuator_ops_t){
                                            .slot_set = mock_slot_set,
                                            .all_off  = mock_all_off,
                                        },
                                        s_paths, 2U));
    drain(&now_ms);

    TEST_ASSERT_EQUAL(SW_OK, fluid_path_set(FLUID_PATH_MASK(TEST_PATH_A)));
    TEST_ASSERT_FALSE(fluid_path_is_settled());
    drain(&now_ms);
    TEST_ASSERT_TRUE(fluid_path_is_settled());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fluid_path_init_all_off);
    RUN_TEST(test_fluid_path_open_single_path);
    RUN_TEST(test_fluid_path_parallel_paths_share_pump);
    RUN_TEST(test_fluid_path_nonblocking_request);
    return UNITY_END();
}
