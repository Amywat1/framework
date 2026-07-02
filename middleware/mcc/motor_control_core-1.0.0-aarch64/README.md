# motor_control_core SDK 移植说明

版本：1.0.0

---

## 一、SDK 目录结构

```
motor_control_core-1.0.0-<arch>/
├── include/                            # 公开头文件
│   └── motor/                          # 按模块组织的头文件
│       └── motor_version.h             # 版本宏（构建时自动生成）
├── lib/
│   └── libmotor_control_core.a         # 静态库
└── README.md                           # 本文档
```

---

## 二、系统要求

| 项目 | 最低要求 |
|------|---------|
| C 编译器 | 支持 C11 标准（GCC 4.9+ 或 arm-none-eabi-gcc 等交叉编译工具链） |
| 操作系统 | Linux / RTOS / 裸机（库本身不依赖操作系统） |

---

## 三、集成方式

### 方式一：CMake 工程

将 SDK 解压后，通过变量指定根路径：

```cmake
set(MOTOR_ROOT /path/to/motor_control_core-1.0.0-<arch>)

target_include_directories(your_app PRIVATE ${MOTOR_ROOT}/include)
target_link_libraries(your_app PRIVATE ${MOTOR_ROOT}/lib/libmotor_control_core.a)
```

完整示例：

```cmake
cmake_minimum_required(VERSION 3.10)
project(your_project C)

set(MOTOR_ROOT /opt/sdk/motor_control_core-1.0.0-aarch64)

add_executable(your_app main.c)
target_include_directories(your_app PRIVATE ${MOTOR_ROOT}/include)
target_link_libraries(your_app PRIVATE ${MOTOR_ROOT}/lib/libmotor_control_core.a)
```

**交叉编译时**额外指定工具链文件：

```bash
cmake -B build -DCMAKE_TOOLCHAIN_FILE=/path/to/arm-toolchain.cmake
cmake --build build
```

---

### 方式二：Makefile / IDE（Keil、IAR 等）

**步骤 1**：记录 SDK 解压路径，例如 `SDK_DIR = /opt/sdk/motor_control_core-1.0.0-aarch64`。

**步骤 2**：在编译命令中添加头文件路径：

```
-I$(SDK_DIR)/include
```

**步骤 3**：在链接命令中添加库文件：

```
-L$(SDK_DIR)/lib -lmotor_control_core
```

Keil / IAR 中在工程属性的"Include Paths"和"Libraries"中填写对应路径即可。

---

## 四、版本检查

SDK 提供版本宏，可在编译期检查版本兼容性：

```c
#include "motor/motor_version.h"

#if MOTOR_VERSION_MAJOR != 1
#error "需要 motor_control_core 1.x 版本"
#endif
```

---

## 五、注意事项

1. **静态库，无运行时依赖**：所有代码内嵌到目标可执行文件，无需在目标设备上额外部署文件。

2. **C11 标准**：目标工程编译器须支持 C11（`-std=c11`），否则可能出现编译错误。

3. **中断上下文**：SDK 中的 API 不得在中断服务程序（ISR）中直接调用，除非头文件注释中明确声明支持中断上下文。

4. **线程安全**：各模块线程安全性在对应头文件中注明，未注明的接口默认不支持并发调用。

5. **架构匹配**：包名中的 `<arch>` 须与目标平台一致（如 `aarch64`、`armv7l`）。架构不匹配时链接报错，需使用对应架构的 SDK 包或重新交叉编译。

---

## 六、常见问题

**Q：编译报 "No such file or directory: motor/motor_executor.h"？**

A：检查 `-I` 或 `target_include_directories` 指向的是 SDK 的 `include/` 目录（而非 `include/motor/`）。

**Q：链接报 "undefined reference to `motor_executor_xxx`"？**

A：确认链接命令中 `-lmotor_control_core` 位于目标文件之后，或 CMake 中 `target_link_libraries` 正确指向 `.a` 的绝对路径。

**Q：链接报架构不匹配？**

A：SDK 中的 `.a` 须与目标架构一致。请使用包名中架构字段对应的 SDK 包，或联系 SDK 维护方获取对应架构的预编译包。
