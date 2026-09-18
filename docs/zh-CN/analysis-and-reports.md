# 分析与报告

[English](../analysis-and-reports.md)

完整审查应组合四类证据：ELF/symbol/relocation/detour 资格；lifted IR 与行为语义；profile、
block owner、bridge 与 payload metadata；字节暴露、差异度、size 和 runtime cost。单一
指标不能证明安全，高 entropy 也可能只表示压缩或加密。

## 内置入口

```sh
./build/myvmp inspect-elf INPUT SYMBOL
./build/myvmp --log-dir DIR protect-dry-run INPUT SYMBOL SEED
./build/myvmp lift-elf-symbol INPUT SYMBOL
./build/myvmp profile-manifest SEED
./build/myvmp quality-report --json BEFORE AFTER
```

保护 sidecar 是 JSON，包含 architecture、VM mode、codec、profile、物理块、owner、
successor 和 bridge。批量保护还会产生 aggregate batch manifest。

`quality-report --json` 输出具有稳定的顶层标识
`"schema": "myvmp-quality-v1"`。解析器应先检查该值，再解释指标；未来 schema 版本可能
增加字段。任一指定阈值失败时命令返回状态码 1，因此可以直接作为 CI 质量门禁。

## 自动报告包

```sh
python3 scripts/generate_analysis_report.py \
  --tool build/myvmp --input app.arm64 \
  --protected app.protected.arm64 --symbol protected_calculation \
  --output build/reports/app
```

输出 `report.md`、`report.json`、`inspect.txt`、可选 `lift.txt`、`quality.json` 和复制的
`protected.manifest.json`。使用 `--before-bytes`/`--after-bytes` 比较提取的函数或 payload
区间；未提供时会跳过昂贵且常被未保护代码污染的质量扫描。只有确实需要时才显式增加
`--whole-elf-quality`。

| 指标 | 方向 | 含义 |
|---|---:|---|
| same-position ratio | 越低越好 | 同一偏移未改变字节 |
| longest equal run | 越低越好 | 最大固定字节岛 |
| word/literal exposure | 越低越好 | 源窗口仍出现在输出中 |
| pairwise ciphertext distance | 越高越好 | seed 间 payload 差异 |
| unique payload/manifest | 等于样本数 | 发现固定 profile 或碰撞 |
| owner balance | 远离 0 | 两个 sibling VM 都做实际工作 |
| bridge count | 非零 | 存在真实跨 profile CFG |
| runtime/size ratio | 有界 | 运行和体积成本 |

项目脚本包括 `bytecode_seed_sweep.py`、`multi_vm_quality.py`、
`riscv64_multi_vm_quality.py`、coverage 和 liftability report。CI 中优先调用对应 Make
target，因为它们会先构建依赖并检查 runtime blob。

发布证据建议保存原始/保护 hash、工具 hash/version、完整命令、受控 seed 记录、sidecar、
报告包、行为测试、目标编译器版本和成本数据。seed 是差异化输入，不是密码学信任根。
