FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential clang python3 binutils ca-certificates git \
        gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu \
        gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu \
        gcc-arm-linux-gnueabihf binutils-arm-linux-gnueabihf \
        qemu-user \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
COPY . .
RUN make -j"$(nproc)" && make test && make docs-check

CMD ["bash"]
