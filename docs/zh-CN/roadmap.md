# 后续路线

[English](../roadmap.md)

AArch64/RV64 scalar 函数保护、动态 operand codec、CFG-aware bytecode、sibling multi-VM、
真实 ELF 行为回归和大位置压力已经实现。后续优先级如下。

## 发布准备

- 按已确定的 GPL-3.0-only 和签名 tag/checksum 策略发布版本化 release；
- 增加可复现构建证据和多工具链 CI；
- 稳定 machine-readable CLI schema 与诊断分类；
- 增加 packaged-tool smoke tests 和多 compiler version compatibility fixtures。
- 只有实际需求足以承担 ELF/target-tool compatibility layer 的长期维护时才考虑原生
  Windows/macOS host port；当前支持基线是 WSL2/container。

## 正确性与平台范围

- 在明确语义/ABI 测试后扩展可选 AArch64/RV64 SIMD/vector；
- 扩展少见 atomic、relocation 和 compiler-specific scalar forms；
- 加强 shared object、TLS、exception/unwind 和超大型应用 qualification；
- 决定 ARM/A32 保持实验还是扩展为长期维护范围。

## 混淆质量

- 增加结构独立的 dispatch/handler family 并测量 signature reduction；
- 只有 selective nesting 明显优于 sibling 且成本可接受时才推进；
- 增加 dynamic trace shape、recovered CFG 和 handler clustering 指标；
- 改进跨 release/compiler/seed 的自动报告对比。

## 新架构

- 基于公共 IR/profile/codec/manifest/ELF64 writer 设计 x86-64 frontend/runtime；
- ISA 语义和 detour 保持 target-specific，不污染公共 bytecode；
- ELF 的架构边界稳定以后再考虑其他可执行格式。

路线项目不是支持承诺。只有 host semantics、target runtime behavior、畸形输入处理与发布文档
一起通过后，功能才算真正支持。
