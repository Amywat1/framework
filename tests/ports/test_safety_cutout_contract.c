#include "common/sw_error.h"
#include "tests/support/safety_cutout_contract.h"
#include "wdf_test_spec.h"

#include <string.h>

#define TEST_CUTOUT_CHANNELS 3U

void setUp(void)
{
}

void tearDown(void)
{
}

typedef struct {
    unsigned calls[TEST_CUTOUT_CHANNELS];
    sw_err_t errors[TEST_CUTOUT_CHANNELS];
    unsigned output_mask;
} fixture_state_t;

static sw_err_t fixture_cutout(void *ctx)
{
    fixture_state_t *state = (fixture_state_t *)ctx;
    sw_err_t         first = SW_OK;

    for (unsigned i = 0U; i < TEST_CUTOUT_CHANNELS; ++i) {
        state->calls[i]++;
        state->output_mask &= ~(1U << i);
        if ((first == SW_OK) && (state->errors[i] != SW_OK)) {
            first = state->errors[i];
        }
    }
    return first;
}

static void fixture_reset(void *ctx)
{
    fixture_state_t *state = (fixture_state_t *)ctx;

    memset(state, 0, sizeof(*state));
    state->output_mask = (1U << TEST_CUTOUT_CHANNELS) - 1U;
}

static void fixture_fail_channel(void *ctx, unsigned channel, sw_err_t error)
{
    if (channel < TEST_CUTOUT_CHANNELS) {
        ((fixture_state_t *)ctx)->errors[channel] = error;
    }
}

static unsigned fixture_channel_count(void *ctx)
{
    (void)ctx;
    return TEST_CUTOUT_CHANNELS;
}

static unsigned fixture_channel_calls(void *ctx, unsigned channel)
{
    return (channel < TEST_CUTOUT_CHANNELS) ? ((fixture_state_t *)ctx)->calls[channel] : 0U;
}

static unsigned fixture_state_fingerprint(void *ctx)
{
    return ((fixture_state_t *)ctx)->output_mask;
}

static void test_safety_cutout_contract(void)
{
    fixture_state_t                  state;
    safety_cutout_contract_fixture_t fixture = {
        .cutout            = fixture_cutout,
        .reset             = fixture_reset,
        .fail_channel      = fixture_fail_channel,
        .channel_count     = fixture_channel_count,
        .channel_calls     = fixture_channel_calls,
        .state_fingerprint = fixture_state_fingerprint,
        .ctx               = &state,
    };
    char err[160] = {0};

    TEST_ASSERT_EQUAL_INT(SW_OK, safety_cutout_contract_run(&fixture, err, sizeof(err)));
}

int main(void)
{
    UNITY_BEGIN();
    WDF_RUN_TEST(test_safety_cutout_contract, "SAFE-09/SAFE-10", "验证安全切断适配器契约套件");
    return UNITY_END();
}
