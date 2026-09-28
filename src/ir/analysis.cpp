#include "myvmp/ir/analysis.hpp"

#include <algorithm>
#include <deque>
#include <map>
#include <stdexcept>

namespace myvmp::ir {
namespace {

void use_gpr(RegisterEffects& effects, RegisterId reg) {
    if (reg < kZeroRegister) effects.gpr_use.set(reg);
}

void def_gpr(RegisterEffects& effects, RegisterId reg) {
    if (reg < kZeroRegister) effects.gpr_def.set(reg);
}

void use_fp(RegisterEffects& effects, RegisterId reg) {
    if (reg >= effects.fp_use.size()) throw std::invalid_argument("FP register index out of range in IR analysis");
    effects.fp_use.set(reg);
}

void def_fp(RegisterEffects& effects, RegisterId reg) {
    if (reg >= effects.fp_def.size()) throw std::invalid_argument("FP register index out of range in IR analysis");
    effects.fp_def.set(reg);
}

void use_vector(RegisterEffects& effects, RegisterId reg) {
    if (reg >= effects.vector_use.size()) throw std::invalid_argument("vector register index out of range in IR analysis");
    effects.vector_use.set(reg);
}

void def_vector(RegisterEffects& effects, RegisterId reg) {
    if (reg >= effects.vector_def.size()) throw std::invalid_argument("vector register index out of range in IR analysis");
    effects.vector_def.set(reg);
}

bool condition_reads_nzcv(Condition condition) {
    return condition != Condition::Al && condition != Condition::Nv;
}

void use_condition(RegisterEffects& effects, Condition condition) {
    effects.nzcv_use = condition_reads_nzcv(condition);
}

template <typename Set>
Set set_difference(const Set& lhs, const Set& rhs) {
    Set result = lhs;
    result &= ~rhs;
    return result;
}

BlockSet sorted_union(const BlockSet& lhs, const BlockSet& rhs) {
    BlockSet result;
    std::set_union(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(), std::back_inserter(result));
    return result;
}

BlockSet sorted_intersection(const BlockSet& lhs, const BlockSet& rhs) {
    BlockSet result;
    std::set_intersection(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(), std::back_inserter(result));
    return result;
}

bool contains(const BlockSet& set, std::size_t value) {
    return std::binary_search(set.begin(), set.end(), value);
}

BlockSet all_blocks(std::size_t count) {
    BlockSet result(count);
    for (std::size_t i = 0; i < count; ++i) result[i] = i;
    return result;
}

bool is_conditional_branch(Opcode opcode) {
    return opcode == Opcode::BranchIf || opcode == Opcode::BranchRegZero ||
           opcode == Opcode::BranchRegBit;
}

bool is_terminator(Opcode opcode) {
    return is_conditional_branch(opcode) || opcode == Opcode::CallVm || opcode == Opcode::ReturnReg;
}

void append_unique(std::vector<std::size_t>& values, std::size_t value) {
    if (std::find(values.begin(), values.end(), value) == values.end()) values.push_back(value);
}

} // namespace

RegisterEffects register_effects(const Instruction& instruction) {
    RegisterEffects effects;
    const auto gpr_binary = [&] {
        use_gpr(effects, instruction.src);
        use_gpr(effects, instruction.rhs);
        def_gpr(effects, instruction.dst);
    };
    const auto fp_binary = [&] {
        use_fp(effects, instruction.src);
        use_fp(effects, instruction.rhs);
        def_fp(effects, instruction.dst);
    };
    switch (instruction.opcode) {
    case Opcode::LoadImm: case Opcode::LoadAddress:
        def_gpr(effects, instruction.dst); break;
    case Opcode::AddImm: case Opcode::SubImm: case Opcode::AndImm: case Opcode::OrImm: case Opcode::XorImm:
    case Opcode::AddImmSetFlags: case Opcode::SubImmSetFlags:
    case Opcode::ShiftReg: case Opcode::ShiftReg32: case Opcode::BitfieldExtract:
    case Opcode::MoveReg: case Opcode::LoadMem: case Opcode::Load64: case Opcode::LoadAcquire:
    case Opcode::LoadExclusive:
        use_gpr(effects, instruction.src); def_gpr(effects, instruction.dst);
        if (instruction.opcode == Opcode::AddImmSetFlags || instruction.opcode == Opcode::SubImmSetFlags) effects.nzcv_def = true;
        break;
    case Opcode::AddRegShifted: case Opcode::AddRegExtended: case Opcode::SubRegShifted: case Opcode::SubRegExtended:
    case Opcode::AndRegShifted: case Opcode::BitClearRegShifted: case Opcode::OrRegShifted:
    case Opcode::OrNotRegShifted: case Opcode::XorRegShifted: case Opcode::XorNotRegShifted:
    case Opcode::UnsignedMulHigh: case Opcode::DivideReg: case Opcode::SignedMulLong: case Opcode::UnsignedMulLong:
        gpr_binary(); break;
    case Opcode::AddRegShiftedSetFlags: case Opcode::AddRegExtendedSetFlags:
    case Opcode::SubRegShiftedSetFlags: case Opcode::SubRegExtendedSetFlags:
        gpr_binary(); effects.nzcv_def = true; break;
    case Opcode::NegReg: case Opcode::NotRegShifted:
        use_gpr(effects, instruction.src); def_gpr(effects, instruction.dst); break;
    case Opcode::NegRegSetFlags:
        use_gpr(effects, instruction.src); def_gpr(effects, instruction.dst); effects.nzcv_def = true; break;
    case Opcode::ShiftRegVariable:
        use_gpr(effects, instruction.src); use_gpr(effects, instruction.rhs); def_gpr(effects, instruction.dst); break;
    case Opcode::MulAdd: case Opcode::MulSub:
        use_gpr(effects, instruction.src); use_gpr(effects, instruction.rhs);
        if ((instruction.imm & kMulAccumulatorZero) == 0) use_gpr(effects, instruction.shift_amount);
        def_gpr(effects, instruction.dst); break;
    case Opcode::MoveKeep:
        use_gpr(effects, instruction.dst); def_gpr(effects, instruction.dst); break;
    case Opcode::CompareImm: case Opcode::TestImm:
        use_gpr(effects, instruction.src); effects.nzcv_def = true; break;
    case Opcode::CompareReg: case Opcode::TestRegShifted: case Opcode::TestRegShifted32:
        use_gpr(effects, instruction.src); use_gpr(effects, instruction.rhs); effects.nzcv_def = true; break;
    case Opcode::BranchIf:
        use_condition(effects, instruction.condition); break;
    case Opcode::BranchRegZero: case Opcode::BranchRegBit:
        use_gpr(effects, instruction.src); break;
    case Opcode::CallVm:
        break;
    case Opcode::LoadFpImm64: case Opcode::LoadFpImm32:
        def_fp(effects, instruction.dst); break;
    case Opcode::FpAdd64: case Opcode::FpSub64: case Opcode::FpMul64: case Opcode::FpDiv64:
    case Opcode::FpAdd32: case Opcode::FpSub32: case Opcode::FpMul32: case Opcode::FpDiv32:
    case Opcode::FpMinMax:
        fp_binary(); break;
    case Opcode::FpMadd64: case Opcode::FpMadd32:
        use_fp(effects, instruction.src); use_fp(effects, instruction.rhs);
        use_fp(effects, instruction.shift_amount); def_fp(effects, instruction.dst); break;
    case Opcode::FpCompare64: case Opcode::FpCompare32:
        use_fp(effects, instruction.src); use_fp(effects, instruction.rhs); effects.nzcv_def = true; break;
    case Opcode::FpUnary:
        use_fp(effects, instruction.src); def_fp(effects, instruction.dst); break;
    case Opcode::FpConvert:
        if (instruction.imm <= static_cast<std::uint64_t>(FpConvertKind::Unsigned64ToFp64)) {
            use_gpr(effects, instruction.src); def_fp(effects, instruction.dst);
        } else {
            use_fp(effects, instruction.src); def_gpr(effects, instruction.dst);
        }
        break;
    case Opcode::FpConditionalCompare:
        use_fp(effects, instruction.src); use_fp(effects, instruction.rhs);
        use_condition(effects, instruction.condition); effects.nzcv_def = true; break;
    case Opcode::FpSelect:
        use_fp(effects, instruction.src); use_fp(effects, instruction.rhs);
        def_fp(effects, instruction.dst); use_condition(effects, instruction.condition); break;
    case Opcode::Select: case Opcode::SelectInc: case Opcode::SelectInv: case Opcode::SelectNeg:
        if ((instruction.imm & kSelectTrueZero) == 0) use_gpr(effects, instruction.src);
        if ((instruction.imm & kSelectFalseZero) == 0) use_gpr(effects, instruction.rhs);
        def_gpr(effects, instruction.dst); use_condition(effects, instruction.condition); break;
    case Opcode::Store64: case Opcode::StoreMem: case Opcode::StoreRelease:
        if (instruction.opcode == Opcode::StoreRelease) {
            use_gpr(effects, instruction.src); use_gpr(effects, instruction.rhs);
        } else {
            use_gpr(effects, instruction.src); use_gpr(effects, instruction.rhs);
            if (instruction.opcode == Opcode::StoreMem && instruction.post_index != 0) {
                def_gpr(effects, instruction.src);
            }
        }
        break;
    case Opcode::LoadRegOffsetMem:
        use_gpr(effects, instruction.src); use_gpr(effects, instruction.rhs); def_gpr(effects, instruction.dst); break;
    case Opcode::StoreRegOffsetMem:
        use_gpr(effects, instruction.dst); use_gpr(effects, instruction.src); use_gpr(effects, instruction.rhs); break;
    case Opcode::StorePair64:
        use_gpr(effects, instruction.dst); use_gpr(effects, instruction.src); use_gpr(effects, instruction.rhs); break;
    case Opcode::StoreExclusive:
        use_gpr(effects, instruction.src); use_gpr(effects, instruction.rhs); def_gpr(effects, instruction.dst); break;
    case Opcode::LoadExclusivePair:
        use_gpr(effects, instruction.src); def_gpr(effects, instruction.dst); def_gpr(effects, instruction.rhs);
        break;
    case Opcode::StoreExclusivePair:
        use_gpr(effects, instruction.src); use_gpr(effects, instruction.rhs);
        use_gpr(effects, instruction.shift_amount);
        def_gpr(effects, instruction.dst); break;
    case Opcode::CompareSwap:
        use_gpr(effects, instruction.dst); use_gpr(effects, instruction.src); use_gpr(effects, instruction.rhs);
        def_gpr(effects, instruction.dst); break;
    case Opcode::CallNative: case Opcode::CallNativeReg:
        // A native gate has no per-call signature in this IR. Treat the full
        // virtual machine state as opaque input/output to prevent unsafe
        // live-state elision across the call boundary.
        for (RegisterId reg = 0; reg < kZeroRegister; ++reg) {
            effects.gpr_use.set(reg); effects.gpr_def.set(reg);
        }
        for (std::size_t reg = 0; reg < 32; ++reg) {
            effects.fp_use.set(reg); effects.fp_def.set(reg);
            effects.vector_use.set(reg); effects.vector_def.set(reg);
        }
        if (instruction.opcode == Opcode::CallNativeReg) use_gpr(effects, instruction.src);
        effects.nzcv_use = true; effects.nzcv_def = true;
        break;
    case Opcode::VectorBinary: case Opcode::VectorCompare: case Opcode::VectorFpBinary:
        use_vector(effects, instruction.src); use_vector(effects, instruction.rhs); def_vector(effects, instruction.dst); break;
    case Opcode::VectorSelect:
        use_vector(effects, instruction.src); use_vector(effects, instruction.rhs);
        if (instruction.imm >= 32) throw std::invalid_argument("vector mask register index out of range in IR analysis");
        use_vector(effects, static_cast<RegisterId>(instruction.imm)); def_vector(effects, instruction.dst); break;
    case Opcode::VectorShiftImm:
        use_vector(effects, instruction.src); def_vector(effects, instruction.dst); break;
    case Opcode::VectorPermute:
        use_vector(effects, instruction.src); use_vector(effects, instruction.rhs); def_vector(effects, instruction.dst); break;
    case Opcode::VectorLoad:
        use_gpr(effects, instruction.src); def_vector(effects, instruction.dst); break;
    case Opcode::VectorStore:
        use_vector(effects, instruction.dst); use_gpr(effects, instruction.src); break;
    case Opcode::VectorMoveImmZero:
        def_vector(effects, instruction.dst); break;
    case Opcode::NoOp:
        break;
    case Opcode::ReturnReg:
        use_gpr(effects, instruction.src); break;
    default:
        throw std::invalid_argument("unknown IR opcode in register-effect analysis");
    }
    return effects;
}

CFGAnalysis CFGAnalysis::build(const Program& program) {
    program.validate();
    const auto& instructions = program.instructions();
    const auto instruction_count = instructions.size();
    std::vector<std::size_t> starts{0};
    for (std::size_t index = 0; index < instruction_count; ++index) {
        const auto& instruction = instructions[index];
        if (is_terminator(instruction.opcode)) {
            if (index + 1 < instruction_count) starts.push_back(index + 1);
            if (instruction.opcode != Opcode::ReturnReg) starts.push_back(instruction.target);
        }
    }
    std::sort(starts.begin(), starts.end());
    starts.erase(std::unique(starts.begin(), starts.end()), starts.end());

    CFGAnalysis result;
    result.instruction_to_block_.resize(instruction_count);
    result.blocks_.reserve(starts.size());
    for (std::size_t block_id = 0; block_id < starts.size(); ++block_id) {
        const auto end = block_id + 1 < starts.size() ? starts[block_id + 1] : instruction_count;
        BasicBlock block;
        block.id = block_id;
        block.begin = starts[block_id];
        block.end = end;
        result.blocks_.push_back(std::move(block));
        for (auto instruction = starts[block_id]; instruction < end; ++instruction) {
            result.instruction_to_block_[instruction] = block_id;
        }
    }

    const auto add_edge = [&result](std::size_t from, std::size_t to) {
        append_unique(result.blocks_[from].successors, to);
        append_unique(result.blocks_[to].predecessors, from);
    };
    for (auto& block : result.blocks_) {
        const auto& last = instructions[block.end - 1];
        if (last.opcode == Opcode::ReturnReg) continue;
        if (last.opcode == Opcode::BranchIf || last.opcode == Opcode::BranchRegZero ||
            last.opcode == Opcode::BranchRegBit || last.opcode == Opcode::CallVm) {
            add_edge(block.id, result.instruction_to_block_[last.target]);
        }
        if (last.opcode != Opcode::BranchIf && last.opcode != Opcode::BranchRegZero &&
            last.opcode != Opcode::BranchRegBit && last.opcode != Opcode::CallVm) {
            if (block.id + 1 < result.blocks_.size()) add_edge(block.id, block.id + 1);
        } else if (block.id + 1 < result.blocks_.size()) {
            add_edge(block.id, block.id + 1);
        }
    }

    const auto block_count = result.blocks_.size();
    const auto empty = BlockSet{};
    const auto all = all_blocks(block_count);
    std::vector<bool> reachable(block_count, false);
    std::deque<std::size_t> worklist{0};
    reachable[0] = true;
    while (!worklist.empty()) {
        const auto block = worklist.front(); worklist.pop_front();
        for (const auto successor : result.blocks_[block].successors) {
            if (!reachable[successor]) { reachable[successor] = true; worklist.push_back(successor); }
        }
    }

    result.dominators_.assign(block_count, all);
    result.dominators_[0] = {0};
    for (std::size_t block = 0; block < block_count; ++block) {
        if (!reachable[block]) result.dominators_[block] = {block};
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t block = 1; block < block_count; ++block) {
            if (!reachable[block]) continue;
            BlockSet next = all;
            bool has_reachable_predecessor = false;
            for (const auto predecessor : result.blocks_[block].predecessors) {
                if (!reachable[predecessor]) continue;
                next = has_reachable_predecessor ? sorted_intersection(next, result.dominators_[predecessor])
                                                 : result.dominators_[predecessor];
                has_reachable_predecessor = true;
            }
            if (!has_reachable_predecessor) next.clear();
            next = sorted_union(next, BlockSet{block});
            if (next != result.dominators_[block]) { result.dominators_[block] = std::move(next); changed = true; }
        }
    }

