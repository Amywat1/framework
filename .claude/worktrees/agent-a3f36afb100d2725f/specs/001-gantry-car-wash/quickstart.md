# 快速开始：往复式龙门洗车机控制软件

## 1. 目标

本指南用于帮助开发与测试人员快速理解并验证主控软件的最小工作闭环：加载程序、选择程序、执行一次仿真洗车周期、触发异常并观察恢复行为。

当前阶段重点验证流程闭环、状态机正确性和安全动作有效性，不把量化时序作为阻塞验收条件。

## 2. 前置条件

- Linux 开发环境可用
- C17 编译器可用
- 已具备项目源码骨架与测试目录
- 具备仿真适配器或可替代的 HIL 接入环境

## 3. 推荐目录约定

```text
configs/
|-- programs/
|   `-- standard_wash.json
|-- vehicle_types.json
`-- system_limits.json

runtime/
|-- logs/
`-- state/
```

## 4. 构建最小闭环

1. 生成构建目录：

```bash
cmake -S . -B build
cmake --build build
```

2. 启动主控守护进程与仿真适配器：

```bash
./build/bin/wash_controller --config ./configs
./build/bin/wash_simulator --scenario normal_cycle
```

3. 通过本地操作接口手动选择程序并启动一次洗车周期：

```bash
./build/bin/wash_cli start --program standard_wash
```

4. 观察以下结果：

- 预检通过
- 车辆自动适配判定通过
- 龙门执行往返
- 顶刷/侧刷按阶段动作
- 多路药剂按配置投加
- 周期正常结束并写出结果日志
- 运行日志输出到 `runtime/logs/`

## 5. 故障验证建议

### 药剂不足

```bash
./build/bin/wash_simulator fault --type chemical_low --channel foam
```

预期：
- 触发资源类故障
- 按程序策略执行降级、跳过或安全结束
- 产生中文告警事件

### 急停

```bash
./build/bin/wash_simulator fault --type emergency_stop
```

预期：
- 立即进入安全停机
- 撤销危险动作
- 周期结束后回到安全待机
- 重新启动前必须重新预检

### 断电恢复

```bash
./build/bin/wash_simulator fault --type power_loss
```

预期：
- 恢复后不允许断点续洗
- 系统回到安全待机
- 重新执行启动前预检后，操作员才能重新选择程序并启动

## 6. 测试建议

```bash
ctest --test-dir build --output-on-failure
```

建议至少覆盖：
- 领域状态机单测
- 程序配置校验测试
- 故障分级策略测试
- 操作命令契约测试
- 仿真场景集成测试
