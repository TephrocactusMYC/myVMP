#pragma once

#include "myvmp/bytecode/bytecode.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace myvmp::vm { struct SiblingVmImage; }

namespace myvmp::runtime {

struct Riscv64RuntimeContext { std::uint64_t function_va{}; };

std::vector<std::uint8_t> build_riscv64_runtime_payload(
    const bytecode::Image& image, Riscv64RuntimeContext context = {});
std::vector<std::uint8_t> build_riscv64_runtime_payload(
    const vm::SiblingVmImage& image, Riscv64RuntimeContext context = {});
std::size_t riscv64_runtime_code_size() noexcept;
std::size_t riscv64_encrypted_bytecode_offset(std::size_t block_count) noexcept;

} // namespace myvmp::runtime
