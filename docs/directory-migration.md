# 目录迁移对照

## 已完成（摘要）

- startup → apps/、core/bootstrap
- coordinators 控制核心 → core/runtime、core/tick
- domain/ports → ports/hal、ports/storage
- platform → adapters/os/linux、adapters/hal/sim_hw
- configs → assets/configs
- 后台报警 → adapters/background（含 alarm_detect_job）
- test_support → tests/support

**已删除目录**：include/platform、src/platform、application/coordinators、application/jobs。

**已删除遗留头/源**（未进 CMake）：device_runtime、runtime_event_recorder、runtime_result_projection、重复 control_tick/scheduler_runtime_port、process_formal_command/process_wash_trigger 公开头。

## 当前 application/

- services/、use_cases/（internal/ 为 use case 私有头）

## 当前 ports/

- hal/、storage/、application/inbound、application/outbound

## 后台适配层（adapters/background）

- alarm_evaluator、alarm_detect_job、alarm_monitor

## 历史命名

system_context → control_context；process_wash_trigger → wash_control；process_formal_command → line_command。
## 测试命名

- system_context 测试目标已重命名为 control_context（CMake 目标与源文件名一致）。