    std::vector<bool> reaches_exit(block_count, false);
    std::deque<std::size_t> reverse_worklist;
    for (const auto& block : result.blocks_) {
        if (instructions[block.end - 1].opcode == Opcode::ReturnReg) {
            reaches_exit[block.id] = true;
            reverse_worklist.push_back(block.id);
        }
    }
    while (!reverse_worklist.empty()) {
        const auto block = reverse_worklist.front(); reverse_worklist.pop_front();
        for (const auto predecessor : result.blocks_[block].predecessors) {
            if (!reaches_exit[predecessor]) { reaches_exit[predecessor] = true; reverse_worklist.push_back(predecessor); }
        }
    }
    result.postdominators_.assign(block_count, empty);
    for (std::size_t block = 0; block < block_count; ++block) {
        if (reaches_exit[block]) result.postdominators_[block] = all;
        else result.postdominators_[block] = {block};
    }
    for (const auto& block : result.blocks_) {
        if (instructions[block.end - 1].opcode == Opcode::ReturnReg) result.postdominators_[block.id] = {block.id};
    }
    changed = true;
    while (changed) {
        changed = false;
        for (std::size_t block = 0; block < block_count; ++block) {
            if (!reaches_exit[block] || instructions[result.blocks_[block].end - 1].opcode == Opcode::ReturnReg) continue;
            BlockSet successors;
            for (const auto successor : result.blocks_[block].successors) {
                if (reaches_exit[successor]) successors.push_back(successor);
            }
            if (successors.empty()) continue;
            auto next = result.postdominators_[successors.front()];
            for (std::size_t i = 1; i < successors.size(); ++i) {
                next = sorted_intersection(next, result.postdominators_[successors[i]]);
            }
            next = sorted_union(next, BlockSet{block});
            if (next != result.postdominators_[block]) { result.postdominators_[block] = std::move(next); changed = true; }
        }
    }

