#include "myvmp/vm/state_encoding.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace myvmp::vm {
namespace {

class SplitMix64 {
public:
    explicit SplitMix64(std::uint64_t state) : state_(state) {}

    std::uint64_t next() {
        auto value = (state_ += 0x9E3779B97F4A7C15ull);
        value = (value ^ (value >> 30u)) * 0xBF58476D1CE4E5B9ull;
        value = (value ^ (value >> 27u)) * 0x94D049BB133111EBull;
        return value ^ (value >> 31u);
    }

private:
    std::uint64_t state_;
};

template <std::size_t Size>
void shuffle_slots(std::array<std::uint8_t, Size>& slots, SplitMix64& random) {
    for (std::size_t i = 0; i < Size; ++i) slots[i] = static_cast<std::uint8_t>(i);
    for (std::size_t i = Size; i > 1; --i) {
        const auto j = static_cast<std::size_t>(random.next() % i);
        std::swap(slots[i - 1], slots[j]);
    }
}

template <std::size_t Size>
void validate_permutation(const std::array<std::uint8_t, Size>& slots) {
    std::array<bool, Size> seen{};
    for (const auto slot : slots) {
        if (slot >= Size || seen[slot]) throw std::invalid_argument("invalid state slot permutation");
        seen[slot] = true;
    }
}

std::array<bool, 4> flags_to_array(const Flags& flags) {
    return {flags.z, flags.n, flags.c, flags.v};
}

Flags array_to_flags(const std::array<bool, 4>& flags) {
    return Flags{flags[0], flags[1], flags[2], flags[3]};
}

} // namespace

void StateRepresentation::validate() const {
    validate_permutation(gpr_slot);
    validate_permutation(fp_slot);
    validate_permutation(vector_slot);
    validate_permutation(vector_byte_slot);
    validate_permutation(flag_slot);
    if ((flag_polarity & 0xF0u) != 0) throw std::invalid_argument("invalid flag polarity mask");
}

StateRepresentation make_state_representation(std::uint32_t profile_id, std::uint64_t seed) {
    StateRepresentation result;
    result.profile_id = profile_id;
    SplitMix64 random(seed ^ (static_cast<std::uint64_t>(profile_id) * 0xD6E8FEB86659FD93ull));
    shuffle_slots(result.gpr_slot, random);
    shuffle_slots(result.fp_slot, random);
    shuffle_slots(result.vector_slot, random);
    shuffle_slots(result.vector_byte_slot, random);
    shuffle_slots(result.flag_slot, random);
    for (auto& key : result.gpr_xor) key = random.next();
    for (auto& key : result.fp_xor) key = random.next();
    for (auto& vector : result.vector_xor) {
        for (auto& byte : vector) byte = static_cast<std::uint8_t>(random.next());
    }
    result.sp_xor = random.next();
    result.flag_polarity = static_cast<std::uint8_t>(random.next() & 0x0Fu);
    result.validate();
    return result;
}

EncodedArchitecturalState encode_state(const ArchitecturalState& state,
                                       const StateRepresentation& representation) {
    representation.validate();
    EncodedArchitecturalState encoded;
    for (std::size_t logical = 0; logical < state.gpr.size(); ++logical) {
        encoded.slots.gpr[representation.gpr_slot[logical]] = state.gpr[logical] ^ representation.gpr_xor[logical];
    }
    for (std::size_t logical = 0; logical < state.fp.size(); ++logical) {
        encoded.slots.fp[representation.fp_slot[logical]] = state.fp[logical] ^ representation.fp_xor[logical];
    }
    for (std::size_t reg = 0; reg < state.vectors.size(); ++reg) {
        const auto& bytes = state.vectors[reg].bytes();
        std::array<std::uint8_t, ir::Vector128::kBytes> encoded_bytes{};
        for (std::size_t byte = 0; byte < bytes.size(); ++byte) {
            encoded_bytes[representation.vector_byte_slot[byte]] =
                static_cast<std::uint8_t>(bytes[byte] ^ representation.vector_xor[reg][byte]);
        }
        encoded.slots.vectors[representation.vector_slot[reg]] = ir::Vector128(encoded_bytes);
    }
    const auto flags = flags_to_array(state.flags);
    std::array<bool, 4> encoded_flags{};
    for (std::size_t bit = 0; bit < flags.size(); ++bit) {
        encoded_flags[representation.flag_slot[bit]] = flags[bit] ^ ((representation.flag_polarity >> bit) & 1u);
    }
    encoded.slots.flags = array_to_flags(encoded_flags);
    encoded.slots.sp = state.sp ^ representation.sp_xor;
    return encoded;
}

