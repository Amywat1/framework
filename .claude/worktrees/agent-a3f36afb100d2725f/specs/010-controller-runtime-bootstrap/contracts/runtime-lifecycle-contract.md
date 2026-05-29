# 契约：Runtime 生命周期

## 目标

固定正式主控运行实例的唯一生命周期入口，避免调用方继续分散地拼装 `system_context`、适配器和 scheduler。

## 公开能力

- `controller_runtime_create(...)`
- `controller_runtime_run(...)`
- `controller_runtime_destroy(...)`
- 必要的只读状态观测接口

## 生命周期契约

### `create`

- 输入必须包含创建正式实例所需的全部必需绑定与路径参数。
- 同一进程同一时刻只允许存在 1 个正式 runtime 实例。
- 成功返回时：
  - `system_context` 已 acquire
  - 端口已绑定
  - 程序仓储已初始化
  - 事件日志已初始化
  - scheduler 已创建
  - runtime 状态为“已创建未运行”
- 失败返回时：
  - 必须提供明确 `operation_result_t`
  - 不得留下有效 scheduler 绑定
  - 不得留下有效 runtime 实例占用
  - 若失败原因为“已有正式实例未销毁”，不得破坏现有实例

### `run`

- 只允许对已成功 create 的 runtime 调用。
- 不负责再做装配或补建资源。
- 运行阶段仍沿用当前阻塞式 run loop 模型。
- 正常退出返回成功；运行期错误、生命周期误用或内部故障返回失败。
- 一旦 `run` 返回，实例必须进入终态，只允许 `destroy`。

### `destroy`

- 可以销毁完全创建成功的实例。
- 可以销毁部分 create 成功后失败的实例。
- 可以安全处理空对象或已销毁对象。
- 必须按逆序清理 runtime owned 资源。

## 禁止行为

- 正式入口外的代码直接手工串联 `system_context` 获取、适配器初始化和 scheduler 创建，作为新的主路径。
- `run` 隐式创建 scheduler、日志或仓储。
- 同一进程内并存多个正式 runtime 实例。
- 对已返回的 runtime 实例再次调用 `run`。
- `destroy` 越权销毁调用方拥有的驱动上下文、stdio 或测试桩对象。
