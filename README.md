# Code-M8

当前程序是一个面向龙门洗车控制器的单进程主控骨架。它的目标不是把所有细节都堆在入口文件中，而是把“启动装配、运行时生命周期、平台调度、单拍推进、业务编排、领域规则、外部适配”拆成清晰的层次，让开发者能够在不混淆职责的前提下理解和扩展系统。

## 项目定位

当前实现可以概括为三句话：

- 这是一个以 `wash_controller` 为正式入口的常驻主控程序。
- 它通过固定周期调度和标准输入命令驱动洗车流程运行。
- 它把 Linux 事件循环、运行时状态、业务流程推进和外部适配做了明确分层。

从运行形态看，它是单进程、单线程主控骨架；从架构风格看，它接近“领域核心 + 应用编排 + 端口适配 + 平台调度”的实现方式。

## 推荐阅读顺序

如果第一次接触代码，建议按下面顺序阅读：

1. `src/main/main.c`
2. `include/application/coordinators/controller_runtime.h`
3. `src/application/coordinators/controller_runtime.c`
4. `include/application/coordinators/system_context.h`
5. `src/application/coordinators/system_context.c`
6. `include/platform/controller_scheduler.h`
7. `src/platform/linux/controller_scheduler_linux.c`
8. `src/platform/linux/stdio_formal_command_adapter.c`
9. `include/application/coordinators/main_loop.h`
10. `src/application/coordinators/main_loop.c`
11. `src/application/use_cases/process_formal_command.c`
12. `src/application/use_cases/process_wash_trigger.c`
13. `src/domain/services/wash_execution_service.c`

这条顺序基本对应当前程序的真实运行链路。

## 框架分层

### 1. 进程入口层

`src/main/main.c` 只负责最小化启动装配：

- 初始化仿真驱动端口
- 准备调度配置
- 组装 `controller_runtime_config_t`
- 调用 `controller_runtime_create()`、`controller_runtime_run()`、`controller_runtime_destroy()`

`main` 不再直接持有业务大循环，也不直接操作领域对象。它只是正式入口。

### 2. Runtime 生命周期层

`controller_runtime` 是当前程序的正式运行时外壳，负责：

- 校验运行配置
- 获取正式 `system_context`
- 装配传感器端口、执行机构端口、程序仓储和事件日志
- 创建平台调度器
- 驱动调度器运行
- 在退出时逆序销毁资源

可以把它理解成“把一个可运行的主控进程真正拉起来”的那一层。

### 3. 运行时组合根

`system_context` 是整个主控的运行时组合根。它承载当前程序运行需要保留的核心状态，包括：

- 当前洗车会话
- 当前执行段状态
- 等待条件与超时信息
- 冻结后的程序快照
- 待处理 trigger 队列
- 当前逻辑时间
- 全局故障状态
- 最近结果和原因
- 已装配的外部端口

当前 `system_context` 已经不是对外暴露内部结构的普通对象，而是一个正式句柄。外部代码只允许通过公开接口或私有构建函数访问它，这样可以把生命周期、单实例占用和状态边界统一收口。

### 4. 平台调度层

`controller_scheduler` 负责“什么时候推进业务”，不负责“业务上应该发生什么”。当前 Linux 实现位于 `src/platform/linux/controller_scheduler_linux.c`，它统一管理：

- 固定控制周期
- 命令输入事件的 fd 就绪通知
- 唤醒与退出流程
- 调度状态和运行指标

具体 stdin/stdout 命令来源适配由 `src/platform/linux/stdio_formal_command_adapter.c` 承担。调度器只感知“命令 fd 就绪”，不再直接处理命令缓冲、协议执行和响应输出。这层屏蔽了底层 `epoll`、`timerfd`、`eventfd` 等 Linux 细节，使上层只感知“被调度”而不直接依赖 OS 等待机制。

### 5. 单拍运行内核

`main_loop` 是单拍推进器，而不是外层事件循环。它每次只做一拍内的有界工作：

- 补充必要的超时 trigger
- 从 pending trigger 队列中挑选优先级最高的事件
- 消费一个 trigger
- 推进一次运行态逻辑

因此，平台调度层负责“何时调”，`main_loop` 负责“调一次时做什么”。

### 6. 应用编排层

应用层主要负责把输入、查询和流程推进组织成清晰的用例入口：

- `process_formal_command`：把命令文本变成正式请求
- `process_wash_trigger`：把 trigger 变成业务动作
- `query_wash_session_status`：把当前运行态投影成可读状态

这一层不拥有外层事件循环，也不直接处理文件系统或 OS 等待，而是专注于“流程编排”。

### 7. 领域规则层

领域层负责真正的业务语义与状态机，包括：

- 洗车会话的开始、停止、异常结束和完成
- 工步段的进入、运行、退出和切换
- 等待条件与超时策略
- 段内条件控制与完成条件
- 故障、停止、超时的业务收口

这层决定“业务上现在应该怎样推进”，是系统的核心。

### 8. 适配器层

适配器层负责把外部世界翻译成系统可用的输入输出，包括：

- 文件程序仓储
- JSON 程序解析
- 文件事件日志
- stdin/stdout formal command 来源适配
- 仿真传感器驱动
- 仿真执行机构驱动

例如程序仓储通过 `program_repository_port` 向上提供“按程序 ID 加载和校验程序”的能力，而不会把文件路径和目录结构泄露到领域层。

## 当前主运行链路

下面是当前程序从启动到稳态运行的主链路。

### 启动阶段

