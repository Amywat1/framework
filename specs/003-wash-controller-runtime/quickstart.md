# 快速开始：洗车主控运行骨架

## 1. 目标

本指南用于验证新的主控骨架已经成立：  
1. `wash_controller` 启动后常驻运行；  
2. 通过 `stdin` 接收正式命令；  
3. 任务由输入事件和等待超时持续推进；  
4. 启动前预检可以阻断无效启动；  
5. `status` 能看见当前状态、等待条件和全局故障信息。

## 2. 前置条件

- Linux 或兼容开发环境可用
- C17 编译器与 CMake 可用
- 已具备至少一份可用程序配置，例如 `standard_wash`
- 仿真驱动和基础测试可运行

## 3. 构建

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## 4. 启动主控

```bash
./build/src/wash_controller
```

预期结果：

- 进程启动后保持运行
- 没有命令输入时继续等待，不自行退出
- 同一进程内可连续处理多单任务

## 5. 正式命令协议

```text
start <program_id>
stop
feedback <signal_code>
fault <fault_code> <reason>
fault clear
status
```

约束：

- 每条命令都必须返回一条单行结果到 `stdout`
- 未知命令或参数缺失返回单行错误，但主控继续运行
- `status` 返回单行状态摘要

## 6. 手工验证场景

### 场景 A：正常启动并查询状态

1. 输入 `start standard_wash`
2. 输入 `status`

预期：

- `start` 返回已受理结果
- 主控创建活动任务
- `status` 可看到会话状态、执行状态、当前阶段和最近原因

### 场景 B：运行中提交反馈推进流程

1. 启动任务
2. 按当前等待阶段输入匹配的 `feedback <signal_code>`
3. 重复查询 `status`

预期：

- 匹配反馈能够推进执行
- 不匹配或迟到反馈不会错误推进流程
- 状态查询持续可用

### 场景 C：启动前预检失败

1. 构造预检失败条件
2. 输入 `start standard_wash`
3. 输入 `status`

预期：

- 启动被拒绝
- 单行结果直接带出失败原因
- 主控继续运行并等待下一条命令

### 场景 D：空闲时上报全局故障并清除

1. 确保当前没有活动任务
2. 输入 `fault E_STOP emergency_pressed`
3. 输入 `status`
4. 输入 `start standard_wash`
5. 输入 `fault clear`
6. 再次输入 `start standard_wash`

预期：

- 第 2 步记录全局故障
- 第 3 步显示无活动任务且存在全局故障
- 第 4 步因全局故障被拒绝
- 第 5 步清除全局故障
- 第 6 步恢复可启动

### 场景 E：等待超时驱动推进

1. 启动任务
2. 在等待阶段不输入反馈
3. 观察一段时间后再次输入 `status`

预期：

- 主循环持续发现并处理等待超时
- 任务进入超时后的明确路径
- `status` 能反映最新状态或最近原因

### 场景 F：连续任务

1. 完成或中止第一单
2. 在同一进程内再次输入 `start standard_wash`

预期：

- 无需重启主控
- 第二单可在同一进程内被创建并推进
- 第一单残留状态不会污染第二单

## 7. 最低测试覆盖建议

- 单元测试：命令解析、全局故障规则、预检受理规则、状态视图映射
- 集成测试：常驻主循环、多次任务、预检失败、空闲 fault 阻断/清除
- 契约测试：`stdin/stdout` 命令协议、`status` 输出、错误命令结果
- 仿真测试：正常、停止、故障、超时、连续任务
