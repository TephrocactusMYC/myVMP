# CactiVM：面向 ELF 函数的多架构选择性二进制虚拟化

[English](../paper.md) · 技术论文稿，2026 年 10 月 2 日

**作者：**Yuchen Mu

> 这是基于实际实现的研究论文稿，不是已经通过同行评审的论文，也不主张密码学安全。
> 下文数据来自可复现的项目 fixture，不是与商业保护器的比较或大型应用统计。

## 摘要

虚拟化保护把选中的原生指令转换为由内嵌解释器执行的字节码。真正的工程难点不只是修改
opcode 编号，还包括在真实二进制中保留调用约定、控制流、内存副作用和部署约束。本文介绍
CactiVM：面向 AArch64、ARM32/A32 与 RV64 的函数级 ELF 转换器。系统将指定函数提升为公共
中间表示（IR），构建控制流图（CFG），根据 seed 生成 VM profile，嵌入目标架构原生 runtime，
再修补原函数入口。Sibling 模式把物理块分配给两个 profile，并在 CFG 边切换解码域。已实现
的机制包括有界立即数重写、重复物理 opcode、特定 superoperator、operand-window 编码和
架构相关原生 handler。正确性通过 host 语义测试及 QEMU 下原始/保护后产物的差分运行检验。
同时，项目维护的参考攻击者工具可以无需 sidecar 撤销当前 runtime 格式，并恢复 VM 指令和
静态 CFG 边。这表明现有编码提供差异化，但不是保密边界。本文的贡献是可审计的跨 ISA 工程
设计，以及同时报告保护变换和攻击者可见结构的评价方法。

## 1. 引言

应用通常只需保护少数关键函数。默认虚拟化整个程序会带来体积、运行成本和兼容性问题；
而链接后二进制保护还必须处理真实符号范围、relocation、ELF segment 布局、可执行空间和
入口远跳距离。将单架构原型扩展到三个指令集时，这些问题尤其明显。

CactiVM 的转换单位是 little-endian ELF 中具名且有文件映射的函数。输入不会被就地修改；
输出仍是普通可执行文件或共享对象，不依赖 LLVM pass 或定制 loader。解码器不能安全建模
的原生指令会使整个函数被拒绝，而不会被作为不透明 VM 操作跳过。项目保护的是**选定函数**，
不是 ELF 中的全部代码；当分析者在运行时观察到明文状态后，项目也不承诺保密性。

本文描述四项工程贡献：三个目标架构共用的 IR/bytecode 契约；在 build、基本块和指令层
变化的 seed profile 与 sibling owner；失败关闭的 ELF 发布路径及 host/QEMU 测试；以及量化
当前格式泄露内容的无 sidecar 参考攻击者工具。更细节的模块说明见
[架构](architecture.md)、[混淆设计](obfuscation.md)、[安全模型](security-model.md)和
[分析指南](analysis-and-reports.md)。

## 2. 威胁模型与主张边界

防护方控制输入 ELF、函数选择、seed 和构建流程。攻击者取得保护后 ELF，可以反汇编、
运行和插桩，并可阅读本项目公开源码及格式。`.myvmp.json` sidecar 是诊断产物；若其中的
profile 和 CFG metadata 敏感，不应随发行件一起发布。客户端 runtime 无法永久隐藏其解码
材料。Seed 是可复现和差异化输入，不是秘密密码学密钥。

目标是增加从保护后的机器码映射回原函数语义的分析成本。本文**不**主张语义不可区分、
不可破解、反调试、防篡改或攻击者工作量下界。参考工具已能恢复有用的 VM 结构，这项结果
是评价的一部分，而非需要回避的例外。

## 3. 系统架构

```text
  已链接 ELF + 指定 symbol
              |
     ELF/范围/relocation 检查
              |
       ISA decoder/lifter  <--- AArch64 / A32 / RV64
              |
          公共 typed IR
              |
       CFG + liveness 分析
              |
       profile + block owner
              |
     bytecode + 原生 VM runtime
              |
       ELF 放置 + 入口 detour
              |
       保护后 ELF + 可选 sidecar
```

