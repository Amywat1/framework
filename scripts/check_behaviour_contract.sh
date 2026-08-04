#!/usr/bin/env bash
# check_behaviour_contract.sh — 框架行为契约结构校验
#
# 用法:
#   ./scripts/check_behaviour_contract.sh [framework_root]
#
# 退出码:
#   0  契约结构合规
#   1  发现问题
#   2  参数错误
#
# 校验项:
#   C1  编号唯一（无重复）
#   C2  编号前缀合法（须在第 6 节声明的前缀集合内）
#   C3  编号格式合法（PREFIX-NN，两位数字）
#   C4  类别合法（须在第 4 节表内）
#   C5  归属合法（须在第 2 节表内）
#   C6  级别合法（须在第 3 节表内，或 "—" 表示不判定）
#   C7  状态合法（须在第 5 节表内，或 "—"）
#   C8  归属与级别的组合合规：
#         框架要求 只能是 L2（被测对象是项目适配器，端口是唯一契约边界）
#         项目自负 不得有 L0~L4（框架不判），可为 "—" 或 L5
#   C9  统计表与实际条目数一致
#
# 为何需要本脚本：手工维护的统计与覆盖表必然失真。本仓 tests/reports/ 的
# 手写覆盖表已发生过（声称 event_bus 8 例、实际 13 例）。契约是测试方案的
# 事实源，它一旦失真，"谁负责验哪条" 就重新变成没有答案的问题。

set -uo pipefail

ROOT="${1:-$(cd "$(dirname "$0")/.." && pwd)}"
if [[ ! -d "$ROOT" ]]; then
    echo "错误: 目录不存在: $ROOT" >&2
    exit 2
fi
cd "$ROOT"

DOC="doc/行为契约.md"
if [[ ! -f "$DOC" ]]; then
    echo "错误: 契约文档不存在: $DOC" >&2
    exit 2
fi

python3 - "$DOC" <<'PYEOF'
import re
import sys
from collections import Counter

doc = sys.argv[1]
lines = open(doc, encoding='utf-8').read().split('\n')

PREFIXES = {
    'BOOT', 'EBUS', 'SCHED', 'CMD', 'MODE', 'ALRM', 'SAFE', 'ENGN',
    'PROJ', 'PORT', 'ERRM', 'ASSET', 'ARCH', 'LOG',
}
CATEGORIES = {
    '接口契约', '功能行为', '状态机', '并发', '时序', '容量', '故障',
    '结构', '物理特性',
}
OWNERS = {'框架保证', '框架要求', '项目自负'}
LEVELS = {'L0', 'L1', 'L2', 'L3', 'L4', 'L5'}
STATUSES = {'已验证', '待验证', '待实现'}
DASH = '—'

problems = []
entries = []

# 条目行形如: | ARCH-01 | 描述 | 结构 | 框架保证 | L0 | 已验证 |
row = re.compile(r'^\|\s*([A-Z]+-[^|\s]*)\s*\|(.+)$')

