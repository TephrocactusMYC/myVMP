#pragma once

#include "myvmp/bytecode/bytecode.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace myvmp::runtime {

std::vector<std::uint8_t> build_arm32_runtime_payload(const bytecode::Image& image);
std::size_t arm32_runtime_code_size() noexcept;

} // namespace myvmp::runtime
