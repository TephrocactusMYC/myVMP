#pragma once

#include "myvmp/bytecode/bytecode.hpp"

#include <cstdint>
#include <vector>

namespace myvmp::vm { struct SiblingVmImage; }

namespace myvmp::runtime {

struct Aarch64RuntimeContext {
    std::uint64_t function_va{};
    std::uint64_t payload_va{};
};

// Builds a position-independent AArch64 interpreter followed by encrypted physical
// bytecode. Addresses in native call gates are rebased through the protected function.
std::vector<std::uint8_t> build_aarch64_runtime_payload(
    const bytecode::Image& image, Aarch64RuntimeContext context = {});
std::vector<std::uint8_t> build_aarch64_runtime_payload(
    const vm::SiblingVmImage& image, Aarch64RuntimeContext context = {});
std::size_t aarch64_runtime_code_size() noexcept;
std::size_t aarch64_runtime_bytecode_offset(const bytecode::Image& image) noexcept;
std::size_t aarch64_runtime_bytecode_offset(const vm::SiblingVmImage& image) noexcept;

} // namespace myvmp::runtime
