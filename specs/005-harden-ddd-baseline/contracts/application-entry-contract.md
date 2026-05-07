# 契约：统一应用层正式入口

## 目标

定义正式命令进入系统时必须遵守的统一入口约束，确保写命令与 `status` 都由应用层正式入口承接，但保持各自语义边界。

## 命令受理契约

| 命令 | 正式入口 | 是否入 trigger 队列 | 后续路径 | 备注 |
|------|------|------|------|------|
| `start` | 是 | 是 | `prepare -> submit -> main_loop_run -> finalize` | 正式写命令 |
| `stop` | 是 | 是 | `prepare -> submit -> main_loop_run -> finalize` | 正式写命令 |
| `feedback` | 是 | 是 | `prepare -> submit -> main_loop_run -> finalize` | 正式写命令 |
| `fault` | 是 | 是 | `prepare -> submit -> main_loop_run -> finalize` | 正式写命令 |
| `fault clear` | 是 | 是 | `prepare -> submit -> main_loop_run -> finalize` | 正式写命令 |
| `status` | 是 | 否 | `prepare -> 只读查询 -> finalize` | 正式只读命令 |

## 入口约束

1. 正式 `stdin` 命令不得绕过统一应用层正式入口直接调用业务核心。
2. `status` 也属于正式入口的一部分，但不得为了“统一”而强制变成 trigger 事件。
3. 内部 helper 可以驱动正式入口，或调用只读查询观察状态，但不得构成新的正式主路径。
4. 兼容性 use case 可以保留文件，但不再代表生产路径中的正式主入口。

## 验证准则

- 从任一正式命令入口追踪到应用核心时，只能看到一套正式受理路径。
- `status` 命令必须通过统一入口到达只读查询逻辑，且不进入队列。
- CLI 协议文本和对外命令语义保持不变。
