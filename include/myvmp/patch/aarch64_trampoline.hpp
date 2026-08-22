#pragma once

#include <cstdint>
#include <optional>
#include <array>

namespace myvmp::patch {

constexpr std::int64_t kAarch64BranchRange = 128ll * 1024ll * 1024ll;

std::optional<std::uint32_t> encode_aarch64_b(std::uint64_t from_va, std::uint64_t to_va);
std::optional<std::array<std::uint32_t, 3>> encode_aarch64_page_jump(
    std::uint64_t from_va, std::uint64_t to_va, std::uint8_t scratch = 16);
std::array<std::uint8_t, 16> encode_aarch64_absolute_jump(std::uint64_t to_va);

} // namespace myvmp::patch
