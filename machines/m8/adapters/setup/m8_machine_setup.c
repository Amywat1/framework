/**
 * @file    m8_machine_setup.c
 * @brief   M8 机型装配总入口实现。
 */

#include "machines/m8/adapters/setup/m8_machine_setup.h"

#include "machines/m8/adapters/setup/m8_motor_exec.h"
#include "machines/m8/adapters/setup/m8_motor_domains_setup.h"
#include "machines/m8/adapters/setup/m8_water_setup.h"
#include "common/log.h"

sw_err_t m8_machine_setup(void)
{
    sw_err_t ret;

    ret = m8_motor_exec_init();
    if (ret != SW_OK) {
        LOG_ERROR("m8_machine_setup: m8_motor_exec_init failed ret=%d", (int)ret);
        return ret;
    }

    ret = m8_motor_domains_setup();
    if (ret != SW_OK) {
        LOG_ERROR("m8_machine_setup: m8_motor_domains_setup failed ret=%d", (int)ret);
        return ret;
    }

    ret = m8_water_setup();
    if (ret != SW_OK) {
        LOG_ERROR("m8_machine_setup: m8_water_setup failed ret=%d", (int)ret);
        return ret;
    }

    LOG_INFO("m8_machine_setup ok");
    return SW_OK;
}
