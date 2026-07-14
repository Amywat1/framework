/**
 * @file    hal_voice_port.c
 * @brief   HAL 语音端口注册表
 */

#include "ports/outbound/hal/hal_voice_port.h"

static const hal_voice_ops_t *s_ops;

void hal_voice_register(const hal_voice_ops_t *ops)
{
    s_ops = ops;
}

const hal_voice_ops_t *hal_voice_get_ops(void)
{
    return s_ops;
}
