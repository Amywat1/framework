# Contract：`controller_scheduler` 平台抽象

## 目的

定义平台层调度器对上暴露的最小语义边界，确保上层只依赖“启动运行时控制器”而不依赖 Linux 等待原语。

## 参与者

- `main.c`：组合根与启动方
- `controller_scheduler`：平台抽象
- Linux 私有实现：`epoll + timerfd + eventfd + CLOCK_MONOTONIC`
- `system_context_t`：被驱动的运行态组合根

## 契约规则

1. 上层只能通过调度器配置、启动、停止、只读观测接口使用调度器。
2. 调度器公开接口不得暴露 `epoll fd`、`timerfd`、`eventfd` 或平台私有事件结构。
3. 调度器必须独占等待、唤醒、退出和事件源注册逻辑。
4. 调度器必须通过 `main_loop_advance_time()` 与 `main_loop_run()` 驱动领域推进，不得绕过现有单拍入口直接改领域状态。
5. 调度器失败时必须提供明确错误结论和最近错误原因。

## 可验证结果

- `main.c` 只保留装配/启动逻辑
- `system_context_t` 中没有 OS 句柄
- 平台层头文件对外不泄露 Linux 等待原语

## 失败语义

- 调度器初始化失败：启动直接失败，不进入运行态
- 周期源或等待原语失效：进入 `failed`
- 退出请求到来：进入 `draining` 或 `stopped`
