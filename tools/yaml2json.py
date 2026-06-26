#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
构建期工具：把洗车方案 YAML 转换为等价 JSON。

YAML 是人维护的源文件；本脚本在构建机（开发/CI）上把它转成 JSON，
供设备运行期用 cJSON 解析。脚本只运行在构建机，不进入固件。

用法：
    python3 tools/yaml2json.py <input.yaml> <output.json>

依赖：pyyaml（pip install pyyaml）。仅修改 YAML 的人需要本依赖；
生成的 JSON 一并提交仓库后，常规构建/固件编译无需 python。
"""

import sys
import json


def main():
    if len(sys.argv) != 3:
        sys.stderr.write("用法: yaml2json.py <input.yaml> <output.json>\n")
        return 2

    try:
        import yaml
    except ImportError:
        sys.stderr.write("缺少依赖 pyyaml，请执行: pip install pyyaml\n")
        return 2

    import re

    # 采用 YAML 1.2 布尔语义：仅 true/false 视为布尔，
    # 避免 PyYAML(YAML 1.1) 把裸键 on/off/yes/no 误判为布尔
    # （否则标记里的 `on:` 键会被转成布尔 True → JSON "true"）。
    class CleanLoader(yaml.SafeLoader):
        pass

    for _ch in list(CleanLoader.yaml_implicit_resolvers):
        CleanLoader.yaml_implicit_resolvers[_ch] = [
            (tag, rgx)
            for (tag, rgx) in CleanLoader.yaml_implicit_resolvers[_ch]
            if tag != "tag:yaml.org,2002:bool"
        ]
    CleanLoader.add_implicit_resolver(
        "tag:yaml.org,2002:bool",
        re.compile(r"^(?:true|True|TRUE|false|False|FALSE)$"),
        list("tTfF"),
    )

    in_path, out_path = sys.argv[1], sys.argv[2]

    try:
        with open(in_path, "r", encoding="utf-8") as f:
            data = yaml.load(f, Loader=CleanLoader)
    except Exception as exc:  # noqa: BLE001
        sys.stderr.write("YAML 解析失败 %s: %s\n" % (in_path, exc))
        return 1

    try:
        with open(out_path, "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, indent=2)
            f.write("\n")
    except OSError as exc:
        sys.stderr.write("写出失败 %s: %s\n" % (out_path, exc))
        return 1

    sys.stdout.write("已生成 %s\n" % out_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
