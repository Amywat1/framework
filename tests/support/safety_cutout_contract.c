#include "tests/support/safety_cutout_contract.h"

#include <stdio.h>

static sw_err_t contract_fail(char *err, size_t err_size, const char *message)
{
    if ((err != NULL) && (err_size > 0U)) {
        (void)snprintf(err, err_size, "%s", message);
    }
    return SW_ERR_STATE;
}

sw_err_t safety_cutout_contract_run(const safety_cutout_contract_fixture_t *fixture, char *err, size_t err_size)
{
    unsigned channels;
    unsigned state_before;
    unsigned state_after;
    sw_err_t ret;

    if ((fixture == NULL) || (fixture->cutout == NULL) || (fixture->reset == NULL) || (fixture->fail_channel == NULL)
        || (fixture->channel_count == NULL) || (fixture->channel_calls == NULL)
        || (fixture->state_fingerprint == NULL)) {
        return contract_fail(err, err_size, "安全切断契约 fixture 不完整");
    }

    fixture->reset(fixture->ctx);
    state_before = fixture->state_fingerprint(fixture->ctx);
    if (fixture->cutout(fixture->ctx) != SW_OK) {
        return contract_fail(err, err_size, "无故障切断未返回 SW_OK");
    }
    state_after = fixture->state_fingerprint(fixture->ctx);
    if (state_before == state_after) {
        return contract_fail(err, err_size, "切断未改变安全输出状态");
    }
    state_before = state_after;
    if (fixture->cutout(fixture->ctx) != SW_OK) {
        return contract_fail(err, err_size, "重复切断未保持 SW_OK");
    }
    if (fixture->state_fingerprint(fixture->ctx) != state_before) {
        return contract_fail(err, err_size, "重复切断不是幂等操作");
    }

    channels = fixture->channel_count(fixture->ctx);
    if (channels == 0U) {
        return contract_fail(err, err_size, "fixture 未提供切断通道");
    }
    fixture->reset(fixture->ctx);
    fixture->fail_channel(fixture->ctx, 0U, SW_ERR_HW);
    if (channels > 1U) {
        fixture->fail_channel(fixture->ctx, channels - 1U, SW_ERR_COMM);
    }
    ret = fixture->cutout(fixture->ctx);
    if (ret != SW_ERR_HW) {
        return contract_fail(err, err_size, "多路失败时未返回首个错误码");
    }
    for (unsigned i = 0U; i < channels; ++i) {
        if (fixture->channel_calls(fixture->ctx, i) != 1U) {
            return contract_fail(err, err_size, "一路失败时未尽力执行全部切断通道");
        }
    }

    fixture->reset(fixture->ctx);
    fixture->fail_channel(fixture->ctx, channels - 1U, SW_ERR_STORAGE);
    ret = fixture->cutout(fixture->ctx);
    if (ret != SW_ERR_STORAGE) {
        return contract_fail(err, err_size, "末路通道错误未原样返回");
    }
    for (unsigned i = 0U; i < channels; ++i) {
        if (fixture->channel_calls(fixture->ctx, i) != 1U) {
            return contract_fail(err, err_size, "末路失败时未执行全部切断通道");
        }
    }
    return SW_OK;
}
