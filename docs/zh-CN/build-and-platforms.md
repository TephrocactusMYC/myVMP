# 支持平台与编译环境

[English](../build-and-platforms.md)

## 支持矩阵

| 组件 | 状态 | 说明 |
|---|---|---|
| Linux x86-64 host | 主要 | 本机编译 C++ 工具，目标测试使用交叉工具链 |
| Linux AArch64/RV64 host | 预期可用 | 需要本机编译器，目前发布回归主要为 cross-host |
| Windows host | 通过 WSL2/container 支持 | 不支持原生 Win32 build |
| macOS host | 通过 container/Linux VM 支持 | 不支持原生 Mach-O host build |
| AArch64 ELF64 小端 | 主要目标 | executable、PIE、shared object，广泛 scalar 范围及文档所列固定 128 位 Advanced SIMD 子集 |
| RV64 ELF64 小端 | 主要目标 | executable、PIE、shared object、scalar/RVC 工作流 |
| ARM ELF32 A32 | 支持的标量目标 | executable、动态 codec 与 sibling multi-VM；无 Thumb/VFP/NEON |
| x86-64 target | 未实现 | parser 能识别，但 protector 明确拒绝 |
| PE/Mach-O target | 不支持 | 保护输出仍是 Linux ELF |

不支持的操作会被拒绝；表格不意味着任意编译器输出都可保护。

## 基本宿主环境

需要 GNU Make、C++20 编译器、Python 3、ELF headers 和 binutils：

```sh
sudo apt-get update
sudo apt-get install build-essential clang python3 binutils
make -j$(nproc)
make test
```

使用 `./build/myvmp --version` 检查嵌入版本。

可以覆盖工具和构建目录：

```sh
make CXX=clang++ STRIP=llvm-strip -j$(nproc)
make BUILD_DIR=build-debug \
  CXXFLAGS='-std=c++20 -O0 -g -Wall -Wextra -Wpedantic -Werror'
```

## 交叉工具链与 QEMU

Ubuntu/Debian 完整环境：

```sh
sudo apt-get install \
  gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu \
  gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu \
  gcc-arm-linux-gnueabihf binutils-arm-linux-gnueabihf \
  qemu-user
```

普通 host build 使用仓库内 runtime blob，不要求 cross assembler。修改 `runtime/*.S` 后：

```sh
make generate-aarch64-runtime check-aarch64-runtime
make generate-riscv64-runtime check-riscv64-runtime
make generate-arm32-runtime check-arm32-runtime
```

不要手工编辑生成的 `.inc` 字节数组。

## Windows/WSL2

Windows 推荐安装 Ubuntu 24.04 WSL2，并把仓库 clone 到 WSL 文件系统（例如
`~/src/myVMP`，避免 `/mnt/c` 的 I/O 开销）：

```powershell
wsl --install -d Ubuntu-24.04
```

进入 Ubuntu 后按上文安装 GNU cross toolchain/QEMU 并运行 `make`。原生 Visual Studio/
MinGW 不是发布支持路径，因为 `<elf.h>`、shell automation、GNU target tools 和 QEMU-user
都需要单独移植。

## macOS/Windows 容器

Docker Desktop、Podman 等 Linux container runtime 可以提供一致 Ubuntu 24.04 环境：

```sh
docker build -t myvmp:0.1.0 .
docker run --rm myvmp:0.1.0 build/myvmp --version
docker run --rm myvmp:0.1.0 make release-check
docker run --rm -v "$PWD/samples:/work" myvmp:0.1.0 \
  /workspace/build/myvmp protect /work/app.arm64 target /work/app.protected.arm64 1001
```

PowerShell volume 参数可使用 `${PWD}/samples:/work`。Apple Silicon 默认运行 arm64 Ubuntu
container；checked-in target runtime blob 使 host tool build 不依赖目标架构。

当前不仓促提供原生 macOS port：它仍只输出 Linux ELF，却需要长期维护 ELF type
compatibility layer 和 Linux/GNU test tooling 替代。容器提供更小、更可复现的支持面。

## Sanitizer 构建

```sh
make BUILD_DIR=build-sanitize \
  CXXFLAGS='-std=c++20 -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer' \
  LDFLAGS='-fsanitize=address,undefined' -j$(nproc)
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 build-sanitize/myvmp_tests
```

managed/ptrace runner 可能无法执行 leak detection；关闭它不会关闭 address/UB 检查。

## 发布包

```sh
make release
make package
```

release 使用独立 `build/release-build` object tree 的 `-O3 -DNDEBUG` 配置，避免复用已有
debug/development object；strip 工具并打包中英文文档；压缩包位于
`build/dist/myvmp-<version>-linux-<host-arch>-tool.tar.gz`，内部根目录为
`myvmp-<version>/`。环境支持静态 C++ 链接时可以增加
`STATIC=1`。

版本来自 `VERSION`，CLI、archive 名称和根目录必须一致。release 发布和签名由仓库托管
平台的公开流程维护。

发布前运行 `make reproducible-check package-smoke-test`。它使用固定
`SOURCE_DATE_EPOCH` 比较两个隔离 release 构建，规范化 tar 的 owner、顺序和时间戳，
再解包检查二进制与必要文档。
