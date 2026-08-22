#pragma once

#include "myvmp/bytecode/bytecode.hpp"

#include <array>
#include <cstdint>

namespace myvmp::runtime {

[[nodiscard]] std::array<std::uint8_t, 256> physical_to_logical_table(
    const bytecode::VmProfile& profile);

// index_xor places sizes in a transformed dispatcher domain while preserving
// the profile's physical-to-logical semantics.
[[nodiscard]] std::array<std::uint8_t, 256> physical_operand_size_table(
    const bytecode::VmProfile& profile, std::uint8_t index_xor = 0);

} // namespace myvmp::runtime
