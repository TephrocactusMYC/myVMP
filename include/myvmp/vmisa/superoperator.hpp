#pragma once

#include "myvmp/ir/analysis.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

// Analysis-only superoperator mining. These descriptors do not currently
// generate bytecode or native handlers; they are bounded inputs to a future
// profile-specific ISA/handler synthesis stage.
namespace myvmp::vmisa {

struct SuperoperatorOccurrence {
    std::size_t block_id{};
    std::size_t begin{}; // Inclusive instruction index.
    std::size_t end{};   // Exclusive instruction index.
};

struct SuperoperatorCandidate {
    std::vector<ir::Opcode> opcodes;
    std::vector<SuperoperatorOccurrence> occurrences;
    std::uint64_t profile_tag{};
};

struct SuperoperatorMiningOptions {
    std::uint64_t profile_seed{};
    std::size_t min_length = 2;
    std::size_t max_length = 4;
    std::size_t max_candidates = 32;
    std::size_t max_occurrences_per_candidate = 64;
    std::size_t min_frequency = 2;
};

// Mine deterministic, frequent opcode subsequences. Candidates are confined
// to basic blocks and to side-effect-free scalar register operations, so no
// memory, call, flag, or control-flow semantics are silently fused.
[[nodiscard]] std::vector<SuperoperatorCandidate> mine_superoperators(
    const ir::Program& program,
    const ir::CFGAnalysis& cfg,
    const SuperoperatorMiningOptions& options = {});

[[nodiscard]] bool validate_superoperator_candidate(
    const ir::Program& program,
    const ir::CFGAnalysis& cfg,
    const SuperoperatorCandidate& candidate,
    std::size_t max_length = 4);

} // namespace myvmp::vmisa
