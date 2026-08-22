#include "myvmp/runtime/profile_tables.hpp"

#include <stdexcept>

namespace myvmp::runtime {

std::array<std::uint8_t, 256> physical_to_logical_table(
    const bytecode::VmProfile& profile) {
    std::array<std::uint8_t, 256> table{};
    table.fill(0xff);
    for (unsigned physical = 0; physical < table.size(); ++physical) {
        try {
            table[physical] = static_cast<std::uint8_t>(
                profile.logical(static_cast<std::uint8_t>(physical)));
        } catch (const std::runtime_error&) {
        }
    }
    return table;
}

std::array<std::uint8_t, 256> physical_operand_size_table(
    const bytecode::VmProfile& profile, std::uint8_t index_xor) {
    std::array<std::uint8_t, 256> table{};
    for (unsigned physical = 0; physical < table.size(); ++physical) {
        try {
            const auto logical = profile.logical(static_cast<std::uint8_t>(physical));
            table[static_cast<std::uint8_t>(physical ^ index_xor)] =
                static_cast<std::uint8_t>(bytecode::operand_size(logical));
        } catch (const std::runtime_error&) {
        }
    }
    return table;
}

} // namespace myvmp::runtime
