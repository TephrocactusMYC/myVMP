#pragma once

#include <cstdint>
#include <limits>
#include <optional>

namespace myvmp::patch {

[[nodiscard]] inline std::optional<std::int64_t> signed_pc_distance(
    std::uint64_t from, std::uint64_t to) noexcept {
    if (to >= from) {
        const auto distance = to - from;
        if (distance > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
            return std::nullopt;
        return static_cast<std::int64_t>(distance);
    }
    const auto distance = from - to;
    constexpr auto minimum_magnitude = std::uint64_t{1} << 63u;
    if (distance > minimum_magnitude) return std::nullopt;
    if (distance == minimum_magnitude) return std::numeric_limits<std::int64_t>::min();
    return -static_cast<std::int64_t>(distance);
}

} // namespace myvmp::patch
