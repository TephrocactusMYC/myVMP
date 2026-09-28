#include "myvmp/vmisa/superoperator.hpp"

#include <algorithm>
#include <map>
#include <tuple>

namespace myvmp::vmisa {
namespace {

using Key = std::vector<ir::Opcode>;

struct KeyLess {
    bool operator()(const Key& lhs, const Key& rhs) const noexcept {
        return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
            [](ir::Opcode a, ir::Opcode b) {
                return static_cast<std::uint8_t>(a) < static_cast<std::uint8_t>(b);
            });
    }
};

bool is_safe_pure_opcode(ir::Opcode opcode) noexcept {
    using ir::Opcode;
    switch (opcode) {
    case Opcode::LoadImm:
    case Opcode::AddImm:
    case Opcode::SubImm:
    case Opcode::AndImm:
    case Opcode::OrImm:
    case Opcode::XorImm:
    case Opcode::AddRegShifted:
    case Opcode::AddRegExtended:
    case Opcode::SubRegShifted:
    case Opcode::SubRegExtended:
    case Opcode::NegReg:
    case Opcode::AndRegShifted:
    case Opcode::BitClearRegShifted:
    case Opcode::NotRegShifted:
    case Opcode::OrRegShifted:
    case Opcode::OrNotRegShifted:
    case Opcode::XorRegShifted:
    case Opcode::XorNotRegShifted:
    case Opcode::ShiftReg:
    case Opcode::ShiftReg32:
    case Opcode::ShiftRegVariable:
    case Opcode::DivideReg:
    case Opcode::BitfieldExtract:
    case Opcode::UnsignedMulHigh:
    case Opcode::MulAdd:
    case Opcode::MulSub:
    case Opcode::SignedMulLong:
    case Opcode::UnsignedMulLong:
    case Opcode::MoveReg:
    case Opcode::MoveKeep:
    case Opcode::Select:
    case Opcode::SelectInc:
    case Opcode::SelectInv:
    case Opcode::SelectNeg:
        return true;
    default:
        return false;
    }
}

std::uint64_t mix(std::uint64_t value) noexcept {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

std::uint64_t tag_for(const Key& key, std::uint64_t seed) noexcept {
    auto value = mix(seed ^ 0x53555045524f5045ULL);
    for (const auto opcode : key) {
        value = mix(value ^ static_cast<std::uint8_t>(opcode));
    }
    return value;
}

bool options_valid(const SuperoperatorMiningOptions& options) noexcept {
    return options.min_length >= 2 && options.max_length >= options.min_length &&
           options.max_length <= 8 && options.max_candidates > 0 &&
           options.max_occurrences_per_candidate > 0 && options.min_frequency > 0;
}

} // namespace

std::vector<SuperoperatorCandidate> mine_superoperators(
    const ir::Program& program,
    const ir::CFGAnalysis& cfg,
    const SuperoperatorMiningOptions& options) {
    if (!options_valid(options)) return {};

    const auto& instructions = program.instructions();
    const auto& blocks = cfg.blocks();
    const auto& instruction_to_block = cfg.instruction_to_block();
    if (instruction_to_block.size() != instructions.size()) return {};

    std::map<Key, std::vector<SuperoperatorOccurrence>, KeyLess> found;
    for (const auto& block : blocks) {
        if (block.begin > block.end || block.end > instructions.size()) continue;
        for (std::size_t begin = block.begin; begin < block.end; ++begin) {
            Key key;
            const auto limit = std::min(block.end, begin + options.max_length);
            for (std::size_t end = begin; end < limit; ++end) {
                if (instruction_to_block[end] != block.id ||
                    !is_safe_pure_opcode(instructions[end].opcode)) break;
                key.push_back(instructions[end].opcode);
                if (key.size() < options.min_length) continue;
                auto& occurrences = found[key];
                if (occurrences.size() < options.max_occurrences_per_candidate) {
                    occurrences.push_back({block.id, begin, end + 1});
                }
            }
        }
    }

    struct Ranked {
        SuperoperatorCandidate candidate;
        std::size_t observed_count{};
    };
    std::vector<Ranked> ranked;
    for (auto& [key, occurrences] : found) {
        // Occurrences are capped for memory bounds; requiring the cap to meet
        // min_frequency is conservative when a sequence is extremely common.
        if (occurrences.size() < options.min_frequency) continue;
        const auto tag = tag_for(key, options.profile_seed);
        const auto observed_count = occurrences.size();
        ranked.push_back({{key, std::move(occurrences), tag}, observed_count});
    }
    std::sort(ranked.begin(), ranked.end(), [](const Ranked& lhs, const Ranked& rhs) {
        if (lhs.observed_count != rhs.observed_count) return lhs.observed_count > rhs.observed_count;
        if (lhs.candidate.profile_tag != rhs.candidate.profile_tag)
            return lhs.candidate.profile_tag < rhs.candidate.profile_tag;
        return KeyLess{}(lhs.candidate.opcodes, rhs.candidate.opcodes);
    });

    std::vector<SuperoperatorCandidate> result;
    const auto count = std::min(options.max_candidates, ranked.size());
    result.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (validate_superoperator_candidate(program, cfg, ranked[i].candidate,
                                              options.max_length)) {
            result.push_back(std::move(ranked[i].candidate));
        }
    }
    return result;
}

bool validate_superoperator_candidate(const ir::Program& program,
                                      const ir::CFGAnalysis& cfg,
                                      const SuperoperatorCandidate& candidate,
                                      std::size_t max_length) {
    if (candidate.opcodes.size() < 2 || candidate.opcodes.size() > max_length ||
        candidate.occurrences.empty()) return false;
    const auto& instructions = program.instructions();
    const auto& blocks = cfg.blocks();
    const auto& mapping = cfg.instruction_to_block();
    if (mapping.size() != instructions.size()) return false;
    for (const auto opcode : candidate.opcodes) {
        if (!is_safe_pure_opcode(opcode)) return false;
    }
    for (const auto& occurrence : candidate.occurrences) {
        if (occurrence.block_id >= blocks.size() || occurrence.begin >= occurrence.end ||
            occurrence.end - occurrence.begin != candidate.opcodes.size() ||
            occurrence.end > instructions.size()) return false;
        const auto& block = blocks[occurrence.block_id];
        if (occurrence.begin < block.begin || occurrence.end > block.end) return false;
        for (std::size_t i = 0; i < candidate.opcodes.size(); ++i) {
            const auto index = occurrence.begin + i;
            if (mapping[index] != occurrence.block_id ||
                instructions[index].opcode != candidate.opcodes[i] ||
                !is_safe_pure_opcode(instructions[index].opcode)) return false;
        }
    }
    return true;
}

} // namespace myvmp::vmisa
