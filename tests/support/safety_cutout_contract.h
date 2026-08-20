#ifndef TESTS_SUPPORT_SAFETY_CUTOUT_CONTRACT_H
#define TESTS_SUPPORT_SAFETY_CUTOUT_CONTRACT_H

#include "common/sw_error.h"

#include <stddef.h>

/** @brief 项目安全切断实现的契约测试夹具。 */
typedef struct {
    sw_err_t (*cutout)(void *ctx);
    void (*reset)(void *ctx);
    void (*fail_channel)(void *ctx, unsigned channel, sw_err_t error);
    unsigned (*channel_count)(void *ctx);
    unsigned (*channel_calls)(void *ctx, unsigned channel);
    unsigned (*state_fingerprint)(void *ctx);
    void *ctx;
} safety_cutout_contract_fixture_t;

/**
 * @brief  验证切断幂等、失败后继续全部通道并返回首个错误
 * @param  fixture  项目提供的测试夹具
 * @param  err      失败说明缓冲，可为 NULL
 * @param  err_size err 缓冲容量
 * @retval SW_OK 契约通过
 * @retval SW_ERR_PARAM fixture 不完整
 * @retval SW_ERR_STATE 契约断言失败
 */
sw_err_t safety_cutout_contract_run(const safety_cutout_contract_fixture_t *fixture, char *err, size_t err_size);

#endif
