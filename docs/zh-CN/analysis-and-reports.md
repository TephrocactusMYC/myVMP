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

使用 `scripts/vm_attacker_metrics.py` 查看面向攻击者的结构指标：

```sh
python3 scripts/vm_attacker_metrics.py build/protected.elf --output build/attacker.json
python3 scripts/vm_attacker_metrics.py build/a.elf.myvmp.json build/b.elf.myvmp.json
python3 scripts/vm_attacker_metrics.py build/protected.elf --trace build/block-trace.json
```

报告包含可见 profile 指纹、manifest CFG/owner/bridge 结构、实现中已知的 operand 格式复用，
以及可选的实际 block trace 形状。trace 接受 block ID JSON 数组或 `{"block_ids": [...]}`。
sidecar 图指标反映的是披露的 metadata；没有 sidecar 的 ELF 会标记为 opaque。这些只是结构
代理指标，不代表语义恢复结果或实测反虚拟化成本。

攻击者侧的 payload/manifest 基线工具：

```sh
python3 scripts/reference_devirtualizer.py build/protected.elf \
  --payload-file-offset 0x123000 --output build/reference-analysis.json
```

文件偏移使用 `protect` 输出的 `payload_offset`。工具提取加密 VM 区域、计算哈希，
并将密文范围关联到 sidecar 中公开的 block/profile/CFG 信息。它**不会**声称恢复了
字节码语义：还需建模 native runtime 的 block whitening 和动态 operand-window 变换。
这是后续对抗式评估的基线 oracle，不是完整 devirtualizer。

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
`riscv64_multi_vm_quality.py`、coverage 和 liftability report。自动化发布 job 中优先调用
对应 Make target，因为它们会先构建依赖并检查 runtime blob。

发布证据建议保存原始/保护 hash、工具 hash/version、完整命令、受控 seed 记录、sidecar、
报告包、行为测试、目标编译器版本和成本数据。seed 是差异化输入，不是密码学信任根。

`FunctionVmIsa` API 另提供实验性的函数专属 ISA 与 operand grammar 描述，用于 specialization
研究；目前仅是元数据基础，native encoder/runtime 仍使用统一 bytecode operand contract。
