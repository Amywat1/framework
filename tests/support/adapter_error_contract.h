#ifndef TESTS_SUPPORT_ADAPTER_ERROR_CONTRACT_H
#define TESTS_SUPPORT_ADAPTER_ERROR_CONTRACT_H

#include "common/sw_error.h"

#include <stddef.h>

/** @brief 项目适配器错误透传契约测试夹具。 */
typedef struct {
    sw_err_t (*invoke)(void *ctx, sw_err_t injected_error);
    void *ctx;
} adapter_error_contract_fixture_t;

/**
 * @brief  验证硬件、通信、存储错误均原样返回调用方
 * @param  fixture  项目提供的测试夹具
 * @param  err      失败说明缓冲，可为 NULL
 * @param  err_size err 缓冲容量
 * @retval SW_OK 契约通过
 * @retval SW_ERR_PARAM fixture 不完整
 * @retval SW_ERR_STATE 错误被吞掉或改写
 */
sw_err_t adapter_error_contract_run(const adapter_error_contract_fixture_t *fixture, char *err, size_t err_size);

#endif
