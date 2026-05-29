# Contract：`main.c` 与 `main_loop_*()` 边界

## 目的

保持“主程序只装配与启动，`main_loop_*()` 只做单拍推进”的双边界，不让等待和业务推进再次耦合。

## 契约规则

1. `main.c` 只负责：
   - 创建 `system_context_t`
   - 装配驱动、仓储和日志端口
   - 构造调度器配置
   - 启动调度器
   - 退出时释放资源
2. `main.c` 不再负责：
   - `select` / `epoll` 等等待逻辑
   - 外层事件循环
   - 周期计算
   - `fd` 级事件分发
   - 无限 `drain_runtime()`
3. `main_loop_advance_time()` 只负责推进运行时钟。
4. `main_loop_run()` 只负责一次有界单拍推进，可消费待处理触发并执行一次运行时 tick。
5. 正式命令路径不得通过持续循环直接拥有主调度权；是否补跑由调度器决定。

## 可验证结果

- `main.c` 中不再出现 `select`、`wait_timeout` 和无限 drain 模式
- `main_loop_*()` 不包含等待原语
- 命令路径不再是事实上的第二调度器