1. `main` 初始化仿真传感器和执行机构端口。
2. `main` 准备调度配置，例如控制周期、退出模式和每拍最大 trigger 数。
3. `main` 组装 `controller_runtime_config_t`，把端口、配置目录、日志路径和标准输入输出传给 runtime。
4. `controller_runtime_create()` 获取正式 `system_context`，装配程序仓储和事件日志，再创建平台调度器。
5. `controller_runtime_run()` 启动调度器主循环。

### 稳态运行阶段

调度器在稳态下持续等待三类输入：

- 周期事件
- 命令输入事件
- 退出/唤醒事件

当事件到来时，调度器会决定是否推进一拍，并调用 `main_loop_run()`。

### 单拍推进阶段

`main_loop_run()` 每次推进时：

1. 检查当前等待条件是否已到期，必要时自动补一个 `TIMEOUT` trigger。
2. 从待处理 trigger 队列中选出优先级最高的一个。
3. 调用 `process_wash_trigger_execute()` 消费这个 trigger。
4. 再调用 `process_wash_runtime_tick()` 推进一次当前 wash session 的运行态。

这意味着 trigger 消费和运行态推进是在同一拍中连续完成的。

### 命令进入阶段

命令主链路是：

`stdin` -> `stdio_formal_command_adapter` -> `process_formal_command`

平台调度器只负责发现 stdin fd 就绪并触发适配器处理；适配器负责读取、缓冲、按行解析、调用正式命令用例并写回响应。

当前命令分为两类：

- 控制型命令：如 `start`、`stop`、`fault`，先转换成 trigger，再进入 pending queue
- 查询型命令：如 `status`，直接走状态查询，不进入 trigger 队列

命令不会直接改写领域状态，而是先标准化为正式请求，这样外部输入、超时、故障和停止都可以统一进入同一套运行时推进模型。

### 业务推进阶段

`process_wash_trigger` 负责把 trigger 映射成业务动作：

- `START`：加载程序快照，启动 session，装载第一个工步段
- `STOP`：中断当前执行并收口会话
- `FAULT`：记录或处理中断故障
- `TIMEOUT`：按超时策略推进异常流程

在会话运行过程中，`wash_execution_service` 负责：

- 推动工步段从 `entering` 进入 `running`
- 基于运行时快照判断位置、跟随、条件控制和段完成
- 触发退出动作并等待退出完成
- 在段完成后切到下一段，或结束整个流程

## 设计思想

### 1. 启动与运行分离

`main` 和 `controller_runtime` 负责启动与销毁，`controller_scheduler` 负责长期运行，`main_loop` 负责单拍推进。这样每层只承担一个明确职责。

### 2. 平台调度与业务语义分离

平台层拥有时间推进和事件等待，领域层拥有状态机和业务规则。这样系统不会把 Linux 细节扩散进业务核心。

### 3. 输入先标准化，再进入业务

命令文本不会直接驱动领域对象，而是先进入正式命令入口，再转换成 trigger 或查询请求。这让各种入口都能共享同一套业务推进机制。

### 4. 运行态集中承载

核心运行状态集中在 `system_context` 中，而不是分散在大量全局变量里。这使得生命周期、状态访问和资源释放更容易控制。

### 5. 外部资源通过端口接入

程序配置、日志、传感器和执行机构都通过端口接入，核心只依赖抽象能力，不直接依赖外部资源实现细节。

### 6. 单拍推进优先于隐式长逻辑

当前主控骨架强调有界单拍推进。每一拍只做有限工作，这有利于控制实时性、定位问题和约束复杂度。

## 当前关键入口文件

- 进程入口：`src/main/main.c`
- runtime 生命周期：`src/application/coordinators/controller_runtime.c`
- 运行时组合根：`src/application/coordinators/system_context.c`
- 平台调度器：`src/platform/linux/controller_scheduler_linux.c`
- stdin 命令来源适配：`src/platform/linux/stdio_formal_command_adapter.c`
- 单拍推进器：`src/application/coordinators/main_loop.c`
- 正式命令入口：`src/application/use_cases/process_formal_command.c`
- trigger 编排：`src/application/use_cases/process_wash_trigger.c`
- 状态查询：`src/application/use_cases/query_wash_session_status.c`
- 程序仓储：`src/adapters/outbound/file_program_repository.c`
- 程序解析：`src/adapters/config/json_program_parser.c`

## 给新开发者的建议

- 先把 `controller_runtime -> controller_scheduler -> main_loop -> process_wash_trigger` 这条主链读通，再看某个具体业务细节。
- 遇到“这个状态从哪来”的问题，优先看 `system_context` 是否承载了它。
- 遇到“为什么会在这个时刻推进”的问题，优先看 `controller_scheduler`。
- 遇到“为什么业务这样变化”的问题，优先看 `process_wash_trigger` 和领域服务，而不是先看平台层。
- 扩展外部资源接入时，优先走端口和适配器，不要把文件系统、驱动或 Linux 细节直接带进领域层。

## 总结

当前程序已经实现成一个正式的主控骨架：

- `main` 负责启动
- `controller_runtime` 负责生命周期
- `system_context` 负责承载运行态
- `controller_scheduler` 负责外层调度
- `main_loop` 负责单拍推进
- 应用层负责编排
- 领域层负责规则
- 适配器层负责外部世界接入

如果要快速理解代码，不要先盯某个具体设备动作或某个 JSON 字段，而是先建立这条主链的整体心智模型。理解了这条主链，再进入任一细节模块都会容易很多。
