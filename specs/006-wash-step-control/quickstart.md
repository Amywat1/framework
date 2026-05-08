# 快速开始：洗车工步真实控制模型

## 1. 目标

本指南用于验证 `006-wash-step-control` 的设计目标已经落地：

1. 洗车程序已从 `wash_stage` 粗阶段表达升级为强类型工步段模型。
2. JSON 只停留在配置适配层，领域层与应用层不再解释运行期字符串。
3. 主控按“进入段 -> 运行段 -> 退出段”持续推进工步，而不是一次性下发动作。
4. RO 水段和风干段所需执行机构品类已补齐，不再是占位能力。
5. stop/fault/timeout/exit_failure 已纳入统一应用编排与结果投影。

## 2. 前置条件

- 已完成本特性的实现修改
- CMake 和可用的 C17 编译环境可用
- 仓库根目录下可以执行 `ctest`
- 仿真驱动可提供位置、跟随、关闭反馈和故障输入

## 3. 构建与测试

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

若直接联调主控程序，请从仓库根目录启动，避免 `./configs` 相对路径失效：

```bash
./build/src/wash_controller
start wash_step_control_v1
status
```

## 4. 重点验证顺序

### A. 配置适配层

检查：
- JSON 程序配置是否能映射为强类型工步模型
- 非法字段组合是否被明确拒绝
- 旧阶段字段是否不再主导正式语义

预期：
- 字符串只停留在 `adapters/config`
- 不存在运行期字符串比对驱动主控核心

### B. 工步主链路

检查：
- 任一工步是否按进入、运行、退出三段式推进
- 顶刷跟随、侧刷延迟喷药、RO 水、风干是否在运行期持续评估
- 顶刷回零是否在退出段正式执行

预期：
- 本段完成与收尾完成可明确区分
- 不再存在旧阶段一次性下发模型主导结果

### C. 位置与条件语义

检查：
- 相对位置、到车头、到车尾、回零完成是否使用统一业务语义
- 条件控制启停点是否可解释

预期：
- 每个启停点都能说明依据
- 位置失效时不会继续按正常流程推进

### D. 异常与收尾

检查：
- 跟随信号丢失
- 未到目标位置超时
- 顶刷回零失败
- 药剂关闭失败
- RO 水关闭失败
- 风干关闭失败

预期：
- 段内超时与收尾超时结果分离
- 直接中止与先收尾后停机策略可被明确验证

## 5. 建议测试覆盖

- `tests/unit/`：工步段模型、位置触发器、退出动作、异常策略、配置映射校验
- `tests/contract/`：JSON 配置契约、运行状态契约、结果投影契约
- `tests/integration/`：应用层统一工步编排、stop/fault/timeout/exit_failure 收口
- `tests/simulation/`：顶刷整段喷药、侧刷延迟喷药、到尾停药停刷并回零、RO 水完整关闭、风干完整关闭

## 6. 回归矩阵

- US1 配置与模型验收：
  `ctest --test-dir build --output-on-failure -R "test_unit_program_config_validation|test_contract_program_config_adapter"`
- US2 工步主链路验收：
  `ctest --test-dir build --output-on-failure -R "test_contract_segment_runtime|test_integration_segment_runtime|test_sim_segment_runtime_control"`
- US3 位置语义与异常收尾验收：
  `ctest --test-dir build --output-on-failure -R "test_contract_position_exit|test_integration_segment_exception_paths|test_sim_segment_exception_behavior"`
- HIL/CLI 冒烟入口：
  `tests/hil/test_hil_smoke.sh`

## 7. 本次实现落点

- 有效样例程序已放入 `configs/programs/wash_step_control_v1.json`
- `tests/fixtures/wash_step_control/` 提供合法/非法配置夹具
- `conditional_controls v1` 仅接受药剂窗口控制
- RO 水与风干已补齐仿真执行机构与关闭反馈/超时兜底语义

## 6. 通过标准

- 顶刷段、侧刷段、RO 水段、风干段主流程通过
- 条件控制启停点与配置语义一致
- 退出动作全部有明确完成判定
- 不存在“工步已配置但执行机构能力缺失”的未闭合情况
- 与新工步段模型冲突的旧阶段有效语义数量为 0