    std::map<std::size_t, NaturalLoop> loops_by_header;
    for (auto& block : result.blocks_) {
        for (const auto successor : block.successors) {
            if (!reachable[block.id] || !contains(result.dominators_[block.id], successor)) continue;
            block.backedge_successors.push_back(successor);
            auto& loop = loops_by_header[successor];
            loop.header = successor;
            append_unique(loop.latches, block.id);
            std::vector<std::size_t> loop_worklist{block.id};
            std::vector<bool> in_loop(block_count, false);
            for (const auto existing : loop.blocks) in_loop[existing] = true;
            in_loop[successor] = true;
            in_loop[block.id] = true;
            while (!loop_worklist.empty()) {
                const auto current = loop_worklist.back(); loop_worklist.pop_back();
                for (const auto predecessor : result.blocks_[current].predecessors) {
                    if (!reachable[predecessor] || in_loop[predecessor]) continue;
                    in_loop[predecessor] = true;
                    loop_worklist.push_back(predecessor);
                }
            }
            loop.blocks.clear();
            for (std::size_t id = 0; id < block_count; ++id) if (in_loop[id]) loop.blocks.push_back(id);
        }
    }
    for (auto& [header, loop] : loops_by_header) {
        (void)header;
        result.natural_loops_.push_back(std::move(loop));
    }

