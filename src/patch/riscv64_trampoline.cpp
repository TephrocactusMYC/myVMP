#include "myvmp/patch/riscv64_trampoline.hpp"
#include "myvmp/patch/pc_relative.hpp"

#include <limits>

namespace myvmp::patch {

std::optional<std::uint32_t> encode_riscv64_jal(std::uint64_t from_va, std::uint64_t to_va,
                                                std::uint8_t rd) {
    const auto distance = signed_pc_distance(from_va, to_va);
    if (!distance) return std::nullopt;
    const auto delta = *distance;
    if (rd > 31 || (delta & 1) != 0 || delta < -kRiscv64JalRange || delta >= kRiscv64JalRange)
        return std::nullopt;
    const auto imm = static_cast<std::uint32_t>(delta) & 0x1fffffu;
    return ((imm >> 20u) & 1u) << 31u |
           ((imm >> 1u) & 0x3ffu) << 21u |
           ((imm >> 11u) & 1u) << 20u |
           ((imm >> 12u) & 0xffu) << 12u |
           static_cast<std::uint32_t>(rd) << 7u | 0x6fu;
}

std::optional<std::array<std::uint32_t, 2>> encode_riscv64_far_jump(
    std::uint64_t from_va, std::uint64_t to_va, std::uint8_t scratch_and_link) {
    const auto distance = signed_pc_distance(from_va, to_va);
    if (!distance) return std::nullopt;
    const auto delta = *distance;
    if (scratch_and_link > 31 || (delta & 1) != 0 ||
        delta < std::numeric_limits<std::int32_t>::min() ||
        delta > std::numeric_limits<std::int32_t>::max()) return std::nullopt;
    const auto rounded = delta + 0x800;
    auto high = rounded / 4096;
    if (rounded < 0 && (rounded % 4096) != 0) --high;
    const auto low = delta - (high << 12);
    const auto auipc = (static_cast<std::uint32_t>(high) & 0xfffffu) << 12u |
                       static_cast<std::uint32_t>(scratch_and_link) << 7u | 0x17u;
    const auto jalr = (static_cast<std::uint32_t>(low) & 0xfffu) << 20u |
                      static_cast<std::uint32_t>(scratch_and_link) << 15u |
                      static_cast<std::uint32_t>(scratch_and_link) << 7u | 0x67u;
    return std::array{auipc, jalr};
}

} // namespace myvmp::patch
