# io_handle 单元测试报告

| 属性 | 值 |
|------|-----|
| 测试目标 | `test_io_handle` |
| 源文件 | `tests/common/test_io_handle.c` |
| 被测代码 | `common/io_handle.h`（头文件内联函数） |
| 最后执行 | 2026-07-09 |
| 结果 | **6/6 通过** |

## 编码规则（被测契约）

16 位句柄编码：

- bit15：类型位（0=DI，1=DO）
- bit14~8：子板号（7 位，最大 0x7F）
- bit7~0：引脚号（8 位，最大 0xFF）

## 用例明细

| 用例 | 目的 | 验证点 | 结果 |
|------|------|--------|------|
| `test_di_encode_decode` | DI 编解码 | board=3, pin=17 解析正确；kind=DI | 通过 |
| `test_do_encode_decode` | DO 编解码 | board=5, pin=8 解析正确；kind=DO | 通过 |
| `test_di_do_kind_distinct` | DI/DO 类型区分 | 同 board/pin 的 DI 与 DO 句柄不同 | 通过 |
| `test_make_helpers_match_macros` | 宏与函数等价 | `IO_DI`/`io_di_make`、`IO_DO`/`io_do_make` 结果一致 | 通过 |
| `test_board_pin_mask_boundary` | 最大值边界 | board=0x7F, pin=0xFF 正确编码 | 通过 |
| `test_board_overflow_is_masked` | 板号溢出掩码 | board=0xFF 被截断为 0x7F | 通过 |

## 未覆盖项

| 场景 | 说明 |
|------|------|
| `IO_HANDLE_NULL` | 零句柄语义未测 |
| pin 溢出掩码 | 仅测了 board 溢出，pin>0xFF 截断未单独测 |
| 与 HAL 集成 | 句柄在 `hal_io_sim` 中的有效性由 `test_hal_io_sim` 间接覆盖 |

## 待补充

> 新增用例时在此追加。

| 用例 | 目的 | 状态 |
|------|------|------|
| — | — | — |
