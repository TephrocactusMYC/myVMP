# 后续路线

[English](../roadmap.md)

AArch64、ARM32/A32、RV64 scalar 函数保护、动态 operand codec、CFG-aware bytecode、sibling multi-VM、
真实 ELF 行为回归和大位置压力已经实现。后续优先级如下。

## 发布准备

- 按已确定的 GPL-3.0-only 和签名 tag/checksum 策略发布版本化 release；
- 增加可复现构建证据和多工具链 CI；
- 稳定 machine-readable CLI schema 与诊断分类；
- 增加 packaged-tool smoke tests 和多 compiler version compatibility fixtures。
- 只有实际需求足以承担 ELF/target-tool compatibility layer 的长期维护时才考虑原生
  Windows/macOS host port；当前支持基线是 WSL2/container。

## 正确性与平台范围

- 只在 decoder、typed-lane、native runtime 和 QEMU ABI 覆盖齐全时扩展已实现的 AArch64
  固定宽度 Advanced SIMD 子集；SVE/SME 与 RVV 保持为独立的未来设计；
- 扩展少见 atomic、relocation 和 compiler-specific scalar forms；
- 加强 shared object、TLS、exception/unwind 和超大型应用 qualification；
- 扩大 ARM32 compiler corpus qualification，同时把 Thumb/VFP/NEON 保持为显式后续工作。

## 混淆质量

- 增加结构独立的 dispatch/handler family 并测量 signature reduction；
- 只有 selective nesting 明显优于 sibling 且成本可接受时才推进；
- 增加 dynamic trace shape、recovered CFG 和 handler clustering 指标；
- 改进跨 release/compiler/seed 的自动报告对比。

## 新架构

- 基于公共 IR/profile/codec/manifest/ELF64 writer 设计 x86-64 frontend/runtime；
- ISA 语义和 detour 保持 target-specific，不污染公共 bytecode；
- ELF 的架构边界稳定以后再考虑其他可执行格式。

## Android 原生目标远景

- 把 Android 定义为 NDK `.so` 保护与 build integration，不宣称 Java/Kotlin/Dex 或
  whole-APK 混淆；
- 若未来批准开发，先做 arm64-v8a compatibility/liftability corpus，再开始写 Android
  artifact；
- 声明支持前必须通过 Android ET_DYN relocation/RELRO、4/16 KiB、JNI、Gradle、APK/AAB
  和签名顺序门禁；
- 详见仅调研的 [Android 原生保护远景](android-vision.md)。

路线项目不是支持承诺。只有 host semantics、target runtime behavior、畸形输入处理与发布文档
一起通过后，功能才算真正支持。
