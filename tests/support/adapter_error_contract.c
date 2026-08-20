#include "tests/support/adapter_error_contract.h"

#include <stdio.h>

sw_err_t adapter_error_contract_run(const adapter_error_contract_fixture_t *fixture, char *err, size_t err_size)
{
    static const sw_err_t injected[] = {SW_ERR_HW, SW_ERR_COMM, SW_ERR_STORAGE};

    if ((fixture == NULL) || (fixture->invoke == NULL)) {
        if ((err != NULL) && (err_size > 0U)) {
            (void)snprintf(err, err_size, "%s", "适配器错误契约 fixture 不完整");
        }
        return SW_ERR_PARAM;
    }

    for (size_t i = 0U; i < sizeof(injected) / sizeof(injected[0]); ++i) {
        sw_err_t actual = fixture->invoke(fixture->ctx, injected[i]);

        if (actual != injected[i]) {
            if ((err != NULL) && (err_size > 0U)) {
                (void)snprintf(err,
                               err_size,
                               "适配器错误被吞掉或改写: expected=%s actual=%s",
                               sw_err_name(injected[i]),
                               sw_err_name(actual));
            }
            return SW_ERR_STATE;
        }
    }
    return SW_OK;
}
