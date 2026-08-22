#pragma once

#include <cstdint>
#include <optional>

namespace myvmp::patch {

constexpr std::int64_t kArm32BranchRange = 32ll * 1024ll * 1024ll;
std::optional<std::uint32_t> encode_arm32_b(std::uint64_t from_va, std::uint64_t to_va);

} // namespace myvmp::patch
