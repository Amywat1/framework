---
name: openspec-adversarial
description: Generate adversarial tests for an OpenSpec change implementation. Use when the user wants to stress-test, attack, or break their implementation to find defects.
license: MIT
compatibility: Requires openspec CLI.
metadata:
  author: openspec
  version: "1.0"
  generatedBy: "1.3.1"
---

对指定 change 的实现自动执行对抗式验证，生成攻击性测试用例并输出可编译的 C 测试代码。

**Input**: 可选指定 change 名称。省略则从上下文推断或提示选择。

**Steps**

1. **选择 change**

   如果提供了名称，直接使用。否则：
   - 从对话上下文推断
   - 若只有一个活跃 change，自动选中
   - 若有歧义，运行 `openspec list --json` 并用 **AskUserQuestion** 让用户选择

   公告：「对抗验证目标: <name>」

2. **收集被测系统信息**

   a. **行为规格**：读取 `openspec/changes/<name>/` 下的 spec 相关 artifact（如 `specs.md`）。
      若不存在，尝试 `doc/spec/<name>.md`。

   b. **公开接口**：从 change 的 design.md 或 tasks.md 中提取涉及的头文件路径，
      逐一读取。若无法自动确定，用 **AskUserQuestion** 询问用户提供头文件路径。

   c. **实现源码**（可选增强）：读取对应的 `.c` 实现文件，用于更精准的攻击设计。

3. **生成对抗式测试用例**

   以「破坏者」角色思考，针对收集到的规格和接口，从以下 7 个攻击方向生成测试用例：

   1. **状态机边界**：非法状态转换、同一事件重复触发、转换中途被中断
   2. **时序竞态**：中断在关键区间到达、任务切换在未保护窗口发生
   3. **资源枯竭**：队列满、缓冲区满、计数器溢出、连续重试耗尽
   4. **重入与嵌套**：回调中再次调用触发函数、事件处理中发布新事件
   5. **故障组合**：多个故障同时发生、故障恢复过程中二次故障
   6. **边界值**：参数为 0、为最大值、为最大值+1、为负数（如果类型允许）
   7. **初始化依赖**：未初始化即调用、初始化顺序颠倒、重复初始化

   **要求**：
   - 至少 10 个测试用例
   - 覆盖上述 7 个方向中的至少 5 个
   - 每个用例独立可运行，不依赖执行顺序

4. **输出为可编译的 C 测试文件**

   将测试用例写入 `tests/<layer>/test_<feature>_adversarial.c`，格式为 Unity 测试框架：

   ```c
   #include "unity.h"
   #include "<被测头文件>"

   void setUp(void) { /* 每个测试前的初始化 */ }
   void tearDown(void) { /* 每个测试后的清理 */ }

   /* TC-01: <测试名> */
   /* 攻击目标: <试图违反哪条场景/不变量> */
   void test_<攻击描述>(void) {
       // 攻击手法的具体实现
       ...
       TEST_ASSERT_...(...);
   }

   // ... 更多测试用例

   int main(void) {
       UNITY_BEGIN();
       RUN_TEST(test_...);
       // ...
       return UNITY_END();
   }
   ```

   - 测试文件路径根据被测模块所属层级决定（domain/application/runtime/adapters）
   - 若 `tests/` 下已有对应目录结构，遵循之；否则创建

5. **注册到 CMake**（如果 CMakeLists.txt 中有测试注册模式）

   检查 `tests/CMakeLists.txt` 或相应子目录的 cmake 文件，按既有模式添加新测试目标。

6. **运行验证**

   ```bash
   ctest --output-on-failure -R adversarial
   ```

   - 测试通过 → 实现对这些攻击具有鲁棒性
   - 测试失败 → 列出失败项，指出实现中对应的薄弱点，建议修复方向

7. **输出摘要**

   ```
   ## 对抗验证完成

   **目标 change**: <name>
   **被测接口**: <头文件列表>
   **生成用例**: N 个（覆盖 M/7 个攻击方向）
   **结果**: X 通过 / Y 失败

   ### 失败项（需修复）
   - TC-03: <描述> — 暴露的缺陷: <说明>
   - ...

   ### 通过项
   - TC-01: <描述>
   - ...
   ```

**Guardrails**
- **所有输出必须使用中文**（测试代码中的注释也用中文）
- 测试代码必须可编译——include 路径正确，类型匹配，无缺失依赖
- 不修改被测源码，只新增测试文件
- 每个测试用例明确标注攻击目标和攻击手法
- 若某攻击方向在当前模块不适用，在摘要中说明原因并跳过
- 若 build 环境不可用，仍输出测试文件并告知用户手动编译验证
- 测试失败不是错误——它是发现缺陷的正常结果，如实报告即可
