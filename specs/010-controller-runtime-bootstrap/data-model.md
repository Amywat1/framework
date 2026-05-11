# 数据模型：主控 Runtime/Bootstrap 正式入口

## 1. Controller Runtime

**用途**  
表示一个正式主控运行实例，承载从创建成功到销毁完成的生命周期状态，并拥有 runtime 自己创建的资源。

**核心字段**

- `system_context_t system_context`
  - 运行实例占用的组合根句柄
  - 由 runtime 获取并释放
- `controller_scheduler_t *scheduler`
  - 正式调度器实例
  - 由 runtime 创建并销毁
- `controller_runtime_status_t status`
  - 生命周期状态
  - 至少覆盖 `UNINITIALIZED`、`CREATED`、`RUNNING`、`TERMINATED`、`DESTROYED`
- `controller_runtime_config_t config_snapshot`
  - create 时确认后的关键配置快照
  - 用于销毁时校验和必要观测
- `runtime_owned_flags`
  - 标记哪些资源已成功创建并需要在 destroy 时回收
- `bool run_invoked`
  - 标记该实例是否已经进入过运行
  - 用于拒绝重复 `run`

**约束**

- `status=CREATED` 前，`scheduler` 不得暴露为可运行实例。
- `status=TERMINATED` 后，只允许 `destroy`，不得再次 `run`。
- `status=DESTROYED` 后，不得继续表现为有效运行实例。
- 任何 create 失败场景都不能残留“看起来像 CREATED”的半初始化对象。

## 2. Controller Runtime Config

**用途**  
描述创建正式运行实例所需的全部注入输入。

**核心字段**

- `sensor_port_t *sensor_port`
- `actuator_port_t *actuator_port`
- `controller_scheduler_config_t *scheduler_config`
- `controller_scheduler_linux_stdio_t *stdio_binding` 或等价输入
- `const char *config_root`
- `const char *event_log_path`
- 调用方拥有的驱动上下文或测试桩引用
- 运行模式相关开关
  - 正式运行与测试运行可共用结构，但允许注入差异

**约束**

- 所有正式必需项在 create 前必须通过参数校验。
- config 只描述输入，不转移调用方资源的销毁权。
- 正式入口和测试入口允许注入不同对象，但必须复用同一套 create 流程。
- config 不得试图定义多正式实例并存语义；正式入口始终受单实例约束。

## 3. Startup Result

**用途**  
描述 runtime create 的正式结果语义。代码中可以直接复用 `operation_result_t`，但在设计层需要明确其状态含义。

**状态**

- `success`
  - runtime 已完成全部装配
  - 状态进入 `CREATED`
- `failure`
  - 返回明确错误码
  - 已创建的 runtime owned 资源全部完成回滚
  - runtime 不保留有效占用

**典型失败原因**

- 缺失必要配置
- 端口未绑定
- 程序仓储初始化失败
- 事件日志初始化失败
- scheduler 创建失败
- `system_context` 获取失败
- 同一进程内已有正式 runtime 实例尚未销毁

## 4. Run Result

**用途**  
描述 `controller_runtime_run(...)` 的正式结果语义。

**状态**

- `success`
  - 运行按正常退出条件结束
  - 实例进入 `TERMINATED`
- `failure`
  - 运行期错误、生命周期误用或内部故障
  - 实例进入 `TERMINATED`

**约束**

- 无论成功还是失败，只要 `run` 已返回，实例都不得再次进入运行。
- `run` 的正常退出不得被折叠为失败结果。

## 5. Runtime Ownership Boundary

**用途**  
描述 runtime 在创建、运行和销毁过程中对资源拥有权的正式边界。

**runtime owned**

- `system_context_t` 占用权
- `controller_scheduler_t *`
- runtime 内部生命周期状态

**caller owned**

- 驱动上下文及其内部存储
- `sensor_port_t` / `actuator_port_t` 的来源对象
- stdio 句柄
- 外部路径字符串与配置源
- 测试桩对象

**约束**

- destroy 只能释放 runtime owned 资源。
- runtime 可以解除与 caller owned 资源的绑定，但不能默认销毁它们。

## 6. 生命周期状态转换

```text
UNINITIALIZED
    |
    | create success
    v
CREATED
    |
    | run enter
    v
RUNNING
    |
    | run return
    v
TERMINATED
    |
    | destroy
    v
DESTROYED
```

**异常路径**

- `UNINITIALIZED -> create failure -> DESTROYED/未占用`
  - 逻辑上必须回到“无有效实例占用”的安全状态
- `CREATED -> create again`
  - 必须因单实例约束失败，不得生成第二个正式实例
- `CREATED -> destroy -> DESTROYED`
- `RUNNING -> run return(success|failure) -> TERMINATED`
- `TERMINATED -> run`
  - 必须因生命周期误用失败
- `TERMINATED -> destroy -> DESTROYED`
- `DESTROYED -> destroy`
  - 安全返回，不应造成二次释放

## 7. 共享装配模型

**用途**  
描述正式入口与测试入口如何共用同一套 bootstrap 过程。

**共享部分**

- `system_context` 获取
- 端口绑定注入
- 仓储初始化
- 日志初始化
- scheduler 创建
- destroy 逆序清理

**可替换注入部分**

- 模拟驱动 / 测试桩 / 正式驱动上下文
- 配置路径与日志路径
- stdio 绑定
- scheduler 参数

**约束**

- 测试辅助不能再复制正式装配步骤，只能构造不同 `config` 注入并调用同一正式 create。
