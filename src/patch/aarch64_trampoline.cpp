#include "myvmp/patch/aarch64_trampoline.hpp"
#include "myvmp/patch/pc_relative.hpp"

namespace myvmp::patch {

std::optional<std::uint32_t> encode_aarch64_b(std::uint64_t from_va, std::uint64_t to_va) {
    const auto distance = signed_pc_distance(from_va, to_va);
    if (!distance || (*distance % 4) != 0) {
        return std::nullopt;
    }
    const auto delta = *distance;
    if (delta < -kAarch64BranchRange || delta >= kAarch64BranchRange) {
        return std::nullopt;
    }

    const auto imm26 = static_cast<std::uint32_t>((delta / 4) & 0x03ffffff);
    return 0x14000000u | imm26;
}

std::optional<std::array<std::uint32_t, 3>> encode_aarch64_page_jump(
    std::uint64_t from_va, std::uint64_t to_va, std::uint8_t scratch) {
    if (scratch > 30) return std::nullopt;
    const auto page_distance = signed_pc_distance(from_va & ~std::uint64_t{0xfff},
                                                   to_va & ~std::uint64_t{0xfff});
    if (!page_distance) return std::nullopt;
    const auto pages = *page_distance / 4096;
    if (pages < -(std::int64_t{1} << 20) || pages >= (std::int64_t{1} << 20))
        return std::nullopt;
    const auto immediate = static_cast<std::uint32_t>(pages) & 0x1fffffu;
    const auto adrp = 0x90000000u | ((immediate & 3u) << 29u) |
                      (((immediate >> 2u) & 0x7ffffu) << 5u) | scratch;
    const auto add = 0x91000000u | (static_cast<std::uint32_t>(to_va & 0xfffu) << 10u) |
                     (static_cast<std::uint32_t>(scratch) << 5u) | scratch;
    const auto branch = 0xd61f0000u | (static_cast<std::uint32_t>(scratch) << 5u);
    return std::array{adrp, add, branch};
}

std::array<std::uint8_t, 16> encode_aarch64_absolute_jump(std::uint64_t to_va) {
    // ldr x16, #8; br x16; .quad target
    const std::uint32_t ldr = 0x58000050u;
    const std::uint32_t branch = 0xd61f0200u;
    std::array<std::uint8_t, 16> bytes{};
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes[shift / 8] = static_cast<std::uint8_t>(ldr >> shift);
        bytes[4 + shift / 8] = static_cast<std::uint8_t>(branch >> shift);
    }
    for (unsigned shift = 0; shift < 64; shift += 8) bytes[8 + shift / 8] = static_cast<std::uint8_t>(to_va >> shift);
    return bytes;
}

} // namespace myvmp::patch