ELF 前端检查 class、machine、section/segment 边界、symbol 和可执行文件映射。ISA 前端
以真实虚拟地址完整解码选中的函数，把寄存器、flags、内存、控制流、call 及明确支持的浮点/
向量操作提升为 typed IR。任何未知指令或不安全 relocation 都导致拒绝。公共 IR 随后检查
寄存器宽度、目标和退出路径。

Bytecode 层识别 leader、建立 block，并在指令扩展和物理放置后重新映射控制目标。非入口块
可以改变物理顺序，同时以显式 VM branch 与 call continuation 保留逻辑 CFG。runtime 是
可审计的目标架构汇编，构建脚本将其生成版本化字节 blob；host VM 则提供独立语义测试路径。
目标特定 patcher 决定近/远 detour，并更新 ELF 布局。AArch64/RV64 共用参数化 ELF64 布局
引擎，ARM32 使用 ELF32/A32 路径。多函数保护采用阶段化发布，某个函数失败不会留下部分
保护的最终输出。

### 3.1 原生执行边界

入口 detour 把控制权交给 runtime，后者保存目标 ABI 状态、建立 VM frame、定位物理块、
选择 owner profile 并分派 handler。调用未虚拟化函数时，native call gate 同步 ABI 所需的
寄存器和栈状态，返回后继续 VM。入口修补后的原函数余部会写入 seed 派生的 trap fill。
未选中的代码仍按原生方式执行。

### 3.2 架构支持边界

项目提供 AArch64、ARM32 A32 状态、RV64 的标量基线。AArch64 还支持经过测试的固定
128-bit Advanced SIMD 子集：向量 load/store、部分整数/位运算、compare、shift、permute
及 FP32/FP64 运算，并覆盖 native-call ABI。这不代表全部 NEON、SVE/SME、RVV、Thumb 或
ARM32 VFP/NEON。未知或不安全的形式失败关闭。支持边界以 decoder/runtime 测试为准，
不能从架构名称推断。

## 4. 保护表示

### 4.1 Logical ISA 与 profile

IR 操作降低为共用的 logical VM ISA。`VmProfile` 从非零 seed 派生物理 opcode 映射；
常见操作还可在每次出现时选择重复的物理编码。Profile 也影响 dispatcher 比较常量、
原生 handler variant、立即数重建模板、block 物理布局和填充字节。因此输出不只是源指令
顺序加一个 opcode 置换。不过，logical ISA 和 operand-size 契约目前仍在函数间共用。
函数专属语义指令集、独立合成的 handler family 尚属研究方向，不是已部署能力。

### 4.2 Operand 与 payload 编码

每条 VM 指令的 operand 宽度由中心契约声明。Runtime 在 handler 入口结合 key 材料、当前
物理偏移、opcode 域、owner 和 operand-byte 索引解码当前窗口，在下一次 dispatch 前重新
编码。独立的 payload 变换，以及 AArch64 的滚动 block 变换，使字节随位置和块变化。立即
数可以用有界 XOR 或模加 shares 重建。这些层减少原始 literal 和指令字节的直接暴露，但
都是**可逆编码**：runtime 必须携带 key/table，公开的参考工具能将其撤销。不能把它描述为
抵抗二进制持有者的加密保密机制。

### 4.3 CFG 与 sibling profile

编码器为逻辑块提供稳定 identity，但允许不同的物理排列。Sibling 模式把 block 分配给两个
seeded profile。当前 owner 选择结合 CFG、loop、dominator、liveness 信息与 seed 变化，并
固定入口块。每条静态跨 owner 边都具有经过验证的 `VmBridge`，记录源/目标 profile、目标/
continuation、live-state policy 等执行 metadata。原生 runtime 从目标块 owner 选择 translation
和 handler 域。**当前原生 sibling runtime 共享完整执行 frame**；host interpreter 另外测试
了 profile-local 的寄存器、flags、向量、SP 可逆表示和 live-state 传递，但目标汇编尚未执行
这些 state representation transform。

