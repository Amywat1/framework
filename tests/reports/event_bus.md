# event_bus 单元测试报告

| 属性 | 值 |
|------|-----|
| 测试目标 | `test_event_bus` |
| 源文件 | `tests/runtime/test_event_bus.c` |
| 被测代码 | `runtime/event_bus/event_bus.c`、`common/time_util.c` |
| 依赖 | pthread、sem |
| 最后执行 | 2026-07-09 |
| 结果 | **8/8 通过** |

## 测试环境

- 分发线程由测试代码 `pthread_create` 启动，调用 `event_bus_dispatch_loop()`
- 用例间通过 `event_bus_init()` / `event_bus_shutdown()` + `pthread_join` 隔离
- 异步可见性通过 `volatile` 标志 + `usleep` 保证

## 用例明细

### 1. 初始化守卫

| 用例 | `test_not_init_guard` |
|------|----------------------|
| 目的 | 验证未 init 时 API 返回 `SW_ERR_NOT_INIT` |
| 前置 | 不调用 `event_bus_init()` |
| 验证点 | `event_subscribe`、`event_publish`、`event_bus_shutdown` 均返回 `SW_ERR_NOT_INIT` |
| 结果 | 通过 |

### 2. 发布与订阅

| 用例 | `test_publish_subscribe` |
|------|--------------------------|
| 目的 | 验证 publish → dispatch → handler 完整链路 |
| 前置 | init + subscribe `EVT_ALARM_TRIGGERED` + 启动 dispatch 线程 |
| 验证点 | handler 被调用 1 次；`param=8100`；`timestamp_ms > 0` |
| 结果 | 通过 |

### 3. 队列满丢弃

| 用例 | `test_queue_full` |
|------|-------------------|
| 目的 | 验证普通队列满时返回 `SW_ERR_OVERFLOW` 并统计丢弃数 |
| 前置 | init 后连续 publish `EVENT_BUS_QUEUE_SIZE + 5` 次 |
| 验证点 | 最后一次返回 `SW_ERR_OVERFLOW`；`queue_depth == 64`；`dropped_count == 5` |
| 结果 | 通过 |

### 4. 多订阅者

| 用例 | `test_multi_subscriber` |
|------|-------------------------|
| 目的 | 验证同一事件类型的多个 handler 均收到回调 |
| 前置 | 两个 handler 订阅 `EVT_SAFETY_LOCKOUT` |
| 验证点 | `handler1`、`handler2` 各被调用 1 次 |
| 结果 | 通过 |

### 5. FIFO 顺序

| 用例 | `test_fifo_order` |
|------|-------------------|
| 目的 | 验证同类型事件按入队顺序分发 |
| 前置 | 连续 publish param=10/20/30 |
| 验证点 | handler 收到顺序为 10 → 20 → 30 |
| 结果 | 通过 |

### 6. 事件隔离

| 用例 | `test_event_isolation` |
|------|------------------------|
| 目的 | 验证不同事件类型的订阅互不干扰 |
| 前置 | 分别订阅 `EVT_ALARM_TRIGGERED` 和 `EVT_CLOUD_CONNECTED` |
| 验证点 | 只 publish 报警事件时，仅 handler1 被调用 |
| 结果 | 通过 |

### 7. 重复订阅幂等

| 用例 | `test_subscribe_idempotent_and_stats` |
|------|---------------------------------------|
| 目的 | 验证同一 handler 重复订阅幂等，统计值正确 |
| 前置 | 同一 handler 订阅两次 + 另一 handler 订阅一次 |
| 验证点 | `subscribe_count == 2`；publish 后两个 handler 各收到 1 次 |
| 结果 | 通过 |

### 8. shutdown 排空队列

| 用例 | `test_shutdown_drains_queue` |
|------|------------------------------|
| 目的 | 验证 shutdown 后 dispatch 线程排空队列并正常退出 |
| 前置 | publish 3 个事件后调用 shutdown + join |
| 验证点 | 3 个事件全部分发；`published_count == dispatched_count == 3`；`queue_depth == 0` |
| 结果 | 通过 |

## 未覆盖项

| 场景 | 说明 |
|------|------|
| 高优先级队列路由 | `EVT_SAFETY_*`、`EVT_HW_ESTOP_*` 优先分发尚未单测 |
| `event_subscribe_table` | 批量订阅 API 未覆盖 |
| `event_bus_set_fatal_cb` | fatal 回调路径未覆盖 |
| sem 故障注入 | `sem_post`/`sem_wait` 失败路径未覆盖 |

## 待补充

> 新增用例时在此追加，格式参考上方表格。

| 用例 | 目的 | 状态 |
|------|------|------|
| — | — | — |
