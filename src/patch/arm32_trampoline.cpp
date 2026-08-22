#include "myvmp/patch/arm32_trampoline.hpp"
#include "myvmp/patch/pc_relative.hpp"

#include <limits>

namespace myvmp::patch {

std::optional<std::uint32_t> encode_arm32_b(std::uint64_t from_va, std::uint64_t to_va) {
    if (from_va > std::numeric_limits<std::uint64_t>::max() - 8u) return std::nullopt;
    const auto distance = signed_pc_distance(from_va + 8u, to_va);
    if (!distance) return std::nullopt;
    const auto delta = *distance;
    if ((from_va & 3u) != 0 || (to_va & 3u) != 0 || (delta & 3) != 0 ||
        delta < -kArm32BranchRange || delta >= kArm32BranchRange) return std::nullopt;
    return 0xea000000u | (static_cast<std::uint32_t>(delta / 4) & 0x00ffffffu);
}

} // namespace myvmp::patch
