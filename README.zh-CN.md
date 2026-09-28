# myVMP — 动态、多架构 ELF 虚拟化保护

> **只保护真正重要的代码：把选定的 ARM32、AArch64 和 RV64 函数直接转换为每次构建
> 都不同的加密虚拟机。**

![Language](https://img.shields.io/badge/C%2B%2B-20-00599C)
![License](https://img.shields.io/badge/license-GPL--3.0--only-blue)
![Targets](https://img.shields.io/badge/ELF-AArch64%20%7C%20ARM32%20A32%20%7C%20RV64-success)
![VM](https://img.shields.io/badge/VM-dynamic%20sibling%20profiles-purple)

[English README](README.md) · [英文文档](docs/index.md) · [中文文档](docs/zh-CN/index.md) · 版本 0.1.0

myVMP 是一个可落地的函数级保护工具，支持 **ARM32/A32、AArch64 和 RV64 ELF**。
你可以指定授权校验、核心算法、协议逻辑或密钥派生等高价值符号；工具会用嵌入式 VM
runtime 和加密字节码替换其原生控制流。它不需要 LLVM fork、编译器插件或改造源码工程，
也永远不会原地修改输入 ELF。

## 项目优势

- **二进制级工作流：**直接保护已链接 ELF 的具名函数，无需编译器插件。
- **每次构建得到不同 VM：**seed 同时改变 opcode 映射、operand codec、基本块归属与布局、
  加密、立即数降级、dispatcher 常量、handler 变体和原生函数体陷阱填充，而不只是换一个
  字节码 XOR 密钥。
- **真实 sibling multi-VM：**基本块由两个独立 profile 动态持有，并通过经过验证的
  bridge 跨 VM 执行，而不是静态放置第二个 dispatcher。
- **三条完整保护路径、一套统一设计：**三种架构共享语义 IR、字节码/profile 接口、manifest
  与质量门禁，同时保留经审计的 ISA 专属 lifter、runtime 和距离安全跳板。
- **AArch64 SIMD 虚拟化：**经过测试的固定 128 位 Advanced SIMD 子集覆盖整数与浮点
  lane、Q load/store、比较/选择、移位和 EXT，并贯通动态 profile、sibling VM 与原生调用桥。
- **为选择性保护设计：**可保护一个函数，也可用 `protect-many` 指定一组函数，把启动与体积
  成本集中在安全关键代码上。
- **失败关闭：**不支持的指令、危险重定位、畸形 ELF 和不安全 patch 计划会在发布前拒绝。
- **可度量：**提供可复现 manifest、字节暴露指标、多 seed 差异门禁、QEMU 行为对比和
  大型 ELF 压力测试。

### 保护具体改变了什么

下面是仓库 `candidate_branch_ops` fixture 的真实 AArch64 反汇编。76 字节原生循环被替换
为入口跳转，剩余函数体使用随 seed 变化的陷阱销毁；函数语义从附加载荷中的加密 VM 块执行。

```text
保护前                                      保护后
400780: mov  x5, x0                        400780: b    0x47d720
400784: mov  x1, #0                        400784: brk  #0x175a
40078c: lsl  x7, x5, #1                    400788: brk  #0xfef1
400790: add  x4, x5, x1                    40078c: brk  #0xeb9c
   ... 条件循环 ...                            ... 随 seed 变化的陷阱 ...
4007c4: b.ne 0x400790                      4007c8: brk  #0x3381
4007c8: ret
```

### 实测混淆质量

以下 fixture 均以 sibling multi-VM 模式实际保护并在 QEMU 中运行。质量对比的是**原生函数
字节**与**实际加密 VM 基本块字节**，而不是拿大部分内容本应不变的整个 ELF 对比。

| 目标 | 原生 → 加密 VM 字节 | 保护前 → 后熵 | 同位置相同字节 | 32/64 位原生机器字暴露 | QEMU |
|---|---:|---:|---:|---:|---:|
| ARM32/A32 | 68 → 264 | 4.128 → 7.242 | 0 | 0 / 0 | 行为一致 |
| AArch64 | 76 → 197 | 4.682 → 7.081 | 0 | 0 / 0 | 行为一致 |
| RV64 | 32 → 141 | 4.601 → 6.627 | 0 | 0 / 0 | 行为一致 |

三个样本的原生字面量暴露同样为零，且全部通过质量阈值。这些指标是回归证据，并非“保护
不可逆”的承诺。

### 选择性保护与真实 ELF 尺度

| 场景 | 原 ELF | 保护后 ELF | 增长 | 结果 |
|---|---:|---:|---:|---:|
| ARM32，单函数 | 12,596 B | 12,596 B | 0 B | 行为一致 |
| AArch64，单函数 sibling VM | 641,280 B | 706,816 B | 65,536 B | 行为一致 |
| RV64，单函数 sibling VM | 8,736 B | 16,593 B | 7,857 B | 行为一致 |
| RV64，三个函数 `protect-many` | 8,736 B | 25,111 B | 16,375 B | 行为一致 |
| AArch64，129 MiB PIE 中单函数 | 135,275,392 B | 135,340,928 B | 65,536 B | 行为一致 |

最后一项实际覆盖超出 AArch64 直接分支范围的距离安全远跳页。详见
[测试方法与解读](docs/zh-CN/readme-benchmarks.md)及
[机器可读报告](docs/examples/quality-benchmarks.json)。

myVMP 是研究项目。建议保护经过充分测试的关键函数，并结合自身威胁模型评估安全与性能成本。

## 构建

基本环境需要 Linux、GNU Make、C++20 编译器和 Python 3：

```sh
make -j$(nproc)
make test
```

主要产物是 `build/myvmp`、`build/myvmp_demo` 和 `build/myvmp_tests`。构建发布包：

```sh
make release
make package
```

交叉编译器和 QEMU 只在重建目标 runtime 或执行目标架构行为测试时需要。完整说明见
[编译与平台](docs/zh-CN/build-and-platforms.md)。

Windows 推荐 WSL2；Windows/macOS 均可使用官方 Ubuntu 容器工作流：

```sh
docker build -t myvmp:0.1.0 .
docker run --rm myvmp:0.1.0 build/myvmp --version
```

## 保护单个函数

工具读取保留非零长度符号的已链接 ELF。建议为目标函数添加 `noinline` 和 `used`：

```c
__attribute__((noinline, used))
uint64_t protected_calculation(uint64_t x) {
    return (x * 17u) ^ 0x55aa55aaULL;
}
```

```sh
arm-linux-gnueabihf-gcc -O2 -g -fno-inline -fno-stack-protector app.c -o app.arm32
aarch64-linux-gnu-gcc -O2 -g -fno-inline -fno-stack-protector app.c -o app.arm64
riscv64-linux-gnu-gcc  -O2 -g -fno-inline -fno-stack-protector app.c -o app.rv64

# ARM32/A32
./build/myvmp inspect-elf app.arm32 protected_calculation
./build/myvmp protect app.arm32 protected_calculation app.protected.arm32 1001
arm32_sysroot=$(dirname "$(dirname "$(arm-linux-gnueabihf-gcc -print-file-name=libc.so.6)")")
qemu-arm -L "$arm32_sysroot" ./app.protected.arm32

# AArch64
./build/myvmp inspect-elf app.arm64 protected_calculation
./build/myvmp protect-dry-run app.arm64 protected_calculation 1001
./build/myvmp protect app.arm64 protected_calculation app.protected.arm64 1001
aarch64_sysroot=$(dirname "$(dirname "$(aarch64-linux-gnu-gcc -print-file-name=libc.so.6)")")
qemu-aarch64 -L "$aarch64_sysroot" ./app.protected.arm64

# RV64
./build/myvmp inspect-elf app.rv64 protected_calculation
./build/myvmp protect-dry-run app.rv64 protected_calculation 1001
./build/myvmp protect app.rv64 protected_calculation app.protected.rv64 1001
riscv64_sysroot=$(dirname "$(dirname "$(riscv64-linux-gnu-gcc -print-file-name=libc.so.6)")")
qemu-riscv64 -L "$riscv64_sysroot" ./app.protected.rv64
```

输出旁会生成 `.myvmp.json` manifest。发布前必须比较完整程序行为：

```sh
aarch64_sysroot=$(dirname "$(dirname "$(aarch64-linux-gnu-gcc -print-file-name=libc.so.6)")")
qemu-aarch64 -L "$aarch64_sysroot" ./app.arm64 > before.txt
qemu-aarch64 -L "$aarch64_sysroot" ./app.protected.arm64 > after.txt
cmp before.txt after.txt
```

## sibling multi-VM

至少有两个 CFG 基本块的函数可以启用两个独立 seed 的 VM：

```sh
./build/myvmp protect app.arm64 protected_calculation app.multi.arm64 \
  1001 --multi-vm 2002
```

manifest 会记录两个 profile、每个块的 owner 以及所有 bridge。零 seed、相同 seed、
单基本块和非法 owner 都会失败关闭。

## 批量保护选定函数

```sh
./build/myvmp protect-many app.rv64 app.protected.rv64 123456 \
  verify_license derive_session_key validate_transaction
```

`protect-many` 从 master seed 派生每个函数的 seed，并且只有所有函数都成功后才发布结果。
它会生成 `.myvmp.json` 和 `.myvmp.batch.json`，并拒绝覆盖已有输出。

## 分析和质量报告

```sh
./build/myvmp lift-elf-symbol app.arm64 protected_calculation
./build/myvmp quality-report --json original.bin protected.bin
python3 scripts/generate_analysis_report.py \
  --tool build/myvmp --input app.arm64 --protected app.protected.arm64 \
  --symbol protected_calculation --output analysis
```

报告包含 ELF 规划、profile 元数据、哈希和字节质量指标。`quality-report --json` 会声明
稳定的 `myvmp-quality-v1` schema，CI 可在解析字段前校验契约。这些指标用于回归，不能
单独证明抵抗反虚拟化的能力。

## 文档

- [中文文档导航](docs/zh-CN/index.md)
- [架构和保护流水线](docs/zh-CN/architecture.md)
- [混淆和 multi-VM](docs/zh-CN/obfuscation.md)
- [完整使用指南](docs/zh-CN/usage.md)
- [分析报告](docs/zh-CN/analysis-and-reports.md)
- [公开跨架构实测](docs/zh-CN/readme-benchmarks.md)
- [平台与环境](docs/zh-CN/build-and-platforms.md)
- [安全模型](docs/zh-CN/security-model.md)
- [测试](docs/zh-CN/testing.md)
- [调研与参考文献](docs/zh-CN/research.md)
- [后续路线](docs/zh-CN/roadmap.md)
- [贡献指南](CONTRIBUTING.zh-CN.md)和[安全政策](SECURITY.zh-CN.md)

## 许可证

myVMP 使用 [GNU GPL v3.0 only](LICENSE)。贡献内容采用相同许可证，并按
[贡献指南](CONTRIBUTING.zh-CN.md)使用 DCO sign-off。

## 引用

如果 myVMP 对你的研究或工程工作有所帮助，请使用以下 BibTeX 信息引用本项目：

```bibtex
@software{mu_2026_myvmp,
  author  = {Mu, Yuchen},
  title   = {myVMP: Dynamic Multi-Architecture ELF Virtualization},
  year    = {2026},
  version = {0.1.0},
  url     = {https://github.com/TephrocactusMYC/myVMP},
  license = {GPL-3.0-only}
}
```