    for (auto& block : result.blocks_) {
        for (std::size_t index = block.begin; index < block.end; ++index) {
            const auto effects = register_effects(instructions[index]);
            auto& live = block.liveness;
            live.gpr_use |= set_difference(effects.gpr_use, live.gpr_def);
            live.gpr_def |= effects.gpr_def;
            live.fp_use |= set_difference(effects.fp_use, live.fp_def);
            live.fp_def |= effects.fp_def;
            live.vector_use |= set_difference(effects.vector_use, live.vector_def);
            live.vector_def |= effects.vector_def;
            live.nzcv_use = live.nzcv_use || (effects.nzcv_use && !live.nzcv_def);
            live.nzcv_def = live.nzcv_def || effects.nzcv_def;
        }
    }
    changed = true;
    while (changed) {
        changed = false;
        for (std::size_t reverse = block_count; reverse > 0; --reverse) {
            auto& block = result.blocks_[reverse - 1];
            auto& live = block.liveness;
            GprSet gpr_out;
            FpSet fp_out;
            VectorSet vector_out;
            bool nzcv_out = false;
            for (const auto successor : block.successors) {
                gpr_out |= result.blocks_[successor].liveness.gpr_live_in;
                fp_out |= result.blocks_[successor].liveness.fp_live_in;
                vector_out |= result.blocks_[successor].liveness.vector_live_in;
                nzcv_out = nzcv_out || result.blocks_[successor].liveness.nzcv_live_in;
            }
            const auto gpr_in = live.gpr_use | set_difference(gpr_out, live.gpr_def);
            const auto fp_in = live.fp_use | set_difference(fp_out, live.fp_def);
            const auto vector_in = live.vector_use | set_difference(vector_out, live.vector_def);
            const auto nzcv_in = live.nzcv_use || (nzcv_out && !live.nzcv_def);
            if (live.gpr_live_out != gpr_out || live.fp_live_out != fp_out ||
                live.vector_live_out != vector_out || live.nzcv_live_out != nzcv_out ||
                live.gpr_live_in != gpr_in || live.fp_live_in != fp_in ||
                live.vector_live_in != vector_in || live.nzcv_live_in != nzcv_in) {
                live.gpr_live_out = gpr_out; live.fp_live_out = fp_out;
                live.vector_live_out = vector_out; live.nzcv_live_out = nzcv_out;
                live.gpr_live_in = gpr_in; live.fp_live_in = fp_in;
                live.vector_live_in = vector_in; live.nzcv_live_in = nzcv_in;
                changed = true;
            }
        }
    }
    return result;
}

} // namespace myvmp::ir
