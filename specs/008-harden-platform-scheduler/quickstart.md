# Phase 1 快速开始：平台层调度器收敛

## 目标

按“单线程领域推进 + 平台层调度器壳层”完成实现，并验证以下结果：

- `main.c` 收敛成纯组合根
- Linux 等待/唤醒收敛到 `controller_scheduler`
- 周期事件稳定驱动 `main_loop_run()`
- 命令/通知/退出事件遵守分类语义
- 调度器指标可观测

## 实施顺序

1. 新增平台调度器抽象与 Linux 私有实现骨架
2. 将 `main.c` 的等待循环迁移到调度器
3. 调整正式命令路径，去掉命令入口自带的持续推进循环
4. 接入命令/通知/退出三类事件源
5. 扩展日志和状态输出到调度器观测
6. 补齐 contract / integration / simulation 测试

## 代码落点

优先关注以下区域：

- `include/platform/`：新增 `controller_scheduler` 抽象
- `include/platform/linux/`、`src/platform/linux/`：Linux 调度器实现
- `src/main/main.c`：收敛为组合根
- `src/application/use_cases/process_formal_command.c`：去掉命令路径自带 drain/loop
- `src/adapters/inbound/cli_command_adapter.c`：保持输入适配职责
- `tests/contract/`、`tests/integration/`、`tests/simulation/`：新增调度语义验证

## 构建与验证

```powershell
cmake -S . -B build
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

若使用当前仓库既有 WSL 工作流，可执行：

```powershell
wsl.exe bash -lc "cd /mnt/c/Users/HUAWEI/Desktop/Code-M8 && cmake --build build -j2 && cd build && ctest --output-on-failure"
```

## 建议验证清单

1. 运行 contract tests，确认调度器边界、事件分类和 `main_loop_*()` 责任没有回退。
2. 运行 integration tests，确认命令事件仍可启动洗车、状态查询保持只读、退出路径可预测。
3. 运行 simulation tests，确认固定控制周期能持续推进领域，并在积压场景下保持单拍有界。
4. 查看运行日志/状态输出，确认可读到周期次数、超拍次数、事件源计数和最近错误原因。

## 完成判定

- `main.c` 中不再保留 `select`、外层事件循环和无限 `drain_runtime()`
- 领域推进只由调度器触发 `main_loop_advance_time()` / `main_loop_run()`
- 通知路径不直接改领域状态
- 所有新增调度语义测试通过
