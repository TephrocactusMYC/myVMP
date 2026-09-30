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
sidecar 图指标反映的是披露的 metadata；这个依赖 sidecar 的工具会把缺少 sidecar 的 ELF 标记为
opaque，但这是该工具的局限，不代表 runtime 结构不可分析。这些只是结构代理指标，不代表语义
恢复结果或实测反虚拟化成本。

攻击者侧的 runtime bytecode 基线工具：

```sh
python3 scripts/reference_devirtualizer.py build/protected.elf \
  --payload-file-offset 0x123000 --output build/reference-analysis.json
# 忽略相邻 sidecar 时，需提供架构和 runtime 代码大小：
python3 scripts/reference_devirtualizer.py build/protected.elf \
  --no-sidecar --architecture aarch64 --runtime-code-size 10024 \
  --payload-file-offset 0x123000 --output build/runtime-only-analysis.json
```

文件偏移使用 `protect` 输出的 `payload_offset`；runtime 代码大小必须与被分析的构建一致
（`10024` 仅为示例，并非格式常量）。工具直接读取 AArch64、ARM32 和 RV64 原生 runtime
头部，撤销 bytecode whitening 和 operand-window 编码，即使没有 sidecar，也能恢复物理 VM
block、owner、logical VM opcode、operand 字节与显式静态跳转目标，并推导部分 fallthrough 边。
AArch64 单 VM 模式的 opcode 映射来自已修补的原生 dispatcher 以及本仓库源码中的比较顺序；
其余模式使用 runtime translation table。有 sidecar 时，会对比 block、owner 和 CFG 边。
v3 报告把 VM 指令/CFG 恢复与源码级语义 IR 恢复分开计数；后者仍为零，动态 VM return 边和
handler 语义也未推导。暴露的结构不能当作语义保护有效的证据。安装三个交叉工具链后，
可用 `make reference-devirtualizer-test` 验证 AArch64 单/双 VM 和其他真实保护产物，以及
截断输入、非法 size/target/owner 拒绝行为。解析程序应先检查 `schema`：
`payload_file_offset` 指 runtime 起点，`encrypted_bytecode_file_offset` 指加密 VM 区域
（v1 曾用前一个字段名指加密区域）。

当前集成 fixture 在 sibling 模式、seed `12345`/`67890` 下得到以下精确 sidecar 对比：

| 保护后的 fixture | Runtime block | 边界/owner 匹配 | VM 指令 | CFG 边匹配 |
|---|---:|---:|---:|---:|
| AArch64 branch operations | 5 | 5/5 | 27 | 6/6 |
| ARM32 VM probe | 5 | 5/5 | 37 | 6/6 |
| RV64 integer operations | 3 | 3/3 | 29 | 2/2 |

忽略各自 sidecar 时，也能恢复相同的指令和边。上述数值只对应所列 fixture/seed，不表示
整个架构的覆盖率。

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