```text
  逻辑 CFG：       B0 ----> B1 ----> B3
                    \       /
                     -> B2

  物理顺序：       [B0:A] [B3:A] [B1:B] [B2:B]
                    |        ^       |
                    +--- bridge -----+

  A/B 是 profile owner，不是两个操作系统进程。
```

受限 superoperator 只组合合格的同 block compare/test 与 branch 模式，不跨 branch target、
call 或内存顺序边界。这让行为等价有可测试边界，也避免隐藏未经建模的副作用。

## 5. 正确性与失败关闭

只有 native decoding、IR 验证、bytecode target、owner、detour reach 和 ELF 放置全部满足
契约，转换才会接受。未知 opcode 与畸形 runtime metadata 进入显式失败路径。Host VM 快速
执行逻辑 bytecode；真实受保护 ELF 再于 QEMU 下与原件差分运行。测试层包括 CLI 错误契约、
runtime blob 再生成、交叉工具链构建、multi-seed、native call/ABI、ARM32/RV64 行为、
AArch64 SIMD 差分和大 ELF 远跳压力。这些测试只证明所检验案例，不是所有程序或编译器输出
上的形式化证明。

## 6. 攻击者侧分析

高熵或低字节重合不意味着反虚拟化工具无法恢复结构。因此项目维护
[`reference_devirtualizer.py`](https://github.com/TephrocactusMYC/CactiVM/blob/master/scripts/reference_devirtualizer.py) 作为当前格式的参考
攻击者工具。给定保护后 ELF、架构、runtime 起点和代码大小，它会解析原生 runtime table
与 block 范围，撤销 payload/operand 变换，识别 VM 指令边界和 logical opcode，并重建
显式分支目标及可静态推导的 fallthrough 边。它不依赖 sidecar；提供 sidecar 时会核对边界、
owner 和 CFG 边。回归覆盖 AArch64 单/双 VM、ARM32、RV64，并包括非法 size、target 和
owner 输入。

AArch64 单 VM 的物理到逻辑 opcode 映射还使用已修补的原生 dispatcher 比较指令和本仓库
源码中的比较顺序。AArch64 sibling 与 ARM32/RV64 使用 runtime 暴露的 translation table。
工具**没有**恢复 handler 效果、动态 VM return 边或源码级语义 IR。Logical VM opcode 数量
不能当作 IR 恢复数量。下一项可检验基准是恢复 handler 语义，并比较不同 profile、编译器
下归一化的 IR/CFG。

## 7. 评价与解释

已提交的[基准快照](readme-benchmarks.md)在 Ubuntu 22.04、GCC 交叉工具链和 QEMU 6.2
下采集。列出的每个保护后 ELF 都与原件逐字节比较 stdout。字节质量样本比较的是选中原生
函数范围和拼接的加密 VM block 范围，**不是**整个 ELF。

| Fixture | 原生字节 | VM payload 字节 | Payload entropy (bit/byte) | QEMU stdout 相同 |
|---|---:|---:|---:|---|
| ARM32/A32 VM probe | 68 | 264 | 7.241544 | 是 |
| AArch64 branch operations | 76 | 197 | 7.081294 | 是 |
| RV64 loop | 32 | 141 | 6.627036 | 是 |

这三个 fixture 的同位置重合率和所测 literal/word 暴露率为零。样本很小，不构成抗语义
恢复的证明。文件体积成本也不同：快照中 AArch64 单函数增加 65,536 字节；RV64 单函数
增加 7,857 字节，三个函数增加 16,375 字节；ARM32 样本复用已有空间，文件大小未增长。
135,275,392 字节的 AArch64 PIE 远跳样本也增加 65,536 字节，QEMU stdout 保持一致。
该快照没有提供 runtime slowdown，因此不能据此作通用性能结论。

参考攻击者工具提供另一面的观察：其 sibling 集成 fixture 使用 seed `12345`/`67890`，
在 AArch64 恢复 27 条 VM 指令和 sidecar 中 6/6 条静态 CFG 边；ARM32 恢复 37 条和 6/6
条边；RV64 恢复 29 条和 2/2 条边。这些精确匹配说明，至少在这些案例中，差异化与可逆
编码未隐藏该结构。Fixture 过小，不能得出跨样本攻击成本或统计安全结论。

## 8. 局限、效度与后续研究

当前原生系统是两个 sibling profile，而非 N-way 异构 VM。Profile 改变编码与 handler，
但共享 logical ISA 和原生 VM frame。Host-only state-representation 实验及有界序列挖掘器
不能被误称为已经生成的原生 state bridge 或合成 operator。AArch64 SIMD 是明确的子集，
不推导 ARM32/RV64 向量扩展能力。共享对象、TLS、unwind、特殊 relocation、新编译器形式
和大型真实应用仍需更广泛资格验证；指定 symbol 必须可安全修补。

内部效度受 fixture、编译 flags 和 QEMU 行为检查限制；外部效度受缺乏大型独立应用 corpus、
人工逆向研究和反虚拟化时间/成本测量限制。参考工具知道开源格式；它的成功并不量化一个
完全不了解源码的分析者需要多久。反之，它未恢复语义 IR，也不证明这一步困难。后续实验
应与体积/运行开销一起报告源码级 IR/CFG 恢复、trace-shape 对齐、handler clustering、
工具运行时间和人工成本。

## 9. 相关工作与伦理

[自动反虚拟化 SoK](https://eprints.cs.univie.ac.at/7012/1/3465481.3465772.pdf) 讨论 VM
结构与攻击者归一化；[Tigress 文档](https://tigress.wtf/virtualize.html)介绍 duplicate
instruction、superoperator 等设计选项。这些是概念参考，不表示 CactiVM 复刻其实现。
二进制重写与平台正确性参考 [E9Patch](https://doi.org/10.1145/3316415)、
[Arm ELF ABI](https://github.com/ARM-software/abi-aa/blob/main/aaelf64/aaelf64.rst)和
[RISC-V ELF psABI](https://riscv-non-isa.github.io/riscv-elf-psabi-doc/)。完整文献见
[研究记录](research.md)。

软件保护具有双重用途，同样的变换也可能掩盖恶意逻辑。项目公开格式、局限和攻击者侧
工具，让安全主张可证伪。使用者应只修改获授权的软件，并保留法律与运营所需的审计能力。

## 10. 可复现性

仓库采用 GPL-3.0-only。按[构建说明](build-and-platforms.md)准备环境，运行
`make release-check`、`make reference-devirtualizer-test`，再运行[测试文档](testing.md)
对应架构/QEMU target。基准环境、输入选择和数值位于已提交的
[JSON 证据](../examples/quality-benchmarks.json)。`protect` sidecar 保存 seed、profile
与 block metadata 以便受控复现，但应作为诊断产物管理。完整实验应归档输入/输出 hash、
工具链和 QEMU 版本、symbol、flags、seed、sidecar、观测行为和机器可读分析报告。

## 11. 结论

CactiVM 展示了覆盖三个 ELF 架构的模块化、选择性二进制虚拟化流水线，并具有明确正确性
门禁和真实 runtime 测试。多层 profile 与 sibling owner 改变了保护后的执行表面，但现有
runtime 格式仍允许恢复 VM 指令和静态 CFG。比“强混淆”更准确的结论是：该系统可以针对
经过资格验证的选定函数落地，而分析抵抗力仍需语义攻击者基线、更大 corpus 和成本测量。
