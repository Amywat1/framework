# Quickstart：使用正式 Runtime/Bootstrap 入口

## 1. 创建正式运行实例

1. 准备调用方拥有的驱动上下文、端口对象、stdio 和路径参数。
2. 组装 `controller_runtime_config`。
3. 调用 `controller_runtime_create(...)`。
4. 仅当返回成功时，实例才进入“已创建未运行”状态。
5. 若同一进程中已有一个正式 runtime 实例尚未销毁，本次创建必须显式失败。

## 2. 进入运行

1. 对创建成功的实例调用 `controller_runtime_run(...)`。
2. `run` 不再补做端口绑定、仓储初始化或 scheduler 创建。
3. 正常退出返回成功；若运行失败，通过正式结果和状态观测接口读取失败原因。
4. 一旦 `run` 返回，该实例只能进入 `destroy`，不能再次 `run`。

## 3. 销毁实例

1. 运行结束后调用 `controller_runtime_destroy(...)`。
2. destroy 负责逆序清理 runtime owned 资源。
3. destroy 不负责销毁调用方拥有的驱动上下文、stdio 或测试桩。

## 4. 验证启动失败回滚

建议至少覆盖以下场景：

- 配置路径错误时，`controller_runtime_create(...)` 显式失败。
- 日志路径不可用时，`controller_runtime_create(...)` 显式失败。
- scheduler 创建失败时，`controller_runtime_create(...)` 显式失败。
- 已有正式 runtime 实例未销毁时，第二次 `controller_runtime_create(...)` 显式失败。
- 失败后立即再次创建新实例仍可成功。

## 5. 测试复用正式装配流程

1. 测试侧构造不同的驱动上下文、日志路径、stdio 和 scheduler 参数。
2. 通过同一个 `controller_runtime_create(...)` 创建实例。
3. 不在 `tests/test_support.h` 里重新复制 `system_context` 获取、适配器初始化和 scheduler 创建逻辑。
4. 测试也必须遵守“单正式实例、`run` 一次性、正常退出成功”的正式语义。
