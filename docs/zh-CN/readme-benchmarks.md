# README 实测快照

[English](../readme-benchmarks.md) · [机器可读 JSON](../examples/quality-benchmarks.json)

这里保存 README 精简表格背后的证据。快照于 2026-09-04 在 Ubuntu 22.04 上使用仓库
fixture 和正常 `myvmp` CLI 生成。表中每个保护后 ELF 都在 QEMU 中实际执行，stdout 与
原文件逐字节一致。

## 测量对象

质量对比的输入是原生函数范围，保护后样本是 manifest 记录的各加密 VM 基本块载荷拼接。
它用于判断原生字节、对齐机器字和立即数是否泄漏到加密字节码中。这里有意不比较整个
ELF：文件头、库和未选择函数本就应保持不变。

ELF 体积成本单独报告，覆盖 ARM32、AArch64、RV64，单函数与三个函数批量保护，以及一个
135,275,392 字节、必须使用安全远跳页跳板的 AArch64 PIE。精确数据和环境版本见 JSON。

## 如何解读

位置/字面量暴露为零、熵提高且质量阈值通过，说明 codec 和加密流水线正常工作；它们不是
密码学证明，也不表示虚拟化不可分析。部署前请阅读[安全模型](security-model.md)。

可用 [`generate_analysis_report.py`](analysis-and-reports.md) 为自己的二进制生成报告。
结果随工具链、输入、所选符号、seed 和 VM 模式变化；公开数据是实测示例，不是对所有
ELF 的保证。
