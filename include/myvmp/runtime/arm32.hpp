#pragma once

#include "myvmp/bytecode/bytecode.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace myvmp::vm { struct SiblingVmImage; }

namespace myvmp::runtime {

struct Arm32RuntimeContext { std::uint32_t function_va{}; };

std::vector<std::uint8_t> build_arm32_runtime_payload(
    const bytecode::Image& image, Arm32RuntimeContext context = {});
std::vector<std::uint8_t> build_arm32_runtime_payload(
    const vm::SiblingVmImage& image, Arm32RuntimeContext context = {});
std::size_t arm32_runtime_code_size() noexcept;
std::size_t arm32_encrypted_bytecode_offset(std::size_t block_count = 1) noexcept;

} // namespace myvmp::runtime
