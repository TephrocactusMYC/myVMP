#pragma once

#include <cstdint>
#include <array>
#include <optional>

namespace myvmp::patch {

constexpr std::int64_t kRiscv64JalRange = 1ll * 1024ll * 1024ll;
std::optional<std::uint32_t> encode_riscv64_jal(std::uint64_t from_va, std::uint64_t to_va,
                                                std::uint8_t rd = 0);
std::optional<std::array<std::uint32_t, 2>> encode_riscv64_far_jump(
    std::uint64_t from_va, std::uint64_t to_va, std::uint8_t scratch_and_link = 5);

} // namespace myvmp::patch
