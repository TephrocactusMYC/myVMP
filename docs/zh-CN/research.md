# 调研思路与参考文献

[English](../research.md)

这是设计参考书目，不代表 myVMP 复制任何产品或源码。项目自行实现 C++ 架构、IR、
bytecode、runtime 和 ELF transformation。引用内容用于理解成熟设计、攻击流程、ABI 规则
和度量陷阱；参考代码的许可证必须单独审查，不能复制不兼容源码。

## 虚拟化与反虚拟化

- [Automatic Deobfuscation of Virtualization-protected Applications SoK](https://eprints.cs.univie.ac.at/7012/1/3465481.3465772.pdf)：
  fetch/decode/dispatch/handler 恢复和 trace normalization 的攻击模型，促成逐块/profile
  差异化以及对 multi-VM 能力边界的诚实描述。
- [Matryoshka slides](https://cs.anu.edu.au/cybersec/issisp2018/assets/slides/Davidson_PVM_Obfuscation_Slides.pdf)：
  nested virtualization 能增加分析成本但 overhead 明显，因此先实现 sibling VM，而不是
  任意递归 nesting。
- [Tigress Virtualize](https://tigress.wtf/virtualize.html)：重复指令、operand location、
  superoperator、dispatch diversity 和 reentrant interpreter，促成有界、可测试的多态。
- 质量阶段还阅读了近期材料：
  [arXiv:2601.10261](https://arxiv.org/abs/2601.10261)、
  [arXiv:2603.18355](https://arxiv.org/abs/2603.18355)、
  [SSRN 5147457](https://papers.ssrn.com/sol3/papers.cfm?abstract_id=5147457)、
  [IEEE Access](https://colab.ws/articles/10.1109%2Faccess.2021.3094517)。它们用于形成
  attacker-oriented 指标，而不是作为实现规范。

## 开源项目

- [VMPacker](https://github.com/LeoChen-CoreMind/VMPacker)：ARM64 ELF 函数虚拟化、
  opcode randomization、加密/重排 bytecode 和 injection workflow；强化了 binary-level
  frontend 与模块分离思路。
- [xVMP](https://github.com/GANGE666/xVMP)：LLVM-level virtualization 与 per-block
  思路；启发 block identity/local crypto domain，但 myVMP 保持 binary-level。
- [binprotect](https://github.com/noahware/binprotect)：x64 PE VM 与 handler layout 差异。
- [Kiteshield](https://github.com/GunshipPenguin/kiteshield)：x86-64 ELF packing/function
  encryption，用于区分 packing 与 semantic virtualization。
- [covirt](https://github.com/dmaivel/covirt)：VM/MBA/self-modifying 实验及 VM-call 失败，
  强化显式 bridge ABI 和专项测试的必要性。
- [Miasm](https://github.com/cea-sec/miasm)：成熟 lifting/analysis，可作为未来 differential
  oracle；项目仍保留轻量自有 IR。

对商业 protector（包括 VMProtect 保护样本）的社区分析用于研究可识别 dispatcher loop、
handler normalization、rolling decode、operand recovery 和 trace attack，从而在 build、
block、instruction 和 handler 多层改变表示。myVMP 不声称兼容或复现 VMProtect 内部格式。

## Binary rewriting 与标准

- [E9Patch](https://github.com/GJDuck/e9patch) 与[论文](https://doi.org/10.1145/3316415)：
  静态改写和 placement constraints。
- [LIEF](https://github.com/lief-project/LIEF) 与
  [ELF API](https://lief.re/doc/stable/doxygen/classLIEF_1_1ELF_1_1Binary.html)：ELF object
  model、segment/section/dynamic repair 对照。
- 相关 binary rewriting 研究：
  [DOI 10.1145/3546568](https://doi.org/10.1145/3546568)、
  [STTT](https://link.springer.com/article/10.1007/s10009-021-00644-w)。
- 权威语义和 ABI 来源：
  [Arm ELF ABI](https://github.com/ARM-software/abi-aa/blob/main/aaelf64/aaelf64.rst)、
  [RV64 ISA](https://docs.riscv.org/reference/isa/unpriv/rv64.html)、
  [RVC](https://docs.riscv.org/reference/isa/v20260120/unpriv/c-st-ext.html)、
  [RISC-V psABI](https://riscv-non-isa.github.io/riscv-elf-psabi-doc/)、
  [x86-64 psABI](https://gitlab.com/x86-psABIs/x86-64-ABI)、
  [Intel SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)。
- [Csmith](https://github.com/csmith-project/csmith) 启发可选随机 compiler-output 压力层；
  可复现的 checked-in deterministic fixtures 仍是主要回归。

## Android 原生平台调研

Android 远景评估以官方 NDK ABI、JNI、native build、16 KiB page、packaging/signing 文档和
AOSP Bionic linker notes 为依据。结论是现有 AArch64 ELF 能力是很好的起点，但正式支持
必须建立签名前 NDK `.so` pipeline 和 Android 专项兼容门禁。完整分析与来源见
[Android 原生保护远景](android-vision.md)。

## 最终采用的思路

函数级 binary virtualization、自有公共 IR、每构建/每出现 opcode 差异、统一 rolling
operand codec、CFG-aware block/key/layout、受限 superoperator、显式 sibling owner/bridge、
事务 ELF 发布、manifest 与行为/差异/暴露/成本联合测量。

明确不采用：把 entropy 当安全证明、静默放行未知指令、没有状态/异常 ABI 的任意递归 VM、
默认全程序虚拟化，以及复制专有格式或许可证不兼容实现。
