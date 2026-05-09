# 快速开始：主控骨架边界收紧

## 1. 目标

本指南用于验证 `007-controller-boundary-hardening` 的设计目标已经落地：

1. `system_context_t` 已被固定为应用层组合根，而不是领域层公共依赖。
2. 全局故障、最近结果、触发队列和当前时间都已收口到固定写入口。
3. 领域服务继续通过最小依赖切片工作，不直接依赖总上下文。
4. 只读查询不会改写关键运行状态。
5. 会话结束后最终状态落点唯一，可解释且可回归验证。

## 2. 前置条件

- 已完成本特性的实现修改
- CMake、可用的 C17 编译环境和 `ctest` 可用
- 仓库根目录下可构建并运行当前主控程序
- 现有模拟驱动可支撑主循环、触发事件和查询场景回归

## 3. 构建与测试

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

若需要从 CLI 角度做人工回归，可在仓库根目录启动主控程序并执行典型命令路径：

```bash
./build/src/wash_controller
status
start wash_step_control_v1
fault e_stop
status
```

## 4. 重点验证顺序

### A. 组合根边界

检查：
- `include/application/coordinators/system_context.h` 是否保持 opaque，不再公开关键状态字段
- 私有布局是否只保留在 `src/application/coordinators/system_context_private.h`
- 领域服务是否继续只接收 `*_service_args_t` 切片
- 新增局部临时数据是否没有回流成总上下文字段

预期：
- 新代码仅凭 public 头文件无法直接改写关键共享状态
- 不存在新的领域服务直接依赖 `system_context_t`
- 不存在无拥有者说明的共享状态字段

### B. 固定写入口

检查：
- `global_fault_*` 是否只通过正式故障入口改写
- 最近结果与迁移记录是否只通过 `runtime_event_recorder_*()` 落点
- `pending_triggers`、`pending_trigger_count`、`current_time_ms` 是否只由 `main_loop_*()` 维护

预期：
- 关键运行状态的写入口数量受控且可直接定位
- 不存在查询、适配或领域服务顺手写关键状态的路径

### C. 只读查询

检查：
- 执行 `query_wash_session_status_execute()` 前后，关键运行状态是否保持不变
- `status` 命令路径是否只返回当前视图，而不刷新最近结果或故障状态

预期：
- 只读查询副作用数量为 0
- 查询路径不进入业务结果写入通道

### D. 会话结束状态落点

检查：
- 正常结束时，会话最终结果是否只落在 `wash_session.final_session_result`
- 异常结束时，最近结果、迁移记录和会话终态是否保持一致解释

预期：
- 最终状态落点唯一
- 不存在多个运行对象对同一次结束给出互相冲突的最终解释

## 5. 建议测试覆盖

- `tests/contract/`：`test_contract_system_context_boundary`、`test_contract_trigger_runtime_ownership`、`test_contract_session_finalization`、`test_contract_status_query_readonly`
- `tests/integration/`：`test_integration_formal_entry_ownership`、`test_integration_compat_entrypoint_removal`、`test_integration_unique_session_result_sink`、`test_integration_status_query_no_side_effect`
- `tests/unit/`：`test_unit_service_arg_slices`、`test_unit_runtime_object_ownership`
- `tests/simulation/`：`test_sim_controller_global_fault_behavior`、`test_integration_session_status_observability`

## 6. 回归矩阵

- 组合根与最小依赖切片：
  `ctest --test-dir build --output-on-failure -R "test_contract_system_context_boundary|test_unit_service_arg_slices"`
- 固定写入口：
  `ctest --test-dir build --output-on-failure -R "test_contract_trigger_runtime_ownership|test_integration_formal_entry_ownership|test_integration_compat_entrypoint_removal"`
- 只读查询：
  `ctest --test-dir build --output-on-failure -R "test_contract_status_query_readonly|test_integration_status_query_no_side_effect"`
- 会话结束落点：
  `ctest --test-dir build --output-on-failure -R "test_contract_session_finalization|test_integration_unique_session_result_sink"`
- 故障入口与可观测性：
  `ctest --test-dir build --output-on-failure -R "test_sim_controller_global_fault_behavior|test_integration_session_status_observability"`

## 7. 本次实现落点

- `system_context_t` 在 public 头中变为 opaque 组合根，完整布局只保留在私有头
- `process_wash_trigger_execute()` 继续承担全局故障与跨对象编排的正式入口职责
- `runtime_event_recorder_*()` 继续承担最近结果与迁移记录的最终投影职责
- `query_wash_session_status_execute()` 明确保持只读语义
- `main_loop_*()` 只负责时间推进与触发队列维护

## 8. 通过标准

- 关键运行状态固定写入口覆盖率达到 100%
- 领域服务直接依赖 `system_context_t` 的新增路径数量为 0
- 只读查询导致的关键状态非预期写入数量为 0
- 会话结束状态落点冲突数量为 0
