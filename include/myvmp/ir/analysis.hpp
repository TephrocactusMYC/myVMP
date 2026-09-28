#pragma once

#include "myvmp/ir/ir.hpp"

#include <bitset>
#include <cstddef>
#include <vector>

namespace myvmp::ir {

using GprSet = std::bitset<kVirtualRegisterCount>;
using FpSet = std::bitset<32>;
using VectorSet = std::bitset<32>;
using BlockSet = std::vector<std::size_t>;

struct RegisterEffects {
    GprSet gpr_use;
    GprSet gpr_def;
    FpSet fp_use;
    FpSet fp_def;
    VectorSet vector_use;
    VectorSet vector_def;
    bool nzcv_use = false;
    bool nzcv_def = false;
};

struct BlockLiveness {
    GprSet gpr_use;
    GprSet gpr_def;
    GprSet gpr_live_in;
    GprSet gpr_live_out;
    FpSet fp_use;
    FpSet fp_def;
    FpSet fp_live_in;
    FpSet fp_live_out;
    VectorSet vector_use;
    VectorSet vector_def;
    VectorSet vector_live_in;
    VectorSet vector_live_out;
    bool nzcv_use = false;
    bool nzcv_def = false;
    bool nzcv_live_in = false;
    bool nzcv_live_out = false;
};

struct BasicBlock {
    std::size_t id = 0;
    std::size_t begin = 0;
    std::size_t end = 0; // Exclusive instruction index.
    std::vector<std::size_t> successors;
    std::vector<std::size_t> predecessors;
    std::vector<std::size_t> backedge_successors;
    BlockLiveness liveness;
};

struct NaturalLoop {
    std::size_t header = 0;
    std::vector<std::size_t> latches;
    BlockSet blocks;
};

// Read-only, context-insensitive control/data-flow facts for the linear IR.
// CallVm is a call-and-return operation: its target edge and continuation edge
// are both represented, making block boundaries agree with bytecode lowering.
// Consequently, dominance and liveness around CallVm are conservative
// approximations rather than interprocedural summaries.
class CFGAnalysis {
public:
    [[nodiscard]] static CFGAnalysis build(const Program& program);

    [[nodiscard]] const std::vector<BasicBlock>& blocks() const noexcept { return blocks_; }
    [[nodiscard]] const std::vector<std::size_t>& instruction_to_block() const noexcept {
        return instruction_to_block_;
    }
    // Sets are sorted block IDs. Unreachable blocks have only themselves as
    // dominators. Postdominators describe paths that can reach a return.
    [[nodiscard]] const std::vector<BlockSet>& dominators() const noexcept { return dominators_; }
    [[nodiscard]] const std::vector<BlockSet>& postdominators() const noexcept { return postdominators_; }
    [[nodiscard]] const std::vector<NaturalLoop>& natural_loops() const noexcept { return natural_loops_; }

private:
    std::vector<BasicBlock> blocks_;
    std::vector<std::size_t> instruction_to_block_;
    std::vector<BlockSet> dominators_;
    std::vector<BlockSet> postdominators_;
    std::vector<NaturalLoop> natural_loops_;
};

// Describes the architectural register effects of one IR instruction.
// XZR reads are constants and XZR writes are discarded, so register 32 is
// intentionally absent from both sets. FP/vector register indices must be
// in [0, 31]. Unknown opcodes are rejected rather than under-approximated.
[[nodiscard]] RegisterEffects register_effects(const Instruction& instruction);

} // namespace myvmp::ir