for idx, line in enumerate(lines, start=1):
    m = row.match(line)
    if not m:
        continue
    ident = m.group(1).strip()
    cells = [c.strip() for c in m.group(2).split('|')]
    # 去掉行尾空单元
    while cells and cells[-1] == '':
        cells.pop()
    if len(cells) != 5:
        problems.append(f'{doc}:{idx}: {ident} 字段数为 {len(cells)}，应为 5'
                        f'（描述/类别/归属/级别/状态）')
        continue
    desc, cat, owner, level, status = cells
    entries.append((idx, ident, cat, owner, level, status))

    if not re.fullmatch(r'[A-Z]+-\d{2}', ident):
        problems.append(f'{doc}:{idx}: 编号格式非法 [{ident}]，应为 PREFIX-NN')
    else:
        prefix = ident.split('-')[0]
        if prefix not in PREFIXES:
            problems.append(f'{doc}:{idx}: 编号前缀未声明 [{prefix}]')

    if not desc:
        problems.append(f'{doc}:{idx}: {ident} 描述为空')
    if cat not in CATEGORIES and cat != DASH:
        problems.append(f'{doc}:{idx}: {ident} 类别非法 [{cat}]')
    if owner not in OWNERS:
        problems.append(f'{doc}:{idx}: {ident} 归属非法 [{owner}]')
    if level not in LEVELS and level != DASH:
        problems.append(f'{doc}:{idx}: {ident} 级别非法 [{level}]')
    if status not in STATUSES and status != DASH:
        problems.append(f'{doc}:{idx}: {ident} 状态非法 [{status}]')

    # C8 归属与级别的组合
    if owner == '框架要求' and level != 'L2':
        problems.append(
            f'{doc}:{idx}: {ident} 归属为「框架要求」但级别为 {level}；'
            f'框架要求的被测对象是项目适配器，端口是唯一契约边界，只能是 L2。'
            f'若确实需要更高级别，应先修契约本身而非降低验证级别')
    if owner == '项目自负' and level in {'L0', 'L1', 'L2', 'L3', 'L4'}:
        problems.append(
            f'{doc}:{idx}: {ident} 归属为「项目自负」但级别为 {level}；'
            f'框架不判定项目自负条目，级别应为 — 或 L5')

# C1 编号唯一
dupes = [k for k, v in Counter(e[1] for e in entries).items() if v > 1]
for d in sorted(dupes):
    where = [str(e[0]) for e in entries if e[1] == d]
    problems.append(f'{doc}: 编号重复 [{d}]，出现在行 {", ".join(where)}')

# C9 统计表与实际一致
owner_actual = Counter(e[3] for e in entries)
level_actual = Counter(e[4] for e in entries)

def declared(section_names):
    """从统计表读取声明值：形如 | 框架保证 | 87 |"""
    out = {}
    for line in lines:
        m = re.match(r'^\|\s*(\S+?)\s*\|\s*(\d+)\s*\|$', line)
        if m and m.group(1) in section_names:
            out[m.group(1)] = int(m.group(2))
    return out

decl_owner = declared(OWNERS | {'合计'})
decl_level = declared({f'{l} 静态' for l in ['L0']} | set())
# 级别行形如 | L0 静态 | 20 |
for line in lines:
    m = re.match(r'^\|\s*(L\d)\s+\S+\s*\|\s*(\d+)\s*\|$', line)
    if m:
        decl_level[m.group(1)] = int(m.group(2))

for owner in sorted(OWNERS):
    if owner in decl_owner and decl_owner[owner] != owner_actual.get(owner, 0):
        problems.append(
            f'{doc}: 统计表「{owner}」声明 {decl_owner[owner]}，'
            f'实际 {owner_actual.get(owner, 0)}')
if '合计' in decl_owner and decl_owner['合计'] != len(entries):
    problems.append(
        f'{doc}: 统计表「合计」声明 {decl_owner["合计"]}，实际 {len(entries)}')

for level in sorted(LEVELS):
    if level in decl_level and decl_level[level] != level_actual.get(level, 0):
        problems.append(
            f'{doc}: 统计表「{level}」声明 {decl_level[level]}，'
            f'实际 {level_actual.get(level, 0)}')

print(f'契约条目: {len(entries)}')
print('归属分布: ' + ', '.join(f'{k}={owner_actual[k]}'
                              for k in sorted(owner_actual)))
print('级别分布: ' + ', '.join(f'{k}={level_actual[k]}'
                              for k in sorted(level_actual)))
print('状态分布: ' + ', '.join(f'{k}={v}' for k, v in
                              sorted(Counter(e[5] for e in entries).items())))
print()

if problems:
    for p in problems:
        print(f'[FAIL] {p}')
    print()
    print(f'结果: 失败 — {len(problems)} 项问题')
    sys.exit(1)

print('结果: 通过 — 契约结构合规，统计与条目一致')
PYEOF