ArchitecturalState decode_state(const EncodedArchitecturalState& encoded,
                                const StateRepresentation& representation) {
    representation.validate();
    ArchitecturalState decoded;
    for (std::size_t logical = 0; logical < decoded.gpr.size(); ++logical) {
        decoded.gpr[logical] = encoded.slots.gpr[representation.gpr_slot[logical]] ^ representation.gpr_xor[logical];
    }
    for (std::size_t logical = 0; logical < decoded.fp.size(); ++logical) {
        decoded.fp[logical] = encoded.slots.fp[representation.fp_slot[logical]] ^ representation.fp_xor[logical];
    }
    for (std::size_t reg = 0; reg < decoded.vectors.size(); ++reg) {
        const auto& bytes = encoded.slots.vectors[representation.vector_slot[reg]].bytes();
        std::array<std::uint8_t, ir::Vector128::kBytes> decoded_bytes{};
        for (std::size_t byte = 0; byte < bytes.size(); ++byte) {
            decoded_bytes[byte] = static_cast<std::uint8_t>(
                bytes[representation.vector_byte_slot[byte]] ^ representation.vector_xor[reg][byte]);
        }
        decoded.vectors[reg] = ir::Vector128(decoded_bytes);
    }
    const auto flags = flags_to_array(encoded.slots.flags);
    std::array<bool, 4> decoded_flags{};
    for (std::size_t bit = 0; bit < decoded_flags.size(); ++bit) {
        decoded_flags[bit] = flags[representation.flag_slot[bit]] ^ ((representation.flag_polarity >> bit) & 1u);
    }
    decoded.flags = array_to_flags(decoded_flags);
    decoded.sp = encoded.slots.sp ^ representation.sp_xor;
    return decoded;
}

EncodedArchitecturalState transform_live_state(const EncodedArchitecturalState& source,
                                               const StateRepresentation& source_representation,
                                               const EncodedArchitecturalState& destination,
                                               const StateRepresentation& destination_representation,
                                               const LiveStateMask& live) {
    constexpr auto valid_gpr_mask = (std::uint64_t{1} << ir::kVirtualRegisterCount) - 1u;
    if ((live.gpr & ~valid_gpr_mask) != 0 || (live.flags & 0xf0u) != 0) {
        throw std::invalid_argument("live-state mask contains an out-of-range register or flag");
    }
    auto logical_source = decode_state(source, source_representation);
    auto logical_destination = decode_state(destination, destination_representation);
    for (std::size_t reg = 0; reg < logical_source.gpr.size(); ++reg) {
        if ((live.gpr & (std::uint64_t{1} << reg)) != 0) logical_destination.gpr[reg] = logical_source.gpr[reg];
    }
    for (std::size_t reg = 0; reg < logical_source.fp.size(); ++reg) {
        if ((live.fp & (std::uint32_t{1} << reg)) != 0) logical_destination.fp[reg] = logical_source.fp[reg];
        if ((live.vector & (std::uint32_t{1} << reg)) != 0) {
            logical_destination.vectors[reg] = logical_source.vectors[reg];
        }
    }
    const auto source_flags = flags_to_array(logical_source.flags);
    auto destination_flags = flags_to_array(logical_destination.flags);
    for (std::size_t bit = 0; bit < source_flags.size(); ++bit) {
        if ((live.flags & (std::uint8_t{1} << bit)) != 0) destination_flags[bit] = source_flags[bit];
    }
    logical_destination.flags = array_to_flags(destination_flags);
    if (live.sp || (live.gpr & (std::uint64_t{1} << ir::kStackPointerRegister)) != 0) {
        logical_destination.sp = logical_source.sp;
        logical_destination.gpr[ir::kStackPointerRegister] = logical_source.gpr[ir::kStackPointerRegister];
    }
    return encode_state(logical_destination, destination_representation);
}

} // namespace myvmp::vm
