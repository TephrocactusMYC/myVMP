#pragma once

#include "myvmp/ir/ir.hpp"
#include "myvmp/vm/vm.hpp"

#include <array>
#include <cstdint>

namespace myvmp::vm {

// Host-model state representation for a single VM profile. This descriptor is not
// consumed by the native runtimes yet; it provides a deterministic, reversible model
// for designing and validating profile-to-profile state bridges.
struct StateRepresentation {
    std::uint32_t profile_id{};
    std::array<std::uint8_t, ir::kVirtualRegisterCount> gpr_slot{};
    std::array<std::uint8_t, 32> fp_slot{};
    std::array<std::uint8_t, 32> vector_slot{};
    std::array<std::uint8_t, ir::Vector128::kBytes> vector_byte_slot{};
    std::array<std::uint8_t, 4> flag_slot{};
    std::array<std::uint64_t, ir::kVirtualRegisterCount> gpr_xor{};
    std::array<std::uint64_t, 32> fp_xor{};
    std::array<std::array<std::uint8_t, ir::Vector128::kBytes>, 32> vector_xor{};
    std::uint64_t sp_xor{};
    std::uint8_t flag_polarity{};

    void validate() const;
};

struct ArchitecturalState {
    std::array<std::uint64_t, ir::kVirtualRegisterCount> gpr{};
    std::array<std::uint64_t, 32> fp{};
    ir::VectorRegisterFile vectors{};
    Flags flags{};
    std::uint64_t sp{};
};

struct LiveStateMask {
    std::uint64_t gpr{}; // bits 0..32; SP has its own bit and storage.
    std::uint32_t fp{};
    std::uint32_t vector{};
    std::uint8_t flags{}; // Z/N/C/V in bits 0..3.
    bool sp{};
};

struct EncodedArchitecturalState {
    ArchitecturalState slots{}; // arrays indexed by profile-local slots.
};

// Same seed/profile pair always produces the same descriptor. Different profile IDs
// perturb all layouts and masks independently.
[[nodiscard]] StateRepresentation make_state_representation(std::uint32_t profile_id,
                                                              std::uint64_t seed);
[[nodiscard]] EncodedArchitecturalState encode_state(const ArchitecturalState& state,
                                                      const StateRepresentation& representation);
[[nodiscard]] ArchitecturalState decode_state(const EncodedArchitecturalState& state,
                                               const StateRepresentation& representation);

// Transform only live architectural components. Non-live destination values retain
// their prior logical values. This models the intended liveness-aware bridge semantics.
[[nodiscard]] EncodedArchitecturalState transform_live_state(
    const EncodedArchitecturalState& source,
    const StateRepresentation& source_representation,
    const EncodedArchitecturalState& destination,
    const StateRepresentation& destination_representation,
    const LiveStateMask& live);

} // namespace myvmp::vm
