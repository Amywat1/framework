/**
 * @file    hal_voice_sim.h
 * @brief   语音模块 HAL 仿真注册接口
 */

#ifndef ADAPTERS_OUTBOUND_HAL_SIM_HAL_VOICE_SIM_H
#define ADAPTERS_OUTBOUND_HAL_SIM_HAL_VOICE_SIM_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief  注册语音模块仿真实现到 hal_voice_port。 */
void hal_voice_sim_register(void);

#ifdef HAL_VOICE_SIM_UNIT_TEST
/** @brief  重置语音仿真内部状态，仅供单元测试使用。 */
void hal_voice_sim_test_reset(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* ADAPTERS_OUTBOUND_HAL_SIM_HAL_VOICE_SIM_H */
