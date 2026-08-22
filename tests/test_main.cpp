#include "myvmp/arm32/decoder.hpp"
#include "myvmp/arm64/decoder.hpp"
#include "myvmp/bytecode/bytecode.hpp"
#include "myvmp/elf/elf.hpp"
#include "myvmp/ir/ir.hpp"
#include "myvmp/patch/aarch64_trampoline.hpp"
#include "myvmp/patch/arm32_trampoline.hpp"
#include "myvmp/protect/aarch64.hpp"
#include "myvmp/runtime/aarch64.hpp"
#include "myvmp/runtime/arm32.hpp"
#include "myvmp/riscv64/decoder.hpp"
#include "myvmp/patch/riscv64_trampoline.hpp"
#include "myvmp/vm/vm.hpp"
#include "myvmp/vm/multi_vm.hpp"

#include <algorithm>
#include <cstdlib>
#include <array>
#include <bit>
#include <exception>
#include <iostream>
#include <elf.h>
#include <stdexcept>
#include <set>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void write_u64_le(std::vector<std::uint8_t>& memory, std::size_t offset, std::uint64_t value) {
    require(offset <= memory.size() && 8 <= memory.size() - offset, "test memory write out of range");
    for (int shift = 0; shift < 64; shift += 8) {
        memory[offset + static_cast<std::size_t>(shift / 8)] = static_cast<std::uint8_t>((value >> shift) & 0xffu);
    }
}

void write_u32_le(std::vector<std::uint8_t>& memory, std::size_t offset, std::uint32_t value) {
    require(offset <= memory.size() && 4 <= memory.size() - offset, "test memory write out of range");
    for (int shift = 0; shift < 32; shift += 8) {
        memory[offset + static_cast<std::size_t>(shift / 8)] = static_cast<std::uint8_t>((value >> shift) & 0xffu);
    }
}

std::uint64_t read_u64_le(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    require(offset <= bytes.size() && 8 <= bytes.size() - offset, "test bytecode read out of range");
    std::uint64_t value = 0;
    for (unsigned shift = 0; shift < 64; shift += 8) {
        value |= static_cast<std::uint64_t>(bytes[offset + shift / 8]) << shift;
    }
    return value;
}

double byte_distance(const std::vector<std::uint8_t>& lhs, const std::vector<std::uint8_t>& rhs) {
    const auto limit = std::min(lhs.size(), rhs.size());
    const auto total = limit + (lhs.size() > rhs.size() ? lhs.size() - rhs.size() : rhs.size() - lhs.size());
    if (total == 0) {
        return 0.0;
    }
    std::size_t different = total - limit;
    for (std::size_t i = 0; i < limit; ++i) {
        if (lhs[i] != rhs[i]) {
            ++different;
        }
    }
    return static_cast<double>(different) / static_cast<double>(total);
}

void test_ir_validation() {
    myvmp::ir::Program program;
    program.load_imm(0, 42);
    bool threw = false;
    try {
        program.validate();
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "program without return must fail validation");

    program.return_reg(0);
    program.validate();
}

void test_vm_executes_encoded_program() {
    myvmp::ir::Program program;
    program.load_imm(0, 5);
    program.add_imm(0, 0, 7);
    program.sub_imm(0, 0, 2);
    program.return_reg(0);

    const auto image = myvmp::bytecode::encode(program, {.key = 0xA5A5A5A5u});
    const auto decoded = myvmp::bytecode::decode_payload(image);
    require(!decoded.empty(), "decoded payload must not be empty");
    require(image.bytes.size() > decoded.size(), "image must include header");

    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image);
    require(result.value == 10, "VM arithmetic result mismatch");
    require(result.instructions_executed == 4, "VM instruction count mismatch");
}

void test_vm_executes_register_ops() {
    myvmp::ir::Program program;
    program.load_imm(0, 5);
    program.add_imm(1, 0, 5);
    program.load_imm(2, 0x35);
    program.xor_reg(1, 1, 2);
    program.add_reg_shifted(0, 1, 0, myvmp::ir::ShiftKind::Lsl, 1);
    program.return_reg(0);

    const auto image = myvmp::bytecode::encode(program, {.key = 0x51515151u});
    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image);
    require(result.value == 73, "VM register operation result mismatch");
}

void test_vm_executes_logical_shift_and_movk() {
    myvmp::ir::Program program;
    program.load_imm(0, 0x1111);
    program.move_keep(0, 0x2222, 16);
    program.load_imm(1, 0xff);
    program.xor_reg_shifted(2, 0, 1, myvmp::ir::ShiftKind::Lsl, 8);
    program.or_reg_shifted(3, 2, 1, myvmp::ir::ShiftKind::Lsl, 32);
    program.and_reg_shifted(0, 3, 3, myvmp::ir::ShiftKind::Lsl, 0);
    program.return_reg(0);

    const auto image = myvmp::bytecode::encode(program, {.key = 0x61616161u});
    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image);
    const std::uint64_t movk = 0x22221111ull;
    const std::uint64_t xored = movk ^ (0xffull << 8);
    const std::uint64_t expected = xored | (0xffull << 32);
    require(result.value == expected, "VM logical shifted or MOVK result mismatch");
}

void test_vm_executes_branch_and_select() {
    myvmp::ir::Program program;
    program.load_imm(0, 1);
    program.compare_imm(0, 1);
    program.branch_if(myvmp::ir::Condition::Ne, 5);
    program.load_imm(1, 10);
    program.load_imm(2, 20);
    program.select(0, 1, 2, myvmp::ir::Condition::Eq);
    program.return_reg(0);

    const auto image = myvmp::bytecode::encode(program, {.key = 0x71717171u});
    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image);
    require(result.value == 10, "VM branch/select result mismatch");
}

void test_diversified_immediates_preserve_branches_and_hide_literal() {
    constexpr std::uint64_t kLiteral = 0x0123456789abcdefull;
    myvmp::ir::Program program;
    program.load_imm(0, 1);
    program.compare_imm(0, 1);
    program.branch_if(myvmp::ir::Condition::Ne, 5);
    program.load_imm(1, kLiteral);
    program.load_imm(2, 20);
    program.select(0, 1, 2, myvmp::ir::Condition::Eq);
    program.return_reg(0);

    const auto profile = myvmp::bytecode::profile_from_seed(0x13579bdu);
    const auto image = myvmp::bytecode::encode(program, {
        .key = 0x71717171u,
        .profile = profile,
        .diversify_immediates = true,
        .diversification_seed = profile.seed,
        .shuffle_blocks = true,
    });
    const auto decoded = myvmp::bytecode::decode_payload(image);
    bool saw_compare_branch_superoperator = false;
    for (std::size_t pc = 0; pc < decoded.size();) {
        const auto opcode = profile.logical(decoded[pc]);
        saw_compare_branch_superoperator |= opcode == myvmp::bytecode::Opcode::CompareImmBranch;
        pc += 1 + myvmp::bytecode::operand_size(opcode);
    }
    require(saw_compare_branch_superoperator,
            "bounded CompareImm+BranchIf superoperator must be selected inside one basic block");
    require(image.branch_targets_are_offsets,
            "shuffled bytecode must mark control-flow targets as payload offsets");
    const std::array<std::uint8_t, 8> literal_bytes{
        0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01,
    };
    require(std::search(decoded.begin(), decoded.end(), literal_bytes.begin(), literal_bytes.end()) == decoded.end(),
            "diversified bytecode must not retain the original LoadImm literal");

    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image);
    require(result.value == kLiteral, "diversified immediate or remapped branch changed semantics");
    require(result.instructions_executed > program.instructions().size(),
            "diversified immediate lowering must emit additional VM work");

    constexpr std::uint64_t kAdd = 0x1111222233334444ull;
    constexpr std::uint64_t kSub = 0x0102030405060708ull;
    constexpr std::uint64_t kXor = 0xfedcba9876543210ull;
    myvmp::ir::Program arithmetic;
    arithmetic.load_imm(0, kLiteral);
    arithmetic.add_imm(0, 0, kAdd);
    arithmetic.sub_imm(0, 0, kSub);
    arithmetic.xor_imm(0, 0, kXor);
    arithmetic.return_reg(0);
    const auto arithmetic_image = myvmp::bytecode::encode(arithmetic, {
        .key = 0x81818181u,
        .profile = profile,
        .diversify_immediates = true,
        .diversification_seed = profile.seed,
    });
    const auto arithmetic_payload = myvmp::bytecode::decode_payload(arithmetic_image);
    const auto contains_u64 = [&arithmetic_payload](std::uint64_t value) {
        std::array<std::uint8_t, 8> bytes{};
        for (unsigned shift = 0; shift < 64; shift += 8) {
            bytes[shift / 8] = static_cast<std::uint8_t>(value >> shift);
        }
        return std::search(arithmetic_payload.begin(), arithmetic_payload.end(),
                           bytes.begin(), bytes.end()) != arithmetic_payload.end();
    };
    require(!contains_u64(kLiteral) && !contains_u64(kAdd) &&
                !contains_u64(kSub) && !contains_u64(kXor),
            "diversified arithmetic must split original immediate literals");
    const std::array<std::uint8_t, 7> duplicate_opcodes{
        profile.load_imm64_duplicate, profile.add_imm64_duplicate,
        profile.sub_imm64_duplicate, profile.xor_imm64_duplicate,
        profile.and_imm64_duplicate, profile.or_imm64_duplicate,
        profile.move_reg64_duplicate,
    };
    require(std::any_of(arithmetic_payload.begin(), arithmetic_payload.end(),
                        [&](std::uint8_t byte) {
                            return std::find(duplicate_opcodes.begin(), duplicate_opcodes.end(), byte) !=
                                   duplicate_opcodes.end();
                        }),
            "diversified arithmetic must select duplicate semantic opcodes per occurrence");
    require(vm.execute(arithmetic_image).value == ((kLiteral + kAdd - kSub) ^ kXor),
            "diversified arithmetic immediate sequence changed semantics");
    std::set<std::size_t> diversified_sizes;
    for (std::uint32_t seed = 1; seed <= 32; ++seed) {
        const auto seeded_profile = myvmp::bytecode::profile_from_seed(seed);
        const auto seeded_image = myvmp::bytecode::encode(arithmetic, {
            .key = seed ^ 0x81818181u,
            .profile = seeded_profile,
            .diversify_immediates = true,
            .diversification_seed = seed,
        });
        require(vm.execute(seeded_image).value == ((kLiteral + kAdd - kSub) ^ kXor),
                "multi-template immediate diversification changed semantics");
        diversified_sizes.insert(myvmp::bytecode::decode_payload(seeded_image).size());
    }
    require(diversified_sizes.size() >= 3,
            "seeded immediate diversification must select several two/three-stage layouts");

    myvmp::ir::Program taken_branch;
    taken_branch.load_imm(0, 0);
    taken_branch.compare_imm(0, 1);
    taken_branch.branch_if(myvmp::ir::Condition::Ne, 4);
    taken_branch.load_imm(0, 0xbadull);
    taken_branch.return_reg(0);
    const auto taken_image = myvmp::bytecode::encode(taken_branch, {
        .key = 0x91919191u,
        .profile = profile,
        .diversify_immediates = true,
        .diversification_seed = profile.seed,
    });
    require(vm.execute(taken_image).value == 0,
            "taken branch target was not remapped after immediate expansion");
    require(taken_image.blocks.size() == 3,
            "branching bytecode must expose explicit basic-block objects");
    require(taken_image.blocks[0].first_ir_instruction == 0 &&
                taken_image.blocks[0].ir_instruction_count == 3 &&
                taken_image.blocks[0].successors.size() == 2 &&
                taken_image.blocks[0].successors[0] == 2 &&
                taken_image.blocks[0].successors[1] == 1,
            "conditional block successors must include target and fallthrough");
    require(taken_image.blocks[1].payload_offset + taken_image.blocks[1].payload_size ==
                taken_image.blocks[2].payload_offset,
            "explicit bytecode block ranges must be contiguous before shuffling");
    require(taken_image.blocks.back().payload_offset + taken_image.blocks.back().payload_size ==
                myvmp::bytecode::decode_payload(taken_image).size(),
            "explicit bytecode blocks must own the complete payload including Halt");

    std::set<std::vector<std::uint32_t>> block_layouts;
    bool saw_explicit_successor = false;
    for (std::uint32_t seed = 1; seed <= 16; ++seed) {
        const auto shuffled_profile = myvmp::bytecode::profile_from_seed(seed);
        const auto shuffled = myvmp::bytecode::encode(taken_branch, {
            .key = seed ^ 0x91919191u,
            .profile = shuffled_profile,
            .diversify_immediates = true,
            .diversification_seed = seed,
            .shuffle_blocks = true,
        });
        require(vm.execute(shuffled).value == 0,
                "explicit-successor block shuffling changed branch semantics");
        std::vector<std::uint32_t> layout;
        for (const auto& block : shuffled.blocks) layout.push_back(block.id);
        block_layouts.insert(std::move(layout));
        const auto shuffled_payload = myvmp::bytecode::decode_payload(shuffled);
        for (std::size_t pc = 0; pc < shuffled_payload.size();) {
            const auto opcode = shuffled_profile.logical(shuffled_payload[pc]);
            saw_explicit_successor |= opcode == myvmp::bytecode::Opcode::JumpBlock;
            pc += 1 + myvmp::bytecode::operand_size(opcode);
        }
    }
    require(block_layouts.size() >= 2,
            "seeded bytecode block shuffling must produce multiple physical layouts");
    require(saw_explicit_successor,
            "shuffled conditional fallthrough must use an explicit successor jump");

    const auto require_superoperator = [&](const myvmp::ir::Program& candidate,
                                           myvmp::bytecode::Opcode expected,
                                           std::uint64_t expected_value) {
        const auto fused = myvmp::bytecode::encode(candidate, {
            .key = 0xb1b1b1b1u, .profile = profile,
            .diversify_immediates = true, .diversification_seed = profile.seed,
            .shuffle_blocks = true,
        });
        require(vm.execute(fused).value == expected_value,
                "extended flag/branch superoperator changed semantics");
        const auto bytes = myvmp::bytecode::decode_payload(fused);
        bool found = false;
        for (std::size_t pc = 0; pc < bytes.size();) {
            const auto opcode = profile.logical(bytes[pc]);
            found |= opcode == expected;
            pc += 1 + myvmp::bytecode::operand_size(opcode);
        }
        require(found, "expected bounded flag/branch superoperator was not emitted");
    };
    myvmp::ir::Program compare_reg_branch;
    compare_reg_branch.load_imm(0, 7);
    compare_reg_branch.load_imm(1, 7);
    compare_reg_branch.compare_reg(0, 1);
    compare_reg_branch.branch_if(myvmp::ir::Condition::Eq, 5);
    compare_reg_branch.load_imm(0, 0xbad);
    compare_reg_branch.return_reg(0);
    require_superoperator(compare_reg_branch, myvmp::bytecode::Opcode::CompareRegBranch, 7);

    myvmp::ir::Program test_imm_branch;
    test_imm_branch.load_imm(0, 8);
    test_imm_branch.test_imm(0, 8);
    test_imm_branch.branch_if(myvmp::ir::Condition::Ne, 4);
    test_imm_branch.load_imm(0, 0xbad);
    test_imm_branch.return_reg(0);
    require_superoperator(test_imm_branch, myvmp::bytecode::Opcode::TestImmBranch, 8);

    const auto encoded_load_operands = [&profile](std::uint32_t target) {
        myvmp::ir::Program block_program;
        block_program.load_imm(0, 0);
        block_program.branch_if(myvmp::ir::Condition::Ne, target);
        block_program.no_op();
        block_program.load_imm(1, kLiteral);
        block_program.return_reg(1);
        const auto block_image = myvmp::bytecode::encode(block_program, {
            .key = 0xa1a1a1a1u,
            .profile = profile,
            .diversify_immediates = true,
            .diversification_seed = profile.seed,
        });
        const auto payload = myvmp::bytecode::decode_payload(block_image);
        std::vector<std::uint64_t> operands;
        for (std::size_t pc = 0; pc < payload.size();) {
            const auto opcode = profile.logical(payload[pc]);
            if (opcode == myvmp::bytecode::Opcode::LoadImm64) {
                operands.push_back(read_u64_le(payload, pc + 2));
            }
            pc += 1 + myvmp::bytecode::operand_size(opcode);
        }
        return operands;
    };
    const auto extra_leader = encoded_load_operands(3);
    const auto no_extra_leader = encoded_load_operands(4);
    require(extra_leader.size() == 2 && no_extra_leader.size() == 2,
            "block-aware diversification fixture must contain two loads");
    require(extra_leader[1] != no_extra_leader[1],
            "the basic-block identity must contribute to immediate reconstruction");
}

void test_vm_executes_immediate_and_extended_sub() {
    myvmp::ir::Program program;
    program.load_imm(0, 0x1234);
    program.load_imm(1, 0xdeadbeef);
    program.sub_reg_extended(2, 0, 1, myvmp::ir::ExtendKind::Uxtb, 0);
    program.and_imm(0, 2, 0xff);
    program.return_reg(0);

    const auto image = myvmp::bytecode::encode(program, {.key = 0x81818181u});
    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image);
    require(result.value == ((0x1234ull - 0xefull) & 0xffull), "VM AND imm or SUB extended result mismatch");
}

void test_vm_executes_32bit_extended_flag_arithmetic() {
    myvmp::ir::Program program;
    program.load_imm(0, 0x100000000ull);
    program.load_imm(1, 0);
    program.sub_reg_extended_set_flags(31, 0, 1, myvmp::ir::ExtendKind::Uxtw, 0, true);
    program.branch_if(myvmp::ir::Condition::Eq, 6);
    program.load_imm(2, 0);
    program.return_reg(2);
    program.load_imm(2, 1);
    program.return_reg(2);

    const auto image = myvmp::bytecode::encode(program, {.key = 0x82828282u});
    myvmp::vm::Interpreter vm;
    require(vm.execute(image).value == 1, "32-bit SUBS extended must compute W-register flags");
}

void test_vm_executes_internal_call_stack() {
    myvmp::ir::Program program;
    program.load_imm(0, 5);
    program.call_vm(4);
    program.add_imm(0, 0, 2);
    program.return_reg(0);
    program.add_imm(0, 0, 7);
    program.return_reg(0);

    myvmp::vm::Interpreter vm;
    require(vm.execute(myvmp::bytecode::encode(program)).value == 14,
            "VM internal call stack must return to the following bytecode instruction");
}

void test_vm_executes_divide_and_variable_shift() {
    myvmp::ir::Program program;
    program.load_imm(0, static_cast<std::uint64_t>(-21ll));
    program.load_imm(1, 4);
    program.divide_reg(2, 0, 1, true, false);
    program.load_imm(3, 65);
    program.shift_reg_variable(4, 2, 3, myvmp::ir::ShiftKind::Ror, false);
    program.return_reg(4);

    const auto image = myvmp::bytecode::encode(program, {.key = 0x83838383u});
    myvmp::vm::Interpreter vm;
    require(vm.execute(image).value == 0xfffffffffffffffdull,
            "signed divide or register-masked variable rotate mismatch");

    myvmp::ir::Program divide_by_zero;
    divide_by_zero.load_imm(0, 123);
    divide_by_zero.load_imm(1, 0);
    divide_by_zero.divide_reg(0, 0, 1, false, true);
    divide_by_zero.return_reg(0);
    require(vm.execute(myvmp::bytecode::encode(divide_by_zero)).value == 0,
            "ARM division by zero must produce zero");
}

void test_aarch64_runtime_payload_builder() {
    myvmp::ir::Program program;
    program.load_imm(1, 7);
    program.add_imm(0, 1, 2);
    program.return_reg(0);
    const auto image = myvmp::bytecode::encode(program);
    const auto decoded = myvmp::bytecode::decode_payload(image);
    const auto payload = myvmp::runtime::build_aarch64_runtime_payload(image);
    require(payload.size() == myvmp::runtime::aarch64_runtime_code_size() + 32 + 256 +
                                  image.blocks.size() * 4 + decoded.size(),
            "AArch64 runtime payload size mismatch");
    require(!std::equal(decoded.begin(), decoded.end(),
                        payload.end() - static_cast<std::ptrdiff_t>(decoded.size())),
            "AArch64 runtime payload must not expose decoded bytecode");
    require(!std::equal(image.bytes.begin() + 5, image.bytes.end(),
                        payload.end() - static_cast<std::ptrdiff_t>(decoded.size())),
            "AArch64 runtime payload must add CFG-local rolling encryption");
    require(myvmp::bytecode::read_u32_le(payload, myvmp::runtime::aarch64_runtime_code_size() + 8) ==
                image.profile.handler_variant_mask,
            "AArch64 runtime header must carry the handler variant mask");
    require(myvmp::bytecode::read_u32_le(payload, myvmp::runtime::aarch64_runtime_code_size() + 12) ==
                image.blocks.size(),
            "AArch64 runtime header must carry the bytecode block count");
    require(myvmp::bytecode::read_u32_le(payload, myvmp::runtime::aarch64_runtime_code_size() + 16) ==
                image.profile.dispatcher_xor_key,
            "AArch64 runtime header must carry the dispatcher XOR key");
    const auto operand_table = myvmp::runtime::aarch64_runtime_code_size() + 32 +
                               image.blocks.size() * 4;
    std::size_t codec_physical_opcodes = 0;
    for (std::uint32_t physical = 0; physical < 256; ++physical) {
        std::optional<myvmp::bytecode::Opcode> logical;
        try {
            logical = image.profile.logical(static_cast<std::uint8_t>(physical));
        } catch (const std::runtime_error&) {
        }
        if (!logical) continue;
        const auto domain = static_cast<std::uint8_t>(physical ^ image.profile.dispatcher_xor_key);
        require(payload[operand_table + domain] == myvmp::bytecode::operand_size(*logical),
                "runtime operand codec table disagrees with the central bytecode format");
        ++codec_physical_opcodes;
    }
    require(codec_physical_opcodes >= 90,
            "runtime operand codec must cover the complete physical opcode profile");
    require(myvmp::bytecode::read_u32_le(payload, myvmp::runtime::aarch64_runtime_code_size() + 32) ==
                decoded.size(),
            "single-block runtime descriptor must own the complete payload");
    auto invalid_blocks = image;
    invalid_blocks.blocks.front().payload_offset = 1;
    bool rejected_invalid_blocks = false;
    try {
        (void)myvmp::runtime::build_aarch64_runtime_payload(invalid_blocks);
    } catch (const std::runtime_error&) {
        rejected_invalid_blocks = true;
    }
    require(rejected_invalid_blocks,
            "AArch64 runtime builder must reject non-contiguous block metadata");

    const auto dynamic_image = myvmp::bytecode::encode(
        program, {.key = 0xa5a5a5a4u, .profile = myvmp::bytecode::profile_from_seed(1)});
    const auto dynamic_payload = myvmp::runtime::build_aarch64_runtime_payload(dynamic_image);
    require(dynamic_payload != payload,
            "dynamic AArch64 profiles must produce distinct runtime payloads");

    myvmp::ir::Program final_handler;
    final_handler.load_imm(0, 1);
    final_handler.neg_reg(0, 0, myvmp::ir::ShiftKind::Lsl, 0);
    final_handler.no_op();
    final_handler.return_reg(0);
    require(!myvmp::runtime::build_aarch64_runtime_payload(
                 myvmp::bytecode::encode(final_handler)).empty(),
            "AArch64 runtime builder must cover the complete bytecode opcode set");
}

void test_dynamic_profiles_change_bytecode_not_semantics() {
    myvmp::ir::Program program;
    program.load_imm(0, 5);
    program.add_imm(0, 0, 7);
    program.sub_imm(0, 0, 2);
    program.return_reg(0);

    const auto profile_a = myvmp::bytecode::profile_from_seed(1);
    const auto profile_b = myvmp::bytecode::profile_from_seed(2);
    require(profile_a.dispatcher_xor_key != 0 && profile_b.dispatcher_xor_key != 0,
            "seeded profiles must select the transformed dispatcher organization");
    const auto image_a = myvmp::bytecode::encode(program, {.key = 0x11111111u, .profile = profile_a});
    const auto image_b = myvmp::bytecode::encode(program, {.key = 0x11111111u, .profile = profile_b});

    require(image_a.bytes != image_b.bytes, "different VM profiles should produce different bytecode");
    require(profile_a.manifest() != profile_b.manifest(), "different VM profiles should produce different manifests");

    myvmp::vm::Interpreter vm;
    require(vm.execute(image_a).value == 10, "profile A semantic mismatch");
    require(vm.execute(image_b).value == 10, "profile B semantic mismatch");

    std::set<std::vector<std::uint8_t>> payloads;
    std::set<std::string> manifests;
    std::set<std::uint32_t> handler_variants;
    std::uint32_t covered_variant_bits = 0;
    std::vector<std::vector<std::uint8_t>> encoded;
    for (std::uint32_t seed = 1; seed <= 16; ++seed) {
        const auto profile = myvmp::bytecode::profile_from_seed(seed);
        const auto image = myvmp::bytecode::encode(program, {.key = seed ^ 0xA5A5A5A5u, .profile = profile});
        require(vm.execute(image).value == 10, "seed-sweep profile semantic mismatch");
        payloads.insert(image.bytes);
        manifests.insert(profile.manifest());
        handler_variants.insert(profile.handler_variant_mask);
        covered_variant_bits |= profile.handler_variant_mask;
        encoded.push_back(image.bytes);
    }
    require(payloads.size() == 16, "seed sweep must produce unique bytecode payloads");
    require(manifests.size() == 16, "seed sweep must produce unique profile manifests");
    require(handler_variants.size() > 1 &&
                covered_variant_bits == myvmp::bytecode::kAllHandlerVariants,
            "seed sweep must select both variants of every implemented native handler");

    double min_distance = 1.0;
    for (std::size_t lhs = 0; lhs < encoded.size(); ++lhs) {
        for (std::size_t rhs = lhs + 1; rhs < encoded.size(); ++rhs) {
            min_distance = std::min(min_distance, byte_distance(encoded[lhs], encoded[rhs]));
        }
    }
    require(min_distance >= 0.10, "seed sweep bytecode distance is too low");
}

void test_vm_profile_demo_seed_round_trip() {
    myvmp::ir::Program program;
    program.load_imm(0, 5);
    program.add_imm(0, 0, 7);
    program.sub_imm(0, 0, 2);
    program.return_reg(0);

    const auto profile = myvmp::bytecode::profile_from_seed(0x20260812u);
    std::set<std::uint8_t> physical;
    physical.insert(profile.load_imm64);
    physical.insert(profile.load_address64);
    physical.insert(profile.add_imm64);
    physical.insert(profile.sub_imm64);
    physical.insert(profile.and_imm64);
    physical.insert(profile.or_imm64);
    physical.insert(profile.xor_imm64);
    physical.insert(profile.add_imm_set_flags64);
    physical.insert(profile.sub_imm_set_flags64);
    physical.insert(profile.add_reg_shifted64);
    physical.insert(profile.add_reg_shifted_set_flags64);
    physical.insert(profile.add_reg_extended64);
    physical.insert(profile.add_reg_extended_set_flags64);
    physical.insert(profile.sub_reg_shifted64);
    physical.insert(profile.sub_reg_shifted_set_flags64);
    physical.insert(profile.sub_reg_extended64);
    physical.insert(profile.sub_reg_extended_set_flags64);
    physical.insert(profile.neg_reg64);
    physical.insert(profile.neg_reg_set_flags64);
    physical.insert(profile.and_reg_shifted64);
    physical.insert(profile.bit_clear_reg_shifted64);
    physical.insert(profile.not_reg_shifted64);
    physical.insert(profile.or_reg_shifted64);
    physical.insert(profile.or_not_reg_shifted64);
    physical.insert(profile.xor_reg_shifted64);
    physical.insert(profile.xor_not_reg_shifted64);
    physical.insert(profile.shift_reg64);
    physical.insert(profile.shift_reg32);
    physical.insert(profile.shift_reg_variable64);
    physical.insert(profile.divide_reg64);
    physical.insert(profile.bitfield_extract64);
    physical.insert(profile.unsigned_mul_high64);
    physical.insert(profile.mul_add64);
    physical.insert(profile.mul_sub64);
    physical.insert(profile.signed_mul_long64);
    physical.insert(profile.unsigned_mul_long64);
    physical.insert(profile.move_keep64);
    physical.insert(profile.move_reg64);
    physical.insert(profile.compare_imm64);
    physical.insert(profile.compare_reg64);
    physical.insert(profile.test_imm64);
    physical.insert(profile.test_reg_shifted64);
    physical.insert(profile.test_reg_shifted32);
    physical.insert(profile.branch_if);
    physical.insert(profile.branch_reg_zero);
    physical.insert(profile.branch_reg_bit);
    physical.insert(profile.select64);
    physical.insert(profile.select_inc64);
    physical.insert(profile.select_inv64);
    physical.insert(profile.select_neg64);
    physical.insert(profile.load64);
    physical.insert(profile.store64);
    physical.insert(profile.load_mem);
    physical.insert(profile.store_mem);
    physical.insert(profile.load_reg_offset_mem);
    physical.insert(profile.store_reg_offset_mem);
    physical.insert(profile.store_pair64);
    physical.insert(profile.call_native);
    physical.insert(profile.call_native_reg);
    physical.insert(profile.call_vm);
    physical.insert(profile.load_fp_imm64);
    physical.insert(profile.fp_add64);
    physical.insert(profile.fp_sub64);
    physical.insert(profile.fp_mul64);
    physical.insert(profile.fp_div64);
    physical.insert(profile.fp_madd64);
    physical.insert(profile.fp_compare64);
    physical.insert(profile.load_fp_imm32);
    physical.insert(profile.fp_add32);
    physical.insert(profile.fp_sub32);
    physical.insert(profile.fp_mul32);
    physical.insert(profile.fp_div32);
    physical.insert(profile.fp_madd32);
    physical.insert(profile.fp_compare32);
    physical.insert(profile.load_acquire);
    physical.insert(profile.store_release);
    physical.insert(profile.load_exclusive);
    physical.insert(profile.store_exclusive);
    physical.insert(profile.compare_swap);
    physical.insert(profile.fp_unary);
    physical.insert(profile.fp_convert);
    physical.insert(profile.fp_min_max);
    physical.insert(profile.no_op);
    physical.insert(profile.ret);
    physical.insert(profile.halt);
    physical.insert(profile.load_imm64_duplicate);
    physical.insert(profile.add_imm64_duplicate);
    physical.insert(profile.sub_imm64_duplicate);
    physical.insert(profile.xor_imm64_duplicate);
    physical.insert(profile.compare_imm_branch);
    physical.insert(profile.jump_block);
    physical.insert(profile.compare_reg_branch);
    physical.insert(profile.test_imm_branch);
    require(physical.size() == 93, "VM profile physical, duplicate, and superoperator opcode map contains collisions");

    const auto image = myvmp::bytecode::encode(program, {.key = 0x12345678u, .profile = profile});
    myvmp::vm::Interpreter vm;
    require(vm.execute(image).value == 10, "demo seed profile semantic mismatch");
}

void test_arm64_decoder() {
    const myvmp::arm64::Decoder decoder;

    const auto movz = decoder.decode(0xD28000A0u);
    require(movz.has_value(), "MOVZ must decode");
    require(movz->mnemonic == myvmp::arm64::Mnemonic::Movz, "MOVZ mnemonic mismatch");
    require(movz->rd == 0, "MOVZ rd mismatch");
    require(movz->imm == 5, "MOVZ immediate mismatch");

    const auto adr = decoder.decode(0x10000040u);
    require(adr.has_value(), "ADR must decode");
    require(adr->mnemonic == myvmp::arm64::Mnemonic::Adr, "ADR mnemonic mismatch");
    require(adr->rd == 0 && adr->branch_offset == 8, "ADR operands mismatch");

    const auto adrp = decoder.decode(0xB0000020u);
    require(adrp.has_value(), "ADRP must decode");
    require(adrp->mnemonic == myvmp::arm64::Mnemonic::Adrp, "ADRP mnemonic mismatch");
    require(adrp->rd == 0 && adrp->branch_offset == 0x5000, "ADRP operands mismatch");

    const auto ldr_literal = decoder.decode(0x58000040u);
    require(ldr_literal.has_value(), "LDR literal must decode");
    require(ldr_literal->mnemonic == myvmp::arm64::Mnemonic::LoadLiteral, "LDR literal mnemonic mismatch");
    require(ldr_literal->rd == 0 && ldr_literal->branch_offset == 8 && ldr_literal->mem_width == 8, "LDR literal operands mismatch");

    const auto madd = decoder.decode(0x9B020C20u);
    require(madd.has_value(), "MADD must decode");
    require(madd->mnemonic == myvmp::arm64::Mnemonic::MulAdd, "MADD mnemonic mismatch");
    require(madd->rd == 0 && madd->rn == 1 && madd->rm == 2 && madd->imm == 3, "MADD operands mismatch");

    const auto mul = decoder.decode(0x9B0A7D28u);
    require(mul.has_value(), "MUL alias must decode");
    require(mul->mnemonic == myvmp::arm64::Mnemonic::MulAdd, "MUL alias mnemonic mismatch");
    require(mul->rd == 8 && mul->rn == 9 && mul->rm == 10 && mul->imm == 31, "MUL alias operands mismatch");

    const auto smull = decoder.decode(0x9B377ED5u);
    require(smull.has_value(), "SMULL alias must decode");
    require(smull->mnemonic == myvmp::arm64::Mnemonic::SignedMulLong, "SMULL mnemonic mismatch");
    require(smull->rd == 21 && smull->rn == 22 && smull->rm == 23, "SMULL operands mismatch");

    const auto ldar = decoder.decode(0xC8DFFC20u);
    require(ldar.has_value(), "LDAR must decode");
    require(ldar->mnemonic == myvmp::arm64::Mnemonic::LoadAcquire, "LDAR mnemonic mismatch");
    require(ldar->rd == 0 && ldar->rn == 1 && ldar->mem_width == 8, "LDAR operands mismatch");

    const auto stxr = decoder.decode(0xC8067D07u);
    require(stxr.has_value(), "STXR must decode");
    require(stxr->mnemonic == myvmp::arm64::Mnemonic::StoreExclusive, "STXR mnemonic mismatch");
    require(stxr->rd == 6 && stxr->rn == 8 && stxr->rm == 7, "STXR operands mismatch");

    const std::array<std::pair<std::uint32_t, std::uint8_t>, 4> acquire_widths{{
        {0x08DFFC20u, 1}, {0x48DFFC62u, 2}, {0x88DFFCA4u, 4}, {0xC8DFFCE6u, 8},
    }};
    for (const auto& [word, width] : acquire_widths) {
        const auto decoded = decoder.decode(word);
        require(decoded.has_value() && decoded->mnemonic == myvmp::arm64::Mnemonic::LoadAcquire,
                "LDAR width form must decode");
        require(decoded->mem_width == width, "LDAR width mismatch");
    }
    const std::array<std::pair<std::uint32_t, std::uint8_t>, 4> release_widths{{
        {0x089FFD28u, 1}, {0x489FFD6Au, 2}, {0x889FFDACu, 4}, {0xC89FFDEEu, 8},
    }};
    for (const auto& [word, width] : release_widths) {
        const auto decoded = decoder.decode(word);
        require(decoded.has_value() && decoded->mnemonic == myvmp::arm64::Mnemonic::StoreRelease,
                "STLR width form must decode");
        require(decoded->mem_width == width, "STLR width mismatch");
    }
    const std::array<std::tuple<std::uint32_t, std::uint8_t, bool>, 8> exclusive_loads{{
        {0x085F7E30u, 1, false}, {0x085FFE72u, 1, true},
        {0x485F7EB4u, 2, false}, {0x485FFEF6u, 2, true},
        {0x885F7F38u, 4, false}, {0x885FFF7Au, 4, true},
        {0xC85F7FBCu, 8, false}, {0xC85FFC20u, 8, true},
    }};
    for (const auto& [word, width, ordered] : exclusive_loads) {
        const auto decoded = decoder.decode(word);
        require(decoded.has_value() && decoded->mnemonic == myvmp::arm64::Mnemonic::LoadExclusive,
                "LDXR/LDAXR width form must decode");
        require(decoded->mem_width == width && (decoded->imm != 0) == ordered,
                "LDXR/LDAXR width or acquire flag mismatch");
    }
    const std::array<std::tuple<std::uint32_t, std::uint8_t, bool>, 8> exclusive_stores{{
        {0x08027C83u, 1, false}, {0x0805FCE6u, 1, true},
        {0x48087D49u, 2, false}, {0x480BFDACu, 2, true},
        {0x880E7E0Fu, 4, false}, {0x8811FE72u, 4, true},
        {0xC8147ED5u, 8, false}, {0xC817FF38u, 8, true},
    }};
    for (const auto& [word, width, ordered] : exclusive_stores) {
        const auto decoded = decoder.decode(word);
        require(decoded.has_value() && decoded->mnemonic == myvmp::arm64::Mnemonic::StoreExclusive,
                "STXR/STLXR width form must decode");
        require(decoded->mem_width == width && (decoded->imm != 0) == ordered,
                "STXR/STLXR width or release flag mismatch");
    }
    const std::array<std::tuple<std::uint32_t, std::uint8_t, std::uint64_t>, 12> compare_swaps{{
        {0x08A07C41u, 1, 0}, {0x08E3FCA4u, 1, 3},
        {0x48A67D07u, 2, 0}, {0x48E9FD6Au, 2, 3},
        {0x88A07C41u, 4, 0}, {0x88E37CA4u, 4, 1},
        {0x88A6FD07u, 4, 2}, {0x88E9FD6Au, 4, 3},
        {0xC8AC7DCDu, 8, 0}, {0xC8EF7E30u, 8, 1},
        {0xC8B2FE93u, 8, 2}, {0xC8F5FEF6u, 8, 3},
    }};
    for (const auto& [word, width, ordering] : compare_swaps) {
        const auto decoded = decoder.decode(word);
        require(decoded.has_value() && decoded->mnemonic == myvmp::arm64::Mnemonic::CompareSwap,
                "CAS ordering form must decode");
        require(decoded->mem_width == width && decoded->imm == ordering,
                "CAS width or ordering mismatch");
    }

    const auto dmb = decoder.decode(0xD5033BBFu);
    require(dmb.has_value(), "DMB must decode");
    require(dmb->mnemonic == myvmp::arm64::Mnemonic::Barrier, "DMB mnemonic mismatch");

    const std::array<std::uint32_t, 8> fp_unary_words{{
        0x1E20C020u, 0x1E60C062u, 0x1E2140A4u, 0x1E6140E6u,
        0x1E21C128u, 0x1E61C16Au, 0x1E22C1ACu, 0x1E6241EEu,
    }};
    for (std::size_t kind = 0; kind < fp_unary_words.size(); ++kind) {
        const auto decoded = decoder.decode(fp_unary_words[kind]);
        require(decoded.has_value() && decoded->mnemonic == myvmp::arm64::Mnemonic::FpUnary,
                "FP unary/conversion form must decode");
        require(decoded->imm == kind, "FP unary/conversion kind mismatch");
    }
    const std::array<std::uint32_t, 16> fp_convert_words{{
        0x1E220020u, 0x1E620062u, 0x9E2200A4u, 0x9E6200E6u,
        0x1E230128u, 0x1E63016Au, 0x9E2301ACu, 0x9E6301EEu,
        0x1E380230u, 0x1E780272u, 0x9E3802B4u, 0x9E7802F6u,
        0x1E390338u, 0x1E79037Au, 0x9E3903BCu, 0x9E790020u,
    }};
    for (std::size_t kind = 0; kind < fp_convert_words.size(); ++kind) {
        const auto decoded = decoder.decode(fp_convert_words[kind]);
        require(decoded.has_value() && decoded->mnemonic == myvmp::arm64::Mnemonic::FpConvert,
                "integer/FP conversion form must decode");
        require(decoded->imm == kind, "integer/FP conversion kind mismatch");
    }
    const std::array<std::uint32_t, 8> fp_min_max_words{{
        0x1E245862u, 0x1E6758C5u, 0x1E2A4928u, 0x1E6D498Bu,
        0x1E3079EEu, 0x1E737A51u, 0x1E366AB4u, 0x1E796B17u,
    }};
    for (std::size_t kind = 0; kind < fp_min_max_words.size(); ++kind) {
        const auto decoded = decoder.decode(fp_min_max_words[kind]);
        require(decoded.has_value() && decoded->mnemonic == myvmp::arm64::Mnemonic::FpMinMax,
                "FP min/max form must decode");
        require(decoded->imm == kind, "FP min/max kind mismatch");
    }

    const auto add = decoder.decode(0x91001C00u);
    require(add.has_value(), "ADD immediate must decode");
    require(add->mnemonic == myvmp::arm64::Mnemonic::AddImm, "ADD mnemonic mismatch");
    require(add->rd == 0 && add->rn == 0 && add->imm == 7, "ADD operands mismatch");

    const auto sub = decoder.decode(0xD1000800u);
    require(sub.has_value(), "SUB immediate must decode");
    require(sub->mnemonic == myvmp::arm64::Mnemonic::SubImm, "SUB mnemonic mismatch");
    require(sub->rd == 0 && sub->rn == 0 && sub->imm == 2, "SUB operands mismatch");

    const auto cmn = decoder.decode(0xB100141Fu);
    require(cmn.has_value(), "CMN immediate must decode");
    require(cmn->mnemonic == myvmp::arm64::Mnemonic::AddImmSetFlags, "CMN immediate mnemonic mismatch");
    require(cmn->rd == 31 && cmn->rn == 0 && cmn->imm == 5, "CMN immediate operands mismatch");

    const auto adds = decoder.decode(0xB1001C83u);
    require(adds.has_value(), "ADDS immediate must decode");
    require(adds->mnemonic == myvmp::arm64::Mnemonic::AddImmSetFlags, "ADDS immediate mnemonic mismatch");
    require(adds->rd == 3 && adds->rn == 4 && adds->imm == 7, "ADDS immediate operands mismatch");

    const auto subs = decoder.decode(0xF10024C5u);
    require(subs.has_value(), "SUBS immediate must decode");
    require(subs->mnemonic == myvmp::arm64::Mnemonic::SubImmSetFlags, "SUBS immediate mnemonic mismatch");
    require(subs->rd == 5 && subs->rn == 6 && subs->imm == 9, "SUBS immediate operands mismatch");

    const auto eor = decoder.decode(0xCA020021u);
    require(eor.has_value(), "EOR shifted register must decode");
    require(eor->mnemonic == myvmp::arm64::Mnemonic::EorShiftedReg, "EOR mnemonic mismatch");
    require(eor->rd == 1 && eor->rn == 1 && eor->rm == 2, "EOR operands mismatch");

    const auto eor_shift = decoder.decode(0xCA010C41u);
    require(eor_shift.has_value(), "EOR shifted register with shift must decode");
    require(eor_shift->mnemonic == myvmp::arm64::Mnemonic::EorShiftedReg, "EOR shifted mnemonic mismatch");
    require(eor_shift->rd == 1 && eor_shift->rn == 2 && eor_shift->rm == 1, "EOR shifted operands mismatch");
    require(eor_shift->shift_amount == 3, "EOR shifted amount mismatch");

    const auto orr = decoder.decode(0xAA030021u);
    require(orr.has_value(), "ORR shifted register must decode");
    require(orr->mnemonic == myvmp::arm64::Mnemonic::OrrShiftedReg, "ORR mnemonic mismatch");
    require(orr->rd == 1 && orr->rn == 1 && orr->rm == 3, "ORR operands mismatch");

    const auto neg = decoder.decode(0xCB0103E0u);
    require(neg.has_value(), "NEG shifted register alias must decode");
    require(neg->mnemonic == myvmp::arm64::Mnemonic::SubShiftedReg, "NEG alias mnemonic mismatch");
    require(neg->rd == 0 && neg->rn == 31 && neg->rm == 1, "NEG alias operands mismatch");

    const auto negs = decoder.decode(0xEB0303E2u);
    require(negs.has_value(), "NEGS shifted register alias must decode");
    require(negs->mnemonic == myvmp::arm64::Mnemonic::SubShiftedRegSetFlags, "NEGS alias mnemonic mismatch");
    require(negs->rd == 2 && negs->rn == 31 && negs->rm == 3, "NEGS alias operands mismatch");

    const auto mvn = decoder.decode(0xAA2503E4u);
    require(mvn.has_value(), "MVN shifted register alias must decode");
    require(mvn->mnemonic == myvmp::arm64::Mnemonic::OrrShiftedReg, "MVN alias mnemonic mismatch");
    require(mvn->rd == 4 && mvn->rn == 31 && mvn->rm == 5 && mvn->imm == 1, "MVN alias operands mismatch");

    const auto bl = decoder.decode(0x94000002u);
    require(bl.has_value(), "BL must decode for explicit unsupported diagnostics");
    require(bl->mnemonic == myvmp::arm64::Mnemonic::BranchLink, "BL mnemonic mismatch");
    require(bl->branch_offset == 2, "BL target offset mismatch");

    const auto br = decoder.decode(0xD61F0200u);
    require(br.has_value(), "BR must decode for explicit unsupported diagnostics");
    require(br->mnemonic == myvmp::arm64::Mnemonic::BranchReg && br->rn == 16, "BR operands mismatch");

    const auto blr = decoder.decode(0xD63F0280u);
    require(blr.has_value(), "BLR must decode for explicit unsupported diagnostics");
    require(blr->mnemonic == myvmp::arm64::Mnemonic::BranchLinkReg && blr->rn == 20, "BLR operands mismatch");

    const auto movk = decoder.decode(0xF2AAB543u);
    require(movk.has_value(), "MOVK must decode");
    require(movk->mnemonic == myvmp::arm64::Mnemonic::Movk, "MOVK mnemonic mismatch");
    require(movk->rd == 3 && movk->imm == 0x55aa && movk->shift_amount == 16, "MOVK operands mismatch");

    const auto movn = decoder.decode(0x92824680u);
    require(movn.has_value(), "MOVN must decode");
    require(movn->mnemonic == myvmp::arm64::Mnemonic::Movn, "MOVN mnemonic mismatch");
    require(movn->rd == 0 && movn->imm == 0xffffffffffffedcbull, "MOVN operands mismatch");

    const auto lsl = decoder.decode(0xD37FF8A7u);
    require(lsl.has_value(), "LSL immediate alias must decode");
    require(lsl->mnemonic == myvmp::arm64::Mnemonic::LslImm, "LSL mnemonic mismatch");
    require(lsl->rd == 7 && lsl->rn == 5 && lsl->shift_amount == 1, "LSL operands mismatch");

    const auto asr = decoder.decode(0x9347FC41u);
    require(asr.has_value(), "ASR immediate alias must decode");
    require(asr->mnemonic == myvmp::arm64::Mnemonic::AsrImm, "ASR mnemonic mismatch");
    require(asr->rd == 1 && asr->rn == 2 && asr->shift_amount == 7, "ASR operands mismatch");

    const auto ror = decoder.decode(0x93C42483u);
    require(ror.has_value(), "ROR immediate alias must decode");
    require(ror->mnemonic == myvmp::arm64::Mnemonic::RorImm, "ROR mnemonic mismatch");
    require(ror->rd == 3 && ror->rn == 4 && ror->shift_amount == 9, "ROR operands mismatch");

    const auto asr32 = decoder.decode(0x13047C20u);
    require(asr32.has_value(), "32-bit ASR immediate alias must decode");
    require(asr32->mnemonic == myvmp::arm64::Mnemonic::AsrImm, "32-bit ASR mnemonic mismatch");
    require(asr32->rd == 0 && asr32->rn == 1 && asr32->shift_amount == 4 && asr32->writes_32bit, "32-bit ASR operands mismatch");

    const auto ror32 = decoder.decode(0x13832062u);
    require(ror32.has_value(), "32-bit ROR immediate alias must decode");
    require(ror32->mnemonic == myvmp::arm64::Mnemonic::RorImm, "32-bit ROR mnemonic mismatch");
    require(ror32->rd == 2 && ror32->rn == 3 && ror32->shift_amount == 8 && ror32->writes_32bit, "32-bit ROR operands mismatch");

    const auto sxtb = decoder.decode(0x93401C20u);
    require(sxtb.has_value(), "SXTB alias must decode");
    require(sxtb->mnemonic == myvmp::arm64::Mnemonic::SignExtendReg, "SXTB mnemonic mismatch");
    require(sxtb->rd == 0 && sxtb->rn == 1 && sxtb->mem_width == 1, "SXTB operands mismatch");

    const auto sxth = decoder.decode(0x93403C62u);
    require(sxth.has_value(), "SXTH alias must decode");
    require(sxth->mnemonic == myvmp::arm64::Mnemonic::SignExtendReg, "SXTH mnemonic mismatch");
    require(sxth->rd == 2 && sxth->rn == 3 && sxth->mem_width == 2, "SXTH operands mismatch");

    const auto sxtw = decoder.decode(0x93407CA4u);
    require(sxtw.has_value(), "SXTW alias must decode");
    require(sxtw->mnemonic == myvmp::arm64::Mnemonic::SignExtendReg, "SXTW mnemonic mismatch");
    require(sxtw->rd == 4 && sxtw->rn == 5 && sxtw->mem_width == 4, "SXTW operands mismatch");

    const auto ands = decoder.decode(0xEA020020u);
    require(ands.has_value(), "ANDS shifted register must decode");
    require(ands->mnemonic == myvmp::arm64::Mnemonic::AndSetFlagsShiftedReg, "ANDS mnemonic mismatch");
    require(ands->rd == 0 && ands->rn == 1 && ands->rm == 2, "ANDS operands mismatch");

    const auto tst_shifted = decoder.decode(0xEA01041Fu);
    require(tst_shifted.has_value(), "TST shifted register must decode");
    require(tst_shifted->mnemonic == myvmp::arm64::Mnemonic::AndSetFlagsShiftedReg, "TST shifted mnemonic mismatch");
    require(tst_shifted->rd == 31 && tst_shifted->rn == 0 && tst_shifted->rm == 1 && tst_shifted->shift_amount == 1, "TST shifted operands mismatch");

    const auto ands32 = decoder.decode(0x6A020420u);
    require(ands32.has_value(), "32-bit ANDS shifted register must decode");
    require(ands32->mnemonic == myvmp::arm64::Mnemonic::AndSetFlagsShiftedReg, "32-bit ANDS mnemonic mismatch");
    require(ands32->rd == 0 && ands32->rn == 1 && ands32->rm == 2 && ands32->shift_amount == 1 && ands32->writes_32bit, "32-bit ANDS operands mismatch");

    const auto tst32 = decoder.decode(0x6A44087Fu);
    require(tst32.has_value(), "32-bit TST shifted register must decode");
    require(tst32->mnemonic == myvmp::arm64::Mnemonic::AndSetFlagsShiftedReg, "32-bit TST shifted mnemonic mismatch");
    require(tst32->rd == 31 && tst32->rn == 3 && tst32->rm == 4 && tst32->shift_amount == 2 && tst32->writes_32bit, "32-bit TST shifted operands mismatch");

    const auto tst = decoder.decode(0xF240049Fu);
    require(tst.has_value(), "TST immediate must decode");
    require(tst->mnemonic == myvmp::arm64::Mnemonic::TestImm, "TST mnemonic mismatch");
    require(tst->rn == 4 && tst->imm == 3, "TST operands mismatch");

    const auto bne = decoder.decode(0x54FFFE61u);
    require(bne.has_value(), "B.NE must decode");
    require(bne->mnemonic == myvmp::arm64::Mnemonic::BranchCond, "B.NE mnemonic mismatch");
    require(bne->condition == myvmp::ir::Condition::Ne && bne->branch_offset == -13, "B.NE operands mismatch");

    const auto blt = decoder.decode(0x5400004Bu);
    require(blt.has_value(), "B.LT must decode");
    require(blt->mnemonic == myvmp::arm64::Mnemonic::BranchCond, "B.LT mnemonic mismatch");
    require(blt->condition == myvmp::ir::Condition::Lt && blt->branch_offset == 2, "B.LT operands mismatch");

    const auto branch = decoder.decode(0x14000002u);
    require(branch.has_value(), "B must decode");
    require(branch->mnemonic == myvmp::arm64::Mnemonic::BranchUncond, "B mnemonic mismatch");
    require(branch->condition == myvmp::ir::Condition::Al && branch->branch_offset == 2, "B operands mismatch");

    const auto tbz = decoder.decode(0x36280040u);
    require(tbz.has_value(), "TBZ must decode");
    require(tbz->mnemonic == myvmp::arm64::Mnemonic::BranchBit, "TBZ mnemonic mismatch");
    require(tbz->rn == 0 && tbz->shift_amount == 5 && tbz->imm == 1 && tbz->branch_offset == 2, "TBZ operands mismatch");

    const auto tbnz = decoder.decode(0xB7400041u);
    require(tbnz.has_value(), "TBNZ must decode");
    require(tbnz->mnemonic == myvmp::arm64::Mnemonic::BranchBit, "TBNZ mnemonic mismatch");
    require(tbnz->rn == 1 && tbnz->shift_amount == 40 && tbnz->imm == 0 && tbnz->branch_offset == 2, "TBNZ operands mismatch");

    const auto csel = decoder.decode(0x9A830040u);
    require(csel.has_value(), "CSEL must decode");
    require(csel->mnemonic == myvmp::arm64::Mnemonic::Select, "CSEL mnemonic mismatch");
    require(csel->rd == 0 && csel->rn == 2 && csel->rm == 3, "CSEL operands mismatch");

    const auto csinc = decoder.decode(0x9A850483u);
    require(csinc.has_value(), "CSINC must decode");
    require(csinc->mnemonic == myvmp::arm64::Mnemonic::SelectInc, "CSINC mnemonic mismatch");
    require(csinc->rd == 3 && csinc->rn == 4 && csinc->rm == 5 && csinc->condition == myvmp::ir::Condition::Eq, "CSINC operands mismatch");

    const auto csinv = decoder.decode(0xDA8810E6u);
    require(csinv.has_value(), "CSINV must decode");
    require(csinv->mnemonic == myvmp::arm64::Mnemonic::SelectInv, "CSINV mnemonic mismatch");
    require(csinv->rd == 6 && csinv->rn == 7 && csinv->rm == 8 && csinv->condition == myvmp::ir::Condition::Ne, "CSINV operands mismatch");

    const auto csneg = decoder.decode(0xDA8BC549u);
    require(csneg.has_value(), "CSNEG must decode");
    require(csneg->mnemonic == myvmp::arm64::Mnemonic::SelectNeg, "CSNEG mnemonic mismatch");
    require(csneg->rd == 9 && csneg->rn == 10 && csneg->rm == 11 && csneg->condition == myvmp::ir::Condition::Gt, "CSNEG operands mismatch");

    const auto cset = decoder.decode(0x9A9F17ECu);
    require(cset.has_value(), "CSET alias must decode");
    require(cset->mnemonic == myvmp::arm64::Mnemonic::SelectInc, "CSET alias mnemonic mismatch");
    require(cset->rd == 12 && cset->rn == 31 && cset->rm == 31 && cset->condition == myvmp::ir::Condition::Ne, "CSET alias operands mismatch");

    const auto csetm = decoder.decode(0xDA9F03EDu);
    require(csetm.has_value(), "CSETM alias must decode");
    require(csetm->mnemonic == myvmp::arm64::Mnemonic::SelectInv, "CSETM alias mnemonic mismatch");
    require(csetm->rd == 13 && csetm->rn == 31 && csetm->rm == 31 && csetm->condition == myvmp::ir::Condition::Eq, "CSETM alias operands mismatch");

    const auto sub_ext = decoder.decode(0xCB210042u);
    require(sub_ext.has_value(), "SUB extended register must decode");
    require(sub_ext->mnemonic == myvmp::arm64::Mnemonic::SubExtendedReg, "SUB extended mnemonic mismatch");
    require(sub_ext->rd == 2 && sub_ext->rn == 2 && sub_ext->rm == 1, "SUB extended operands mismatch");
    require(sub_ext->extend_kind == myvmp::ir::ExtendKind::Uxtb, "SUB extended kind mismatch");

    const auto subs_ext32 = decoder.decode(0x6B21401Fu);
    require(subs_ext32.has_value(), "32-bit SUBS extended register must decode");
    require(subs_ext32->mnemonic == myvmp::arm64::Mnemonic::SubExtendedRegSetFlags,
            "32-bit SUBS extended mnemonic mismatch");
    require(subs_ext32->rd == 31 && subs_ext32->rn == 0 && subs_ext32->rm == 1 && subs_ext32->writes_32bit,
            "32-bit SUBS extended operands mismatch");

    const auto sdiv = decoder.decode(0x9AC20C20u);
    require(sdiv.has_value() && sdiv->mnemonic == myvmp::arm64::Mnemonic::SignedDivide,
            "SDIV X-register form must decode");
    require(sdiv->rd == 0 && sdiv->rn == 1 && sdiv->rm == 2 && !sdiv->writes_32bit,
            "SDIV operands mismatch");

    const auto udiv32 = decoder.decode(0x1AC50883u);
    require(udiv32.has_value() && udiv32->mnemonic == myvmp::arm64::Mnemonic::UnsignedDivide,
            "UDIV W-register form must decode");
    require(udiv32->rd == 3 && udiv32->rn == 4 && udiv32->rm == 5 && udiv32->writes_32bit,
            "UDIV operands mismatch");

    const auto lslv = decoder.decode(0x9AC820E6u);
    require(lslv.has_value() && lslv->mnemonic == myvmp::arm64::Mnemonic::ShiftVariable,
            "LSLV must decode");
    require(lslv->rd == 6 && lslv->rn == 7 && lslv->rm == 8 && lslv->shift_kind == myvmp::ir::ShiftKind::Lsl,
            "LSLV operands mismatch");

    const auto eor_imm = decoder.decode(0xD2400020u);
    require(eor_imm.has_value() && eor_imm->mnemonic == myvmp::arm64::Mnemonic::EorImm,
            "EOR immediate must decode");
    require(eor_imm->rd == 0 && eor_imm->rn == 1 && eor_imm->imm == 1,
            "EOR immediate operands mismatch");

    const auto and_imm = decoder.decode(0x9240F821u);
    require(and_imm.has_value(), "AND immediate must decode");
    require(and_imm->mnemonic == myvmp::arm64::Mnemonic::AndImm, "AND immediate mnemonic mismatch");
    require(and_imm->rd == 1 && and_imm->rn == 1 && and_imm->imm == 0x7fffffffffffffffull, "AND immediate operands mismatch");

    const auto add_shift = decoder.decode(0x8B000420u);
    require(add_shift.has_value(), "ADD shifted register must decode");
    require(add_shift->mnemonic == myvmp::arm64::Mnemonic::AddShiftedReg, "ADD shifted mnemonic mismatch");
    require(add_shift->rd == 0 && add_shift->rn == 1 && add_shift->rm == 0, "ADD shifted operands mismatch");
    require(add_shift->shift_kind == myvmp::ir::ShiftKind::Lsl, "ADD shifted kind mismatch");
    require(add_shift->shift_amount == 1, "ADD shifted amount mismatch");

    const auto cmn_reg = decoder.decode(0xAB02003Fu);
    require(cmn_reg.has_value(), "CMN shifted register must decode");
    require(cmn_reg->mnemonic == myvmp::arm64::Mnemonic::AddShiftedRegSetFlags, "CMN shifted register mnemonic mismatch");
    require(cmn_reg->rd == 31 && cmn_reg->rn == 1 && cmn_reg->rm == 2, "CMN shifted register operands mismatch");

    const auto cmn_ext = decoder.decode(0xAB22483Fu);
    require(cmn_ext.has_value(), "CMN extended register must decode");
    require(cmn_ext->mnemonic == myvmp::arm64::Mnemonic::AddExtendedRegSetFlags, "CMN extended register mnemonic mismatch");
    require(cmn_ext->rd == 31 && cmn_ext->rn == 1 && cmn_ext->rm == 2 && cmn_ext->extend_kind == myvmp::ir::ExtendKind::Uxtw && cmn_ext->shift_amount == 2, "CMN extended register operands mismatch");

    const auto ldp = decoder.decode(0xA94107E0u);
    require(ldp.has_value(), "LDP 64-bit signed offset must decode");
    require(ldp->mnemonic == myvmp::arm64::Mnemonic::LoadPair, "LDP mnemonic mismatch");
    require(ldp->rm == 0 && ldp->rd == 1 && ldp->rn == 31 && ldp->imm == 16 && ldp->mem_width == 8, "LDP operands mismatch");

    const auto stp_pre = decoder.decode(0xA9BF07E0u);
    require(stp_pre.has_value(), "STP pre-index must decode");
    require(stp_pre->mnemonic == myvmp::arm64::Mnemonic::StorePairPreIndex, "STP pre-index mnemonic mismatch");
    require(stp_pre->rm == 0 && stp_pre->rd == 1 && stp_pre->rn == 31 && static_cast<std::int64_t>(stp_pre->imm) == -16 && stp_pre->mem_width == 8, "STP pre-index operands mismatch");

    const auto ldp_post = decoder.decode(0xA8C10FE2u);
    require(ldp_post.has_value(), "LDP post-index must decode");
    require(ldp_post->mnemonic == myvmp::arm64::Mnemonic::LoadPairPostIndex, "LDP post-index mnemonic mismatch");
    require(ldp_post->rm == 2 && ldp_post->rd == 3 && ldp_post->rn == 31 && ldp_post->imm == 16 && ldp_post->mem_width == 8, "LDP post-index operands mismatch");

    const auto ldp32 = decoder.decode(0x294107E0u);
    require(ldp32.has_value(), "LDP 32-bit signed offset must decode");
    require(ldp32->mnemonic == myvmp::arm64::Mnemonic::LoadPair, "LDP 32-bit mnemonic mismatch");
    require(ldp32->rm == 0 && ldp32->rd == 1 && ldp32->rn == 31 && ldp32->imm == 8 && ldp32->mem_width == 4, "LDP 32-bit operands mismatch");

    const auto ldpsw = decoder.decode(0x69421FE6u);
    require(ldpsw.has_value(), "LDPSW signed offset must decode");
    require(ldpsw->mnemonic == myvmp::arm64::Mnemonic::LoadSignedPair, "LDPSW mnemonic mismatch");
    require(ldpsw->rm == 6 && ldpsw->rd == 7 && ldpsw->rn == 31 && ldpsw->imm == 16 && ldpsw->mem_width == 4, "LDPSW operands mismatch");

    const auto ldrsb = decoder.decode(0x39800C20u);
    require(ldrsb.has_value(), "LDRSB unsigned offset must decode");
    require(ldrsb->mnemonic == myvmp::arm64::Mnemonic::LoadSigned, "LDRSB mnemonic mismatch");
    require(ldrsb->rd == 0 && ldrsb->rn == 1 && ldrsb->imm == 3 && ldrsb->mem_width == 1, "LDRSB operands mismatch");

    const auto ldrsh = decoder.decode(0x79800862u);
    require(ldrsh.has_value(), "LDRSH unsigned offset must decode");
    require(ldrsh->mnemonic == myvmp::arm64::Mnemonic::LoadSigned, "LDRSH mnemonic mismatch");
    require(ldrsh->rd == 2 && ldrsh->rn == 3 && ldrsh->imm == 4 && ldrsh->mem_width == 2, "LDRSH operands mismatch");

    const auto ldrsw = decoder.decode(0xB98008A4u);
    require(ldrsw.has_value(), "LDRSW unsigned offset must decode");
    require(ldrsw->mnemonic == myvmp::arm64::Mnemonic::LoadSigned, "LDRSW mnemonic mismatch");
    require(ldrsw->rd == 4 && ldrsw->rn == 5 && ldrsw->imm == 8 && ldrsw->mem_width == 4, "LDRSW operands mismatch");

    const auto ldur = decoder.decode(0xF85F83E0u);
    require(ldur.has_value(), "LDUR must decode");
    require(ldur->mnemonic == myvmp::arm64::Mnemonic::LoadUnsigned, "LDUR mnemonic mismatch");
    require(ldur->rd == 0 && ldur->rn == 31 && static_cast<std::int64_t>(ldur->imm) == -8 && ldur->mem_width == 8, "LDUR operands mismatch");

    const auto sturb = decoder.decode(0x381FF128u);
    require(sturb.has_value(), "STURB must decode");
    require(sturb->mnemonic == myvmp::arm64::Mnemonic::StoreUnsigned, "STURB mnemonic mismatch");
    require(sturb->rd == 8 && sturb->rn == 9 && static_cast<std::int64_t>(sturb->imm) == -1 && sturb->mem_width == 1, "STURB operands mismatch");

    const auto ldr_pre = decoder.decode(0xF8408FE0u);
    require(ldr_pre.has_value(), "LDR pre-index must decode");
    require(ldr_pre->mnemonic == myvmp::arm64::Mnemonic::LoadPreIndex, "LDR pre-index mnemonic mismatch");
    require(ldr_pre->rd == 0 && ldr_pre->rn == 31 && ldr_pre->imm == 8 && ldr_pre->mem_width == 8, "LDR pre-index operands mismatch");

    const auto str_post = decoder.decode(0xF81F07E3u);
    require(str_post.has_value(), "STR post-index must decode");
    require(str_post->mnemonic == myvmp::arm64::Mnemonic::StorePostIndex, "STR post-index mnemonic mismatch");
    require(str_post->rd == 3 && str_post->rn == 31 && static_cast<std::int64_t>(str_post->imm) == -16 && str_post->mem_width == 8, "STR post-index operands mismatch");

    const auto ldrsb_pre = decoder.decode(0x389FFC20u);
    require(ldrsb_pre.has_value(), "LDRSB pre-index must decode");
    require(ldrsb_pre->mnemonic == myvmp::arm64::Mnemonic::LoadSignedPreIndex, "LDRSB pre-index mnemonic mismatch");
    require(ldrsb_pre->rd == 0 && ldrsb_pre->rn == 1 && static_cast<std::int64_t>(ldrsb_pre->imm) == -1 && ldrsb_pre->mem_width == 1, "LDRSB pre-index operands mismatch");

    const auto ldrsw_post = decoder.decode(0xB880856Au);
    require(ldrsw_post.has_value(), "LDRSW post-index must decode");
    require(ldrsw_post->mnemonic == myvmp::arm64::Mnemonic::LoadSignedPostIndex, "LDRSW post-index mnemonic mismatch");
    require(ldrsw_post->rd == 10 && ldrsw_post->rn == 11 && ldrsw_post->imm == 8 && ldrsw_post->mem_width == 4, "LDRSW post-index operands mismatch");

    const auto ldr_reg_offset = decoder.decode(0xF8626820u);
    require(ldr_reg_offset.has_value(), "LDR register offset must decode");
    require(ldr_reg_offset->mnemonic == myvmp::arm64::Mnemonic::LoadRegOffset, "LDR register offset mnemonic mismatch");
    require(ldr_reg_offset->rd == 0 && ldr_reg_offset->rn == 1 && ldr_reg_offset->rm == 2 && ldr_reg_offset->mem_width == 8, "LDR register offset operands mismatch");

    const auto ldrsb_reg_offset = decoder.decode(0x38A26820u);
    require(ldrsb_reg_offset.has_value(), "LDRSB register offset must decode");
    require(ldrsb_reg_offset->mnemonic == myvmp::arm64::Mnemonic::LoadSignedRegOffset, "LDRSB register offset mnemonic mismatch");
    require(ldrsb_reg_offset->rd == 0 && ldrsb_reg_offset->rn == 1 && ldrsb_reg_offset->rm == 2 && ldrsb_reg_offset->mem_width == 1, "LDRSB register offset operands mismatch");

    const auto str_reg_offset = decoder.decode(0xF82B6949u);
    require(str_reg_offset.has_value(), "STR register offset must decode");
    require(str_reg_offset->mnemonic == myvmp::arm64::Mnemonic::StoreRegOffset, "STR register offset mnemonic mismatch");
    require(str_reg_offset->rd == 9 && str_reg_offset->rn == 10 && str_reg_offset->rm == 11 && str_reg_offset->mem_width == 8, "STR register offset operands mismatch");

    const auto unknown = decoder.decode(0xffffffffu);
    require(!unknown.has_value(), "unknown instruction must not decode");
}

void test_arm64_lifter_to_vm() {
    const std::vector<std::uint32_t> words = {
        0xD28000A0u,
        0x91001C00u,
        0xD1000800u,
        0xD65F03C0u,
    };
    const myvmp::arm64::Lifter lifter;
    const auto program = lifter.lift(words);

    const auto image = myvmp::bytecode::encode(program);
    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image);
    require(result.value == 10, "lifted ARM64 program result mismatch");
}

void test_arm64_lifter_divide_and_variable_shift_to_vm() {
    const std::vector<std::uint32_t> words = {
        0xD2800C80u, // mov x0, #100
        0xD2800081u, // mov x1, #4
        0x9AC10C00u, // sdiv x0, x0, x1
        0xD2800022u, // mov x2, #1
        0x9AC22000u, // lslv x0, x0, x2
        0xD65F03C0u, // ret
    };
    const myvmp::arm64::Lifter lifter;
    const auto image = myvmp::bytecode::encode(lifter.lift(words));
    myvmp::vm::Interpreter vm;
    require(vm.execute(image).value == 50, "lifted ARM64 divide/variable-shift result mismatch");
}

void test_arm64_lifter_zero_register_roles_to_vm() {
    const std::vector<std::uint32_t> words = {
        0xD28000A0u, // mov x0, #5
        0x8B0003E1u, // add x1, xzr, x0
        0x8B1F0022u, // add x2, x1, xzr
        0xAA0203E3u, // mov x3, x2
        0xCA1F0064u, // eor x4, x3, xzr
        0x8B00001Fu, // add xzr, x0, x0 (discard)
        0xAA0403E0u, // mov x0, x4
        0xD65F03C0u, // ret
    };
    const myvmp::arm64::Lifter lifter;
    const auto image = myvmp::bytecode::encode(lifter.lift(words));
    myvmp::vm::Interpreter vm;
    require(vm.execute(image).value == 5, "XZR source/destination role mapping mismatch");
}

void test_arm64_lifter_stack_pointer_extended_arithmetic_to_vm() {
    const std::vector<std::uint32_t> words = {
        0x8B2143FFu, // add sp, sp, w1, uxtw
        0x910003E0u, // mov x0, sp
        0xD65F03C0u, // ret
    };
    const myvmp::arm64::Lifter lifter;
    const auto image = myvmp::bytecode::encode(lifter.lift(words));
    std::array<std::uint64_t, 32> initial{};
    initial[1] = 7;
    initial[31] = 100;
    myvmp::vm::Interpreter vm;
    require(vm.execute(image, initial).value == 107, "SP role in extended ADD mismatch");
}

void test_arm64_lifter_zero_register_memory_roles_to_vm() {
    const std::vector<std::uint32_t> words = {
        0xF900003Fu, // str xzr, [x1]
        0xF940003Fu, // ldr xzr, [x1] (discard)
        0xF9400020u, // ldr x0, [x1]
        0xD65F03C0u, // ret
    };
    const myvmp::arm64::Lifter lifter;
    const auto image = myvmp::bytecode::encode(lifter.lift(words));
    std::array<std::uint64_t, 32> initial{};
    initial[1] = 8;
    std::vector<std::uint8_t> memory(32, 0xff);
    myvmp::vm::Interpreter vm;
    require(vm.execute(image, initial, memory).value == 0, "XZR load/store role mapping mismatch");
}

void test_arm64_lifter_adr_to_vm() {
    const std::vector<std::uint32_t> words = {
        0x10000040u, // adr x0, +8 bytes
        0xD65F03C0u, // ret
        0xD65F03C0u, // target: ret
    };
    const myvmp::arm64::Lifter lifter;
    const auto image = myvmp::bytecode::encode(lifter.lift(words));
    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image);
    require(result.value == 8, "lifted ADR ARM64 program result mismatch");
}

void test_arm64_lifter_pc_relative_with_region_va_to_vm() {
    const myvmp::arm64::Lifter lifter;
    myvmp::vm::Interpreter vm;

    const std::vector<std::uint32_t> adr_words = {
        0x10000040u, // adr x0, +8 bytes
        0xD65F03C0u,
        0xD65F03C0u,
    };
    auto image = myvmp::bytecode::encode(lifter.lift(adr_words, 0x400004u));
    require(vm.execute(image).value == 0x40000cu, "ADR must include the lifted region VA");

    const std::vector<std::uint32_t> adrp_words = {
        0xB0000020u, // adrp x0, +0x5000
        0xD65F03C0u,
    };
    image = myvmp::bytecode::encode(lifter.lift(adrp_words, 0x401234u));
    require(vm.execute(image).value == 0x406000u, "ADRP must use the native PC page");
}

void test_arm64_lifter_adrp_to_vm() {
    const std::vector<std::uint32_t> words = {
        0xB0000020u, // adrp x0, +0x5000
        0xD65F03C0u, // ret
    };
    const myvmp::arm64::Lifter lifter;
    const auto image = myvmp::bytecode::encode(lifter.lift(words));
    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image);
    require(result.value == 0x5000, "lifted ADRP ARM64 program result mismatch");
}

void test_arm64_lifter_ldr_literal_region_to_vm() {
    const std::vector<std::uint8_t> region = {
        0x40, 0x00, 0x00, 0x58, // ldr x0, +8
        0xc0, 0x03, 0x5f, 0xd6, // ret
        0x88, 0x77, 0x66, 0x55,
        0x44, 0x33, 0x22, 0x11,
    };
    const myvmp::arm64::Lifter lifter;
    const auto image = myvmp::bytecode::encode(lifter.lift(region, 2));
    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image);
    require(result.value == 0x1122334455667788ull, "lifted LDR literal ARM64 region result mismatch");

    bool rejected_word_only = false;
    try {
        (void)lifter.lift(std::vector<std::uint32_t>{0x58000040u, 0xD65F03C0u});
    } catch (const std::runtime_error&) {
        rejected_word_only = true;
    }
    require(rejected_word_only, "word-only LDR literal lift must require region bytes");
}

void test_arm64_lifter_ldr_literal_address_reader_to_vm() {
    const std::vector<std::uint32_t> words = {
        0x58000040u, // ldr x0, +8
        0xD65F03C0u, // ret
    };
    bool reader_called = false;
    const myvmp::arm64::LiftContext context{
        .region_va = 0x5000,
        .read_memory = [&reader_called](std::uint64_t address, std::uint8_t width)
            -> std::optional<std::uint64_t> {
            reader_called = true;
            if (address == 0x5008 && width == 8) {
                return 0x1122334455667788ull;
            }
            return std::nullopt;
        },
    };
    const myvmp::arm64::Lifter lifter;
    const auto image = myvmp::bytecode::encode(lifter.lift(words, context));
    myvmp::vm::Interpreter vm;
    require(reader_called, "LDR literal must use the address reader when provided");
    require(vm.execute(image).value == 0x1122334455667788ull,
            "LDR literal address-reader value mismatch");
}

void test_arm64_lifter_models_native_call_gates() {
    const myvmp::arm64::Lifter lifter;

    const auto direct = lifter.lift(std::vector<std::uint32_t>{0x94000002u, 0xD65F03C0u}, 0x1000);
    require(direct.instructions()[0].opcode == myvmp::ir::Opcode::CallNative,
            "external BL must lift to a native call gate");
    require(direct.instructions()[0].imm == 0x1008 &&
                static_cast<std::uint64_t>(direct.instructions()[0].post_index) == 0x1004,
            "native call target or link address mismatch");

    const auto internal = lifter.lift(
        std::vector<std::uint32_t>{0x94000001u, 0xD65F03C0u}, 0x1400);
    require(internal.instructions()[0].opcode == myvmp::ir::Opcode::CallVm &&
                internal.instructions()[0].target == 1,
            "in-region BL must lift to a VM call");

    const auto indirect_tail = lifter.lift(
        std::vector<std::uint32_t>{0xD61F0200u, 0xD65F03C0u}, 0x1800);
    require(indirect_tail.instructions()[0].opcode == myvmp::ir::Opcode::CallNativeReg &&
                indirect_tail.instructions()[0].src == 16 &&
                indirect_tail.instructions()[1].opcode == myvmp::ir::Opcode::ReturnReg,
            "BR must lift to an indirect native tail-call gate");

    const auto indirect = lifter.lift(
        std::vector<std::uint32_t>{0xD63F0280u, 0xD65F03C0u}, 0x2000);
    require(indirect.instructions()[0].opcode == myvmp::ir::Opcode::CallNativeReg &&
                indirect.instructions()[0].src == 20 &&
                static_cast<std::uint64_t>(indirect.instructions()[0].post_index) == 0x2004,
            "BLR must lift to an indirect native call gate");
}

void test_arm64_lifter_multiply_family_to_vm() {
    const std::vector<std::uint32_t> words = {
        0x9B020C20u, // madd x0, x1, x2, x3
        0x9B069CA4u, // msub x4, x5, x6, x7
        0x9B0A7D28u, // mul x8, x9, x10
        0x9B0DFD8Bu, // mneg x11, x12, x13
        0x1B1045EEu, // madd w14, w15, w16, w17
        0x1B147E72u, // mul w18, w19, w20
        0x9B377ED5u, // smull x21, w22, w23
        0x9BBA7F38u, // umull x24, w25, w26
        0xCA040000u, // eor x0, x0, x4
        0xCA080000u, // eor x0, x0, x8
        0xCA0B0000u, // eor x0, x0, x11
        0xCA0E0000u, // eor x0, x0, x14
        0xCA120000u, // eor x0, x0, x18
        0xCA150000u, // eor x0, x0, x21
        0xCA180000u, // eor x0, x0, x24
        0xD65F03C0u, // ret
    };
    std::array<std::uint64_t, 32> initial_registers{};
    initial_registers[1] = 2;
    initial_registers[2] = 3;
    initial_registers[3] = 4;
    initial_registers[5] = 5;
    initial_registers[6] = 6;
    initial_registers[7] = 100;
    initial_registers[9] = 7;
    initial_registers[10] = 8;
    initial_registers[12] = 9;
    initial_registers[13] = 10;
    initial_registers[15] = 0xffffffffu;
    initial_registers[16] = 3;
    initial_registers[17] = 5;
    initial_registers[19] = 0x10000u;
    initial_registers[20] = 0x10000u;
    initial_registers[22] = 0xffffffffu;
    initial_registers[23] = 2;
    initial_registers[25] = 0xffffffffu;
    initial_registers[26] = 2;

    const myvmp::arm64::Lifter lifter;
    const auto image = myvmp::bytecode::encode(lifter.lift(words));
    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image, initial_registers);
    const auto madd_value = 2ull * 3ull + 4ull;
    const auto msub_value = 100ull - 5ull * 6ull;
    const auto mul_value = 7ull * 8ull;
    const auto mneg_value = 0ull - 9ull * 10ull;
    const auto madd32_value = static_cast<std::uint64_t>(static_cast<std::uint32_t>(0xffffffffu * 3u + 5u));
    const auto mul32_value = static_cast<std::uint64_t>(static_cast<std::uint32_t>(0x10000u * 0x10000u));
    const auto smull_value = static_cast<std::uint64_t>(static_cast<std::int64_t>(-1) * 2);
    const auto umull_value = 0xffffffffull * 2ull;
    const auto expected = madd_value ^ msub_value ^ mul_value ^ mneg_value ^ madd32_value ^ mul32_value ^ smull_value ^ umull_value;
    require(result.value == expected, "lifted multiply-family ARM64 program result mismatch");
}

void test_arm64_lifter_atomic_barrier_subset_to_vm() {
    const std::vector<std::uint32_t> words = {
        0xC8DFFC20u, // ldar x0, [x1]
        0xC89FFC62u, // stlr x2, [x3]
        0xC85F7CA4u, // ldxr x4, [x5]
        0xC8067D07u, // stxr w6, x7, [x8]
        0xD5033BBFu, // dmb ish
        0xD5033F9Fu, // dsb sy
        0xD5033FDFu, // isb
        0xCA040000u, // eor x0, x0, x4
        0xCA060000u, // eor x0, x0, x6
        0xD65F03C0u, // ret
    };
    std::array<std::uint64_t, 32> initial_registers{};
    initial_registers[1] = 16;
    initial_registers[2] = 0x1122334455667788ull;
    initial_registers[3] = 32;
    initial_registers[5] = 40;
    initial_registers[7] = 0x8877665544332211ull;
    initial_registers[8] = 48;
    std::vector<std::uint8_t> memory(128);
    memory[16] = 0x11;
    memory[17] = 0x22;
    memory[18] = 0x33;
    memory[19] = 0x44;
    memory[20] = 0x55;
    memory[21] = 0x66;
    memory[22] = 0x77;
    memory[23] = 0x88;
    memory[40] = 0xaa;
    memory[41] = 0xbb;
    memory[42] = 0xcc;
    memory[43] = 0xdd;
    memory[44] = 0xee;
    memory[45] = 0xff;
    memory[46] = 0x00;
    memory[47] = 0x11;

    const myvmp::arm64::Lifter lifter;
    const auto image = myvmp::bytecode::encode(lifter.lift(words));
    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image, initial_registers, memory);
    require(result.value == (0x8877665544332211ull ^ 0x1100ffeeddccbbaaull), "lifted atomic/barrier ARM64 program result mismatch");
    require(memory[32] == 0x88 && memory[39] == 0x11, "STLR must store release value");
    require(memory[48] == 0x11 && memory[55] == 0x88, "STXR must store exclusive value");

    myvmp::ir::Program cas_program;
    cas_program.compare_swap(0, 1, 2, 8, true, true);
    cas_program.return_reg(0);
    std::array<std::uint64_t, 32> cas_registers{};
    cas_registers[0] = 0x1122334455667788ull;
    cas_registers[1] = 0x8877665544332211ull;
    cas_registers[2] = 64;
    for (std::size_t i = 0; i < 8; ++i) {
        memory[64 + i] = static_cast<std::uint8_t>(cas_registers[0] >> (i * 8u));
    }
    const auto cas_result = vm.execute(myvmp::bytecode::encode(cas_program), cas_registers, memory);
    require(cas_result.value == 0x1122334455667788ull, "CAS must return the observed value");
    require(memory[64] == 0x11 && memory[71] == 0x88, "successful CAS must store the desired value");
}

void test_arm64_lifter_xzr_aliases_to_vm() {
    const std::vector<std::uint32_t> words = {
        0xCB0103E0u, // neg x0, x1
        0xEB0303E2u, // negs x2, x3
        0xAA2503E4u, // mvn x4, x5
        0x4B0703E6u, // neg w6, w7
        0x6B0903E8u, // negs w8, w9
        0x2A2B03EAu, // mvn w10, w11
        0xCA020000u, // eor x0, x0, x2
        0xCA040000u, // eor x0, x0, x4
        0xCA060000u, // eor x0, x0, x6
        0xCA080000u, // eor x0, x0, x8
        0xCA0A0000u, // eor x0, x0, x10
        0xD65F03C0u, // ret
    };
    std::array<std::uint64_t, 32> initial_registers{};
    initial_registers[1] = 5;
    initial_registers[3] = 7;
    initial_registers[5] = 0x123456789abcdef0ull;
    initial_registers[7] = 9;
    initial_registers[9] = 11;
    initial_registers[11] = 0x12345678u;

    const myvmp::arm64::Lifter lifter;
    const auto image = myvmp::bytecode::encode(lifter.lift(words));
    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image, initial_registers);
    const auto expected = static_cast<std::uint64_t>(-5) ^
                          static_cast<std::uint64_t>(-7) ^
                          ~0x123456789abcdef0ull ^
                          static_cast<std::uint32_t>(0u - 9u) ^
                          static_cast<std::uint32_t>(0u - 11u) ^
                          static_cast<std::uint32_t>(~0x12345678u);
    require(result.value == expected, "lifted XZR alias ARM64 program result mismatch");
}

void test_arm64_lifter_32bit_immediate_flags_to_vm() {
    const std::vector<std::uint32_t> words = {
        0x71000020u, // subs w0, w1, #0
        0x54000065u, // b.pl +3
        0xD2800DE0u, // movz x0, #111
        0x14000002u, // b +2
        0xD2801BC0u, // movz x0, #222
        0xD65F03C0u, // ret
    };
    std::array<std::uint64_t, 32> initial_registers{};
    initial_registers[1] = 0x8000000000000001ull;

    const myvmp::arm64::Lifter lifter;
    const auto image = myvmp::bytecode::encode(lifter.lift(words));
    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image, initial_registers);
    require(result.value == 222, "lifted W-register immediate flags must use 32-bit NZCV semantics");
}

void test_arm64_lifter_32bit_shifted_flags_to_vm() {
    const std::vector<std::uint32_t> words = {
        0x2B020020u, // adds w0, w1, w2
        0x54000065u, // b.pl +3
        0xD2800DE0u, // movz x0, #111
        0x14000002u, // b +2
        0xD2801BC0u, // movz x0, #222
        0xD65F03C0u, // ret
    };
    std::array<std::uint64_t, 32> initial_registers{};
    initial_registers[1] = 0x8000000000000001ull;
    initial_registers[2] = 1;

    const myvmp::arm64::Lifter lifter;
    const auto image = myvmp::bytecode::encode(lifter.lift(words));
    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image, initial_registers);
    require(result.value == 222, "lifted W-register shifted flags must use 32-bit NZCV semantics");
}

void test_arm64_lifter_movn_asr_ror_to_vm() {
    const std::vector<std::uint32_t> words = {
        0x92801FE0u, // movn x0, #0xff
        0x9344FC00u, // asr x0, x0, #4
        0x93C02000u, // ror x0, x0, #8
        0xD65F03C0u, // ret
    };
    const myvmp::arm64::Lifter lifter;
    const auto program = lifter.lift(words);

    const auto image = myvmp::bytecode::encode(program);
    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image);
    require(result.value == 0xf0ffffffffffffffull, "lifted MOVN/ASR/ROR result mismatch");
}

void test_arm64_lifter_32bit_asr_ror_to_vm() {
    const std::vector<std::uint32_t> words = {
        0x13047C20u, // asr w0, w1, #4
        0x13832062u, // ror w2, w3, #8
        0xCA020000u, // eor x0, x0, x2
        0xD65F03C0u, // ret
    };
    const myvmp::arm64::Lifter lifter;
    const auto image = myvmp::bytecode::encode(lifter.lift(words));
    std::array<std::uint64_t, 32> initial_registers{};
    initial_registers[1] = 0xffffffff80000000ull;
    initial_registers[3] = 0xffffffff12345678ull;
    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image, initial_registers);
    require(result.value == (0xf8000000ull ^ 0x78123456ull), "lifted 32-bit ASR/ROR ARM64 program result mismatch");
}

void test_arm64_lifter_high_register_sequence_to_vm() {
    const std::vector<std::uint32_t> words = {
        0xD28000B1u, // mov x17, #5
        0x91000A20u, // add x0, x17, #2
        0xCA110000u, // eor x0, x0, x17
        0xD65F03C0u, // ret
    };
    const myvmp::arm64::Lifter lifter;
    const auto image = myvmp::bytecode::encode(lifter.lift(words));
    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image);
    require(result.value == 2, "lifted high-register ARM64 program result mismatch");
}

void test_arm64_lifter_unconditional_branch_to_vm() {
    const std::vector<std::uint32_t> words = {
        0xD2800020u, // mov x0, #1
        0x14000002u, // b +2 instructions
        0xD2800C60u, // mov x0, #99
        0x91001000u, // add x0, x0, #4
        0xD65F03C0u, // ret
    };
    const myvmp::arm64::Lifter lifter;
    const auto image = myvmp::bytecode::encode(lifter.lift(words));
    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image);
    require(result.value == 5, "lifted B ARM64 program result mismatch");
}

void test_arm64_lifter_lt_branch_to_vm() {
    const std::vector<std::uint32_t> words = {
        0xF100281Fu, // cmp x0, #10
        0x5400004Bu, // b.lt +2 instructions
        0xD2800C60u, // mov x0, #99
        0x91000400u, // add x0, x0, #1
        0xD65F03C0u, // ret
    };
    const myvmp::arm64::Lifter lifter;
    const auto image = myvmp::bytecode::encode(lifter.lift(words));

    myvmp::vm::Interpreter vm;
    std::array<std::uint64_t, 32> initial_registers{};
    initial_registers[0] = 5;
    require(vm.execute(image, initial_registers).value == 6, "lifted B.LT taken result mismatch");

    initial_registers[0] = 20;
    require(vm.execute(image, initial_registers).value == 100, "lifted B.LT fallthrough result mismatch");
}

void test_arm64_lifter_tst_shifted_branch_to_vm() {
    const std::vector<std::uint32_t> words = {
        0xD2800020u, // mov x0, #1
        0xD2800041u, // mov x1, #2
        0xEA01041Fu, // tst x0, x1, lsl #1
        0x54000040u, // b.eq +2 instructions
        0xD2800C60u, // mov x0, #99
        0x91001000u, // add x0, x0, #4
        0xD65F03C0u, // ret
    };
    const myvmp::arm64::Lifter lifter;
    const auto image = myvmp::bytecode::encode(lifter.lift(words));
    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image);
    require(result.value == 5, "lifted TST shifted branch ARM64 program result mismatch");
}

void test_arm64_lifter_32bit_ands_tst_shifted_branch_to_vm() {
    const std::vector<std::uint32_t> ands_words = {
        0x6A020420u, // ands w0, w1, w2, lsl #1
        0x54000044u, // b.mi +2 instructions
        0x528000E0u, // mov w0, #7
        0xD65F03C0u, // ret
    };
    const std::vector<std::uint32_t> tst_words = {
        0x52800540u, // mov w0, #42
        0x6A44087Fu, // tst w3, w4, lsr #2
        0x54000044u, // b.mi +2 instructions
        0x528000E0u, // mov w0, #7
        0xD65F03C0u, // ret
    };
    const myvmp::arm64::Lifter lifter;
    std::array<std::uint64_t, 32> initial_registers{};
    initial_registers[1] = 0xffffffff80000000ull;
    initial_registers[2] = 0xffffffffc0000000ull;
    initial_registers[3] = 0xffffffff80000000ull;
    initial_registers[4] = 0xffffffff00000000ull;
    myvmp::vm::Interpreter vm;
    const auto ands_image = myvmp::bytecode::encode(lifter.lift(ands_words));
    require(vm.execute(ands_image, initial_registers).value == 0x80000000ull, "lifted 32-bit ANDS shifted branch ARM64 program result mismatch");

    const auto tst_image = myvmp::bytecode::encode(lifter.lift(tst_words));
    require(vm.execute(tst_image, initial_registers).value == 7, "lifted 32-bit TST shifted branch ARM64 program result mismatch");
}

void test_arm64_lifter_test_bit_branch_to_vm() {
    const std::vector<std::uint32_t> words = {
        0xD2800400u, // mov x0, #32
        0x36280040u, // tbz w0, #5, +2 instructions
        0xD28000E0u, // mov x0, #7
        0x37100040u, // tbnz w0, #2, +2 instructions
        0xD2800C60u, // mov x0, #99
        0x91000400u, // add x0, x0, #1
        0xD65F03C0u, // ret
    };
    const myvmp::arm64::Lifter lifter;
    const auto image = myvmp::bytecode::encode(lifter.lift(words));
    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image);
    require(result.value == 8, "lifted TBZ/TBNZ ARM64 program result mismatch");
}

void test_arm64_lifter_add_sub_set_flags_to_vm() {
    const std::vector<std::uint32_t> words = {
        0x92800080u, // mov x0, #-5
        0xB100141Fu, // cmn x0, #5
        0x54000040u, // b.eq +2 instructions
        0xD2800C60u, // mov x0, #99
        0xB1001C01u, // adds x1, x0, #7
        0xF1000822u, // subs x2, x1, #2
        0xAB00005Fu, // cmn x2, x0
        0x54000040u, // b.eq +2 instructions
        0x91000440u, // add x0, x2, #1
        0xD65F03C0u, // ret
    };
    const myvmp::arm64::Lifter lifter;
    const auto image = myvmp::bytecode::encode(lifter.lift(words));
    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image);
    require(result.value == 1, "lifted ADDS/SUBS/CMN ARM64 program result mismatch");
}

void test_arm64_lifter_cmn_extended_branch_to_vm() {
    const std::vector<std::uint32_t> words = {
        0xAB22483Fu, // cmn x1, w2, uxtw #2
        0x54000040u, // b.eq +2 instructions
        0xD2800C60u, // mov x0, #99
        0xD65F03C0u, // ret
    };
    const myvmp::arm64::Lifter lifter;
    const auto image = myvmp::bytecode::encode(lifter.lift(words));
    std::array<std::uint64_t, 32> initial_registers{};
    initial_registers[0] = 5;
    initial_registers[1] = 0xfffffffffffffff8ull;
    initial_registers[2] = 2;
    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image, initial_registers);
    require(result.value == 5, "lifted CMN extended ARM64 program result mismatch");
}

void test_arm64_lifter_conditional_select_family_to_vm() {
    const std::vector<std::uint32_t> words = {
        0xF100001Fu, // cmp x0, #0
        0x9A850483u, // csinc x3, x4, x5, eq
        0xDA8810E6u, // csinv x6, x7, x8, ne
        0xDA8BC549u, // csneg x9, x10, x11, gt
        0xCA060060u, // eor x0, x3, x6
        0xCA090000u, // eor x0, x0, x9
        0xD65F03C0u, // ret
    };
    const myvmp::arm64::Lifter lifter;
    const auto image = myvmp::bytecode::encode(lifter.lift(words));
    std::array<std::uint64_t, 32> initial_registers{};
    initial_registers[0] = 0;
    initial_registers[4] = 10;
    initial_registers[5] = 20;
    initial_registers[7] = 30;
    initial_registers[8] = 0x1234;
    initial_registers[10] = 40;
    initial_registers[11] = 50;
    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image, initial_registers);
    const auto expected = 10ull ^ ~0x1234ull ^ static_cast<std::uint64_t>(-50);
    require(result.value == expected, "lifted CSINC/CSINV/CSNEG ARM64 program result mismatch");
}

void test_arm64_lifter_conditional_select_aliases_to_vm() {
    const std::vector<std::uint32_t> words = {
        0xF100001Fu, // cmp x0, #0
        0x9A9F17ECu, // cset x12, eq
        0xDA9F03EDu, // csetm x13, ne
        0x9A8FA5EEu, // cinc x14, x15, lt
        0xDA91B230u, // cinv x16, x17, ge
        0xDA93C672u, // cneg x18, x19, le
        0xCA0D0180u, // eor x0, x12, x13
        0xCA0E0000u, // eor x0, x0, x14
        0xCA100000u, // eor x0, x0, x16
        0xCA120000u, // eor x0, x0, x18
        0xD65F03C0u, // ret
    };
    const myvmp::arm64::Lifter lifter;
    const auto image = myvmp::bytecode::encode(lifter.lift(words));
    std::array<std::uint64_t, 32> initial_registers{};
    initial_registers[0] = 0;
    initial_registers[15] = 10;
    initial_registers[17] = 0x1234;
    initial_registers[19] = 20;
    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image, initial_registers);
    const auto expected = 1ull ^ 0ull ^ 10ull ^ ~0x1234ull ^ static_cast<std::uint64_t>(-20);
    require(result.value == expected, "lifted conditional select aliases ARM64 program result mismatch");
}

void test_arm64_lifter_conditional_select_preserves_x29_x30() {
    const std::vector<std::uint32_t> words = {
        0xF100001Fu, // cmp x0, #0
        0x9A9F17ECu, // cset x12, eq
        0xDA9F03EDu, // csetm x13, ne
        0xCA1E03A0u, // eor x0, x29, x30
        0xD65F03C0u, // ret
    };
    const myvmp::arm64::Lifter lifter;
    const auto image = myvmp::bytecode::encode(lifter.lift(words));
    std::array<std::uint64_t, 32> initial_registers{};
    initial_registers[0] = 0;
    initial_registers[29] = 0x1122334455667788ull;
    initial_registers[30] = 0x8877665544332211ull;
    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image, initial_registers);
    require(result.value == (initial_registers[29] ^ initial_registers[30]), "conditional select must not clobber x29/x30 temporaries");
}

void test_arm64_lifter_fixture_sequence_to_vm() {
    const std::vector<std::uint32_t> words = {
        0x91001401u, // add x1, x0, #5
        0xD28006A2u, // mov x2, #0x35
        0xCA020021u, // eor x1, x1, x2
        0x8B000420u, // add x0, x1, x0, lsl #1
        0xD65F03C0u, // ret
    };
    const myvmp::arm64::Lifter lifter;
    auto lifted = lifter.lift(words);
    myvmp::ir::Program program;
    for (const auto& inst : lifted.instructions()) {
        switch (inst.opcode) {
        case myvmp::ir::Opcode::LoadImm:
            program.load_imm(inst.dst, inst.imm);
            break;
        case myvmp::ir::Opcode::LoadAddress:
            program.load_address(inst.dst, inst.imm);
            break;
        case myvmp::ir::Opcode::AddImm:
            program.add_imm(inst.dst, inst.src, inst.imm);
            break;
        case myvmp::ir::Opcode::SubImm:
            program.sub_imm(inst.dst, inst.src, inst.imm);
            break;
        case myvmp::ir::Opcode::AddRegShifted:
            program.add_reg_shifted(inst.dst, inst.src, inst.rhs, inst.shift_kind, inst.shift_amount);
            break;
        case myvmp::ir::Opcode::AddRegExtended:
            program.add_reg_extended(inst.dst, inst.src, inst.rhs, inst.extend_kind, inst.shift_amount);
            break;
        case myvmp::ir::Opcode::AddRegExtendedSetFlags:
            program.add_reg_extended_set_flags(inst.dst, inst.src, inst.rhs, inst.extend_kind, inst.shift_amount,
                                               inst.mem_width == 4);
            break;
        case myvmp::ir::Opcode::SubRegShifted:
            program.sub_reg_shifted(inst.dst, inst.src, inst.rhs, inst.shift_kind, inst.shift_amount);
            break;
        case myvmp::ir::Opcode::SubRegExtended:
            program.sub_reg_extended(inst.dst, inst.src, inst.rhs, inst.extend_kind, inst.shift_amount);
            break;
        case myvmp::ir::Opcode::SubRegExtendedSetFlags:
            program.sub_reg_extended_set_flags(inst.dst, inst.src, inst.rhs, inst.extend_kind, inst.shift_amount,
                                               inst.mem_width == 4);
            break;
        case myvmp::ir::Opcode::ShiftRegVariable:
            program.shift_reg_variable(inst.dst, inst.src, inst.rhs, inst.shift_kind, inst.mem_width == 4);
            break;
        case myvmp::ir::Opcode::DivideReg:
            program.divide_reg(inst.dst, inst.src, inst.rhs, inst.imm != 0, inst.mem_width == 4);
            break;
        case myvmp::ir::Opcode::NegReg:
            program.neg_reg(inst.dst, inst.src, inst.shift_kind, inst.shift_amount, inst.mem_width == 4);
            break;
        case myvmp::ir::Opcode::NegRegSetFlags:
            program.neg_reg_set_flags(inst.dst, inst.src, inst.shift_kind, inst.shift_amount, inst.mem_width == 4);
            break;
        case myvmp::ir::Opcode::AndImm:
            program.and_imm(inst.dst, inst.src, inst.imm);
            break;
        case myvmp::ir::Opcode::OrImm:
            program.or_imm(inst.dst, inst.src, inst.imm);
            break;
        case myvmp::ir::Opcode::XorImm:
            program.xor_imm(inst.dst, inst.src, inst.imm);
            break;
        case myvmp::ir::Opcode::AddImmSetFlags:
            program.add_imm_set_flags(inst.dst, inst.src, inst.imm, inst.mem_width == 4);
            break;
        case myvmp::ir::Opcode::SubImmSetFlags:
            program.sub_imm_set_flags(inst.dst, inst.src, inst.imm, inst.mem_width == 4);
            break;
        case myvmp::ir::Opcode::AndRegShifted:
            program.and_reg_shifted(inst.dst, inst.src, inst.rhs, inst.shift_kind, inst.shift_amount);
            break;
        case myvmp::ir::Opcode::AddRegShiftedSetFlags:
            program.add_reg_shifted_set_flags(inst.dst, inst.src, inst.rhs, inst.shift_kind, inst.shift_amount, inst.mem_width == 4);
            break;
        case myvmp::ir::Opcode::SubRegShiftedSetFlags:
            program.sub_reg_shifted_set_flags(inst.dst, inst.src, inst.rhs, inst.shift_kind, inst.shift_amount, inst.mem_width == 4);
            break;
        case myvmp::ir::Opcode::BitClearRegShifted:
            program.bit_clear_reg_shifted(inst.dst, inst.src, inst.rhs, inst.shift_kind, inst.shift_amount);
            break;
        case myvmp::ir::Opcode::NotRegShifted:
            program.not_reg_shifted(inst.dst, inst.src, inst.shift_kind, inst.shift_amount, inst.mem_width == 4);
            break;
        case myvmp::ir::Opcode::OrRegShifted:
            program.or_reg_shifted(inst.dst, inst.src, inst.rhs, inst.shift_kind, inst.shift_amount);
            break;
        case myvmp::ir::Opcode::OrNotRegShifted:
            program.or_not_reg_shifted(inst.dst, inst.src, inst.rhs, inst.shift_kind, inst.shift_amount);
            break;
        case myvmp::ir::Opcode::XorRegShifted:
            program.xor_reg_shifted(inst.dst, inst.src, inst.rhs, inst.shift_kind, inst.shift_amount);
            break;
        case myvmp::ir::Opcode::XorNotRegShifted:
            program.xor_not_reg_shifted(inst.dst, inst.src, inst.rhs, inst.shift_kind, inst.shift_amount);
            break;
        case myvmp::ir::Opcode::MoveKeep:
            program.move_keep(inst.dst, static_cast<std::uint16_t>(inst.imm), inst.shift_amount);
            break;
        case myvmp::ir::Opcode::ShiftReg:
            program.shift_reg(inst.dst, inst.src, inst.shift_kind, inst.shift_amount);
            break;
        case myvmp::ir::Opcode::ShiftReg32:
            program.shift_reg32(inst.dst, inst.src, inst.shift_kind, inst.shift_amount);
            break;
        case myvmp::ir::Opcode::BitfieldExtract:
            program.bitfield_extract(inst.dst, inst.src, inst.shift_amount, static_cast<std::uint8_t>(inst.imm));
            break;
        case myvmp::ir::Opcode::UnsignedMulHigh:
            program.unsigned_mul_high(inst.dst, inst.src, inst.rhs);
            break;
        case myvmp::ir::Opcode::MulAdd:
            program.mul_add(inst.dst, inst.src, inst.rhs, inst.shift_amount, inst.mem_width == 4, (inst.imm & myvmp::ir::kMulAccumulatorZero) != 0);
            break;
        case myvmp::ir::Opcode::MulSub:
            program.mul_sub(inst.dst, inst.src, inst.rhs, inst.shift_amount, inst.mem_width == 4, (inst.imm & myvmp::ir::kMulAccumulatorZero) != 0);
            break;
        case myvmp::ir::Opcode::SignedMulLong:
            program.signed_mul_long(inst.dst, inst.src, inst.rhs);
            break;
        case myvmp::ir::Opcode::UnsignedMulLong:
            program.unsigned_mul_long(inst.dst, inst.src, inst.rhs);
            break;
        case myvmp::ir::Opcode::MoveReg:
            program.move_reg(inst.dst, inst.src);
            break;
        case myvmp::ir::Opcode::CompareImm:
            program.compare_imm(inst.src, inst.imm);
            break;
        case myvmp::ir::Opcode::CompareReg:
            program.compare_reg(inst.src, inst.rhs);
            break;
        case myvmp::ir::Opcode::TestImm:
            program.test_imm(inst.src, inst.imm);
            break;
        case myvmp::ir::Opcode::TestRegShifted:
            program.test_reg_shifted(inst.src, inst.rhs, inst.shift_kind, inst.shift_amount);
            break;
        case myvmp::ir::Opcode::TestRegShifted32:
            program.test_reg_shifted32(inst.src, inst.rhs, inst.shift_kind, inst.shift_amount);
            break;
        case myvmp::ir::Opcode::BranchIf:
            program.branch_if(inst.condition, inst.target + 1);
            break;
        case myvmp::ir::Opcode::BranchRegZero:
            program.branch_reg_zero(inst.src, inst.imm != 0, inst.target + 1);
            break;
        case myvmp::ir::Opcode::BranchRegBit:
            program.branch_reg_bit(inst.src, inst.shift_amount, inst.imm != 0, inst.target + 1);
            break;
        case myvmp::ir::Opcode::Select:
            program.select(inst.dst, inst.src, inst.rhs, inst.condition, (inst.imm & myvmp::ir::kSelectTrueZero) != 0, (inst.imm & myvmp::ir::kSelectFalseZero) != 0);
            break;
        case myvmp::ir::Opcode::SelectInc:
            program.select_inc(inst.dst, inst.src, inst.rhs, inst.condition, (inst.imm & myvmp::ir::kSelectTrueZero) != 0, (inst.imm & myvmp::ir::kSelectFalseZero) != 0);
            break;
        case myvmp::ir::Opcode::SelectInv:
            program.select_inv(inst.dst, inst.src, inst.rhs, inst.condition, (inst.imm & myvmp::ir::kSelectTrueZero) != 0, (inst.imm & myvmp::ir::kSelectFalseZero) != 0);
            break;
        case myvmp::ir::Opcode::SelectNeg:
            program.select_neg(inst.dst, inst.src, inst.rhs, inst.condition, (inst.imm & myvmp::ir::kSelectTrueZero) != 0, (inst.imm & myvmp::ir::kSelectFalseZero) != 0);
            break;
        case myvmp::ir::Opcode::Load64:
        case myvmp::ir::Opcode::Store64:
        case myvmp::ir::Opcode::StorePair64:
        case myvmp::ir::Opcode::LoadMem:
        case myvmp::ir::Opcode::StoreMem:
        case myvmp::ir::Opcode::LoadAcquire:
        case myvmp::ir::Opcode::StoreRelease:
        case myvmp::ir::Opcode::LoadExclusive:
        case myvmp::ir::Opcode::StoreExclusive:
        case myvmp::ir::Opcode::LoadExclusivePair:
        case myvmp::ir::Opcode::StoreExclusivePair:
        case myvmp::ir::Opcode::CompareSwap:
        case myvmp::ir::Opcode::LoadRegOffsetMem:
        case myvmp::ir::Opcode::StoreRegOffsetMem:
        case myvmp::ir::Opcode::CallNative:
        case myvmp::ir::Opcode::CallNativeReg:
        case myvmp::ir::Opcode::CallVm:
        case myvmp::ir::Opcode::LoadFpImm64:
        case myvmp::ir::Opcode::FpAdd64:
        case myvmp::ir::Opcode::FpSub64:
        case myvmp::ir::Opcode::FpMul64:
        case myvmp::ir::Opcode::FpDiv64:
        case myvmp::ir::Opcode::FpMadd64:
        case myvmp::ir::Opcode::FpCompare64:
        case myvmp::ir::Opcode::LoadFpImm32:
        case myvmp::ir::Opcode::FpAdd32:
        case myvmp::ir::Opcode::FpSub32:
        case myvmp::ir::Opcode::FpMul32:
        case myvmp::ir::Opcode::FpDiv32:
        case myvmp::ir::Opcode::FpMadd32:
        case myvmp::ir::Opcode::FpCompare32:
        case myvmp::ir::Opcode::FpUnary:
        case myvmp::ir::Opcode::FpConvert:
        case myvmp::ir::Opcode::FpMinMax:
        case myvmp::ir::Opcode::FpConditionalCompare:
        case myvmp::ir::Opcode::FpSelect:
            throw std::runtime_error("unexpected memory IR in arithmetic fixture test");
        case myvmp::ir::Opcode::NoOp:
            program.no_op();
            break;
        case myvmp::ir::Opcode::ReturnReg:
            program.return_reg(inst.src);
            break;
        }
    }

    const auto image = myvmp::bytecode::encode(program);
    std::array<std::uint64_t, 32> initial_registers{};
    initial_registers[0] = 5;
    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image, initial_registers);
    require(result.value == 73, "lifted fixture ARM64 program result mismatch");
}

void test_arm64_lifter_branch_sequence_to_vm() {
    const std::vector<std::uint32_t> words = {
        0xAA0003E5u,
        0xD2800001u,
        0xD2800000u,
        0xD37FF8A7u,
        0x8B0100A4u,
        0x8B0100E2u,
        0xCB010483u,
        0xCA050026u,
        0x8B020002u,
        0xCB030003u,
        0x8B060000u,
        0xF240049Fu,
        0x54000060u,
        0xF240003Fu,
        0x9A830040u,
        0x91000421u,
        0xF100443Fu,
        0x54FFFE61u,
        0xD65F03C0u,
    };

    const myvmp::arm64::Lifter lifter;
    const auto program = lifter.lift(words);
    const auto image = myvmp::bytecode::encode(program);
    std::array<std::uint64_t, 32> initial_registers{};
    initial_registers[0] = 23;
    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image, initial_registers);
    require(result.value == 526, "lifted branch ARM64 program result mismatch");
}

void test_arm64_lifter_integer_sequence_to_vm() {
    const std::vector<std::uint32_t> words = {
        0xD2824682u,
        0x8B020002u,
        0xCB210042u,
        0xD28AB543u,
        0xCA010C41u,
        0xF2AAB543u,
        0x9240F821u,
        0xF2CAB543u,
        0xF2EAB543u,
        0xAA030021u,
        0x8B400820u,
        0xCA001C00u,
        0xD65F03C0u,
    };

    const myvmp::arm64::Lifter lifter;
    const auto program = lifter.lift(words);
    const auto image = myvmp::bytecode::encode(program);
    std::array<std::uint64_t, 32> initial_registers{};
    initial_registers[0] = 0x123456789ull;
    initial_registers[1] = 0xdeadbeefull;
    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image, initial_registers);
    require(result.value == 9259556552717691296ull, "lifted integer ARM64 program result mismatch");
}

void test_arm64_lifter_memory_sequence_to_vm() {
    const std::vector<std::uint32_t> words = {
        0xD10083FFu,
        0xAA0003E1u,
        0x910003E4u,
        0x91003407u,
        0xAA0403E3u,
        0x91006806u,
        0x91009C05u,
        0xD2800002u,
        0xA9009887u,
        0xCA010041u,
        0x91004042u,
        0xF9000C85u,
        0xD2800000u,
        0xF8008461u,
        0x8B010000u,
        0xF101005Fu,
        0x54000100u,
        0xF9400061u,
        0xCA010041u,
        0x91004042u,
        0xF8008461u,
        0x8B010000u,
        0xF101005Fu,
        0x54FFFF41u,
        0x910083FFu,
        0xD65F03C0u,
    };

    const myvmp::arm64::Lifter lifter;
    const auto program = lifter.lift(words);
    const auto image = myvmp::bytecode::encode(program);
    std::array<std::uint64_t, 32> initial_registers{};
    initial_registers[0] = 0x3456u;
    initial_registers[31] = 128;
    std::vector<std::uint8_t> memory(512);
    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image, initial_registers, memory);
    require(result.value == 53606, "lifted memory ARM64 program result mismatch");
}

void test_arm64_lifter_load_pair_sequence_to_vm() {
    const std::vector<std::uint32_t> words = {
        0xA94107E0u, // ldp x0, x1, [sp, #16]
        0xCA010000u, // eor x0, x0, x1
        0xD65F03C0u, // ret
    };
    const myvmp::arm64::Lifter lifter;
    const auto image = myvmp::bytecode::encode(lifter.lift(words));
    std::array<std::uint64_t, 32> initial_registers{};
    initial_registers[31] = 128;
    std::vector<std::uint8_t> memory(512);
    write_u64_le(memory, 144, 0x1111222233334444ull);
    write_u64_le(memory, 152, 0x5555666677778888ull);

    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image, initial_registers, memory);
    require(result.value == (0x1111222233334444ull ^ 0x5555666677778888ull), "lifted LDP ARM64 program result mismatch");
}

void test_arm64_lifter_pair_writeback_sequence_to_vm() {
    const std::vector<std::uint32_t> words = {
        0xD2800220u, // mov x0, #0x11
        0xD2800441u, // mov x1, #0x22
        0xA9BF07E0u, // stp x0, x1, [sp, #-16]!
        0xA8C10FE2u, // ldp x2, x3, [sp], #16
        0xCA030040u, // eor x0, x2, x3
        0xD65F03C0u, // ret
    };
    const myvmp::arm64::Lifter lifter;
    const auto image = myvmp::bytecode::encode(lifter.lift(words));
    std::array<std::uint64_t, 32> initial_registers{};
    initial_registers[31] = 128;
    std::vector<std::uint8_t> memory(512);
    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image, initial_registers, memory);
    require(result.value == 0x33, "lifted pair writeback ARM64 program result mismatch");
}

void test_arm64_lifter_32bit_pair_and_ldpsw_sequence_to_vm() {
    const std::vector<std::uint32_t> words = {
        0x294107E0u, // ldp w0, w1, [sp, #8]
        0x29BF0FE2u, // stp w2, w3, [sp, #-8]!
        0x28C117E4u, // ldp w4, w5, [sp], #8
        0x69421FE6u, // ldpsw x6, x7, [sp, #16]
        0xCA010000u, // eor x0, x0, x1
        0xCA040000u, // eor x0, x0, x4
        0xCA050000u, // eor x0, x0, x5
        0xCA060000u, // eor x0, x0, x6
        0xCA070000u, // eor x0, x0, x7
        0xD65F03C0u, // ret
    };
    const myvmp::arm64::Lifter lifter;
    const auto image = myvmp::bytecode::encode(lifter.lift(words));
    std::array<std::uint64_t, 32> initial_registers{};
    initial_registers[2] = 0xaabbccddu;
    initial_registers[3] = 0x11223344u;
    initial_registers[31] = 128;
    std::vector<std::uint8_t> memory(512);
    write_u32_le(memory, 136, 0x12345678u);
    write_u32_le(memory, 140, 0x90abcdefu);
    write_u32_le(memory, 144, 0x80000001u);
    write_u32_le(memory, 148, 0x0000007fu);

    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image, initial_registers, memory);
    const auto expected = 0x12345678ull ^ 0x90abcdefull ^ 0xaabbccddull ^ 0x11223344ull ^ 0xffffffff80000001ull ^ 0x7full;
    require(result.value == expected, "lifted 32-bit pair/LDPSW ARM64 program result mismatch");
}

void test_arm64_lifter_signed_load_sequence_to_vm() {
    const std::vector<std::uint32_t> words = {
        0x39800C20u, // ldrsb x0, [x1, #3]
        0x79800822u, // ldrsh x2, [x1, #4]
        0xB9800823u, // ldrsw x3, [x1, #8]
        0xCA020000u, // eor x0, x0, x2
        0xCA030000u, // eor x0, x0, x3
        0xD65F03C0u, // ret
    };
    const myvmp::arm64::Lifter lifter;
    const auto image = myvmp::bytecode::encode(lifter.lift(words));
    std::array<std::uint64_t, 32> initial_registers{};
    initial_registers[1] = 128;
    std::vector<std::uint8_t> memory(512);
    memory[131] = 0x80;
    memory[132] = 0x00;
    memory[133] = 0xff;
    memory[136] = 0x01;
    memory[137] = 0x00;
    memory[138] = 0x00;
    memory[139] = 0x80;

    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image, initial_registers, memory);
    require(result.value == 0xffffffff80000081ull, "lifted signed-load ARM64 program result mismatch");
}

void test_arm64_lifter_sign_extend_sequence_to_vm() {
    const std::vector<std::uint32_t> words = {
        0xD2801001u, // mov x1, #0x80
        0x93401C20u, // sxtb x0, w1
        0x93403C22u, // sxth x2, w1
        0x93407C23u, // sxtw x3, w1
        0xCA020000u, // eor x0, x0, x2
        0xCA030000u, // eor x0, x0, x3
        0xD65F03C0u, // ret
    };
    const myvmp::arm64::Lifter lifter;
    const auto image = myvmp::bytecode::encode(lifter.lift(words));
    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image);
    require(result.value == 0xffffffffffffff80ull, "lifted sign-extend ARM64 program result mismatch");
}

void test_arm64_lifter_unscaled_memory_sequence_to_vm() {
    const std::vector<std::uint32_t> words = {
        0xF85F83E0u, // ldur x0, [sp, #-8]
        0xF81F03E0u, // stur x0, [sp, #-16]
        0xF85F03E1u, // ldur x1, [sp, #-16]
        0x385FF3E2u, // ldurb w2, [sp, #-1]
        0x381FE3E2u, // sturb w2, [sp, #-2]
        0x385FE3E3u, // ldurb w3, [sp, #-2]
        0xCA030020u, // eor x0, x1, x3
        0xD65F03C0u, // ret
    };
    const myvmp::arm64::Lifter lifter;
    const auto image = myvmp::bytecode::encode(lifter.lift(words));
    std::array<std::uint64_t, 32> initial_registers{};
    initial_registers[31] = 128;
    std::vector<std::uint8_t> memory(512);
    write_u64_le(memory, 120, 0x123456789abcdef0ull);

    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image, initial_registers, memory);
    require(result.value == (0x123456789abcdef0ull ^ 0x12ull), "lifted unscaled memory ARM64 program result mismatch");
}

void test_arm64_lifter_writeback_memory_sequence_to_vm() {
    const std::vector<std::uint32_t> words = {
        0xF8408FE0u, // ldr x0, [sp, #8]!
        0xF8408FE1u, // ldr x1, [sp, #8]!
        0xF81F87E0u, // str x0, [sp], #-8
        0xF84083E2u, // ldur x2, [sp, #8]
        0xCA020000u, // eor x0, x0, x2
        0xD65F03C0u, // ret
    };
    const myvmp::arm64::Lifter lifter;
    const auto image = myvmp::bytecode::encode(lifter.lift(words));
    std::array<std::uint64_t, 32> initial_registers{};
    initial_registers[31] = 128;
    std::vector<std::uint8_t> memory(512);
    write_u64_le(memory, 136, 0x1111222233334444ull);
    write_u64_le(memory, 144, 0x5555666677778888ull);

    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image, initial_registers, memory);
    require(result.value == 0, "lifted writeback memory ARM64 program result mismatch");
}

void test_arm64_lifter_register_offset_memory_sequence_to_vm() {
    const std::vector<std::uint32_t> words = {
        0xF8626820u, // ldr x0, [x1, x2]
        0xF8627823u, // ldr x3, [x1, x2, lsl #3]
        0xF8247823u, // str x3, [x1, x4, lsl #3]
        0x38666825u, // ldrb w5, [x1, x6]
        0x38276825u, // strb w5, [x1, x7]
        0x38676820u, // ldrb w0, [x1, x7]
        0xCA030000u, // eor x0, x0, x3
        0xD65F03C0u, // ret
    };
    const myvmp::arm64::Lifter lifter;
    const auto image = myvmp::bytecode::encode(lifter.lift(words));
    std::array<std::uint64_t, 32> initial_registers{};
    initial_registers[1] = 128;
    initial_registers[2] = 1;
    initial_registers[4] = 3;
    initial_registers[6] = 2;
    initial_registers[7] = 4;
    std::vector<std::uint8_t> memory(512);
    write_u64_le(memory, 129, 0x1111222233334444ull);
    write_u64_le(memory, 136, 0x5555666677778888ull);
    memory[130] = 0xab;

    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image, initial_registers, memory);
    require(result.value == (0x5555666677778888ull ^ 0xabull), "lifted register-offset memory ARM64 program result mismatch");
}

void test_arm64_lifter_signed_register_offset_memory_sequence_to_vm() {
    const std::vector<std::uint32_t> words = {
        0x38A26820u, // ldrsb x0, [x1, x2]
        0x78A47823u, // ldrsh x3, [x1, x4, lsl #1]
        0xB8A65825u, // ldrsw x5, [x1, w6, uxtw #2]
        0xCA030000u, // eor x0, x0, x3
        0xCA050000u, // eor x0, x0, x5
        0xD65F03C0u, // ret
    };
    const myvmp::arm64::Lifter lifter;
    const auto image = myvmp::bytecode::encode(lifter.lift(words));
    std::array<std::uint64_t, 32> initial_registers{};
    initial_registers[1] = 128;
    initial_registers[2] = 3;
    initial_registers[4] = 2;
    initial_registers[6] = 2;
    std::vector<std::uint8_t> memory(512);
    memory[131] = 0x80;
    memory[132] = 0x00;
    memory[133] = 0xff;
    memory[136] = 0x01;
    memory[137] = 0x00;
    memory[138] = 0x00;
    memory[139] = 0x80;

    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image, initial_registers, memory);
    require(result.value == 0xffffffff80000081ull, "lifted signed register-offset memory ARM64 program result mismatch");
}

void test_arm64_lifter_signed_writeback_memory_sequence_to_vm() {
    const std::vector<std::uint32_t> words = {
        0x38802422u, // ldrsb x2, [x1], #2
        0x789FECA3u, // ldrsh x3, [x5, #-2]!
        0xB8808524u, // ldrsw x4, [x9], #8
        0xCA030040u, // eor x0, x2, x3
        0xCA040000u, // eor x0, x0, x4
        0xCA090000u, // eor x0, x0, x9
        0xD65F03C0u, // ret
    };
    const myvmp::arm64::Lifter lifter;
    const auto image = myvmp::bytecode::encode(lifter.lift(words));
    std::array<std::uint64_t, 32> initial_registers{};
    initial_registers[1] = 128;
    initial_registers[5] = 134;
    initial_registers[9] = 136;
    std::vector<std::uint8_t> memory(512);
    memory[128] = 0x80;
    memory[132] = 0x00;
    memory[133] = 0xff;
    memory[136] = 0x01;
    memory[137] = 0x00;
    memory[138] = 0x00;
    memory[139] = 0x80;

    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image, initial_registers, memory);
    const auto expected = 0xffffffffffffff80ull ^ 0xffffffffffffff00ull ^ 0xffffffff80000001ull ^ 144ull;
    require(result.value == expected, "lifted signed writeback memory ARM64 program result mismatch");
}

void test_arm64_lifter_muldiv_sequence_to_vm() {
    const std::vector<std::uint32_t> words = {
        0xD2849263u, 0xB201F3E2u, 0xF2B24923u, 0xF2955562u, 0xF2C92483u,
        0xD37EF424u, 0xF2E49243u, 0x9BC27C21u, 0x8B001005u, 0x9BC37C03u,
        0x8B4104A1u, 0xCB030002u, 0xCA401420u, 0x8B420462u, 0xCB420882u,
        0x8B000040u, 0xD65F03C0u,
    };
    const myvmp::arm64::Lifter lifter;
    const auto image = myvmp::bytecode::encode(lifter.lift(words));
    std::array<std::uint64_t, 32> initial_registers{};
    initial_registers[0] = 0x123456ull;
    initial_registers[1] = 0xabcdefull;
    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image, initial_registers);
    require(result.value == 68865522, "lifted muldiv ARM64 program result mismatch");
}

void test_arm64_lifter_branch_zero_sequence_to_vm() {
    const std::vector<std::uint32_t> words = {
        0xAA0003E1u, 0xB4000140u, 0xD2800000u, 0x92400822u, 0xD341FC21u,
        0x8B020000u, 0xB5FFFFA1u, 0xF100001Fu, 0xD2800C61u, 0x9A811000u,
        0xD65F03C0u, 0xD2800C60u, 0xD65F03C0u,
    };
    const myvmp::arm64::Lifter lifter;
    const auto image = myvmp::bytecode::encode(lifter.lift(words));
    std::array<std::uint64_t, 32> initial_registers{};
    initial_registers[0] = 0x1234u;
    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image, initial_registers);
    require(result.value == 35, "lifted cbz/cbnz ARM64 program result mismatch");
}

void test_arm64_lifter_narrow_sequence_to_vm() {
    const std::vector<std::uint32_t> words = {
        0xD10043FFu, 0x12001C01u, 0x390003E1u, 0x53083C01u, 0x390007E1u,
        0x53105C01u, 0x39000BE1u, 0x53187C00u, 0x39000FE0u, 0x394003E0u,
        0x394007E1u, 0x12001C21u, 0x0B200020u, 0x790013E0u, 0x39400BE0u,
        0x39400FE1u, 0x12001C21u, 0x0B200020u, 0x790017E0u, 0x794013E1u,
        0x794017E0u, 0x910043FFu, 0x53103C00u, 0x8B212000u, 0xD65F03C0u,
    };
    const myvmp::arm64::Lifter lifter;
    const auto image = myvmp::bytecode::encode(lifter.lift(words));
    std::array<std::uint64_t, 32> initial_registers{};
    initial_registers[0] = 0x12345678u;
    initial_registers[31] = 128;
    std::vector<std::uint8_t> memory(512);
    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image, initial_registers, memory);
    require(result.value == 4587726, "lifted narrow ARM64 program result mismatch");
}

void test_arm64_lifter_pair_sequence_to_vm() {
    const std::vector<std::uint32_t> words = {
        0xD10043FFu, 0x91002C00u, 0x91004421u, 0xF90003E0u, 0xF90007E1u,
        0xF94003E1u, 0xF94007E0u, 0x910043FFu, 0xCA000020u, 0xD65F03C0u,
    };
    const myvmp::arm64::Lifter lifter;
    const auto image = myvmp::bytecode::encode(lifter.lift(words));
    std::array<std::uint64_t, 32> initial_registers{};
    initial_registers[0] = 0x3456u;
    initial_registers[1] = 0x789au;
    initial_registers[31] = 128;
    std::vector<std::uint8_t> memory(512);
    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image, initial_registers, memory);
    require(result.value == 19658, "lifted pair-memory ARM64 program result mismatch");
}

void test_arm64_lifter_bitlogic_sequence_to_vm() {
    const std::vector<std::uint32_t> words = {
        0x8A210002u, 0xAA200021u, 0xCA010041u, 0xCA210000u, 0x8B810C00u, 0xD65F03C0u,
    };
    const myvmp::arm64::Lifter lifter;
    const auto image = myvmp::bytecode::encode(lifter.lift(words));
    std::array<std::uint64_t, 32> initial_registers{};
    initial_registers[0] = 0x1234567890ull;
    initial_registers[1] = 0xabcdef0123ull;
    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image, initial_registers);
    require(result.value == 78187493519ull, "lifted bitlogic ARM64 program result mismatch");
}

void test_arm64_lifter_condition_sequence_to_vm() {
    const std::vector<std::uint32_t> words = {
        0xEB01001Fu, 0xD2800062u, 0x9A82A3E2u, 0xF100001Fu, 0x91001440u,
        0x9A82A002u, 0xF101903Fu, 0x91001C40u, 0x9A82C000u, 0xD65F03C0u,
    };
    const myvmp::arm64::Lifter lifter;
    const auto image = myvmp::bytecode::encode(lifter.lift(words));
    std::array<std::uint64_t, 32> initial_registers{};
    initial_registers[0] = 42;
    initial_registers[1] = 123;
    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image, initial_registers);
    require(result.value == 15, "lifted condition ARM64 program result mismatch");
}

void test_aarch64_branch_encoder() {
    const auto forward = myvmp::patch::encode_aarch64_b(0x1000, 0x1010);
    require(forward.has_value(), "forward AArch64 branch must encode");
    require(*forward == 0x14000004u, "forward AArch64 branch encoding mismatch");

    const auto backward = myvmp::patch::encode_aarch64_b(0x1010, 0x1000);
    require(backward.has_value(), "backward AArch64 branch must encode");
    require(*backward == 0x17fffffcu, "backward AArch64 branch encoding mismatch");

    const auto unaligned = myvmp::patch::encode_aarch64_b(0x1000, 0x1002);
    require(!unaligned.has_value(), "unaligned AArch64 branch must fail");
    require(!myvmp::patch::encode_aarch64_b(std::numeric_limits<std::uint64_t>::max(), 0),
            "AArch64 branch must reject an unrepresentable high-address delta");

    const auto absolute = myvmp::patch::encode_aarch64_absolute_jump(0x123456789abcdef0ull);
    require(absolute[0] == 0x50 && absolute[1] == 0x00 && absolute[2] == 0x00 && absolute[3] == 0x58,
            "AArch64 absolute jump LDR encoding mismatch");
    require(absolute[4] == 0x00 && absolute[5] == 0x02 && absolute[6] == 0x1f && absolute[7] == 0xd6,
            "AArch64 absolute jump BR encoding mismatch");
    std::uint64_t literal = 0;
    for (unsigned index = 0; index < 8; ++index) literal |= static_cast<std::uint64_t>(absolute[8 + index]) << (index * 8u);
    require(literal == 0x123456789abcdef0ull, "AArch64 absolute jump literal mismatch");
}

void test_arm32_branch_encoder() {
    require(myvmp::patch::encode_arm32_b(0x1000, 0x1010) == 0xea000002u,
            "forward ARM32 branch encoding mismatch");
    require(myvmp::patch::encode_arm32_b(0x1010, 0x1000) == 0xeafffffau,
            "backward ARM32 branch encoding mismatch");
    require(!myvmp::patch::encode_arm32_b(0x1000, 0x1002),
            "unaligned ARM32 branch must fail");
    require(!myvmp::patch::encode_arm32_b(std::numeric_limits<std::uint64_t>::max() - 3u, 0),
            "ARM32 branch PC bias must not overflow at the address-space boundary");
}

void test_arm32_decoder_and_lifter_to_vm() {
    const myvmp::arm32::Decoder decoder;
    const auto add = decoder.decode(0xe2800005u);
    require(add.has_value() && add->mnemonic == myvmp::arm32::Mnemonic::AddImm &&
                add->rd == 0 && add->rn == 0 && add->imm == 5,
            "ARM32 ADD immediate decoding mismatch");
    const auto eor = decoder.decode(0xe0200001u);
    require(eor.has_value() && eor->mnemonic == myvmp::arm32::Mnemonic::EorReg && eor->rm == 1,
            "ARM32 EOR register decoding mismatch");
    const auto conditional_sub = decoder.decode(0x82400003u);
    require(conditional_sub.has_value() && conditional_sub->condition == myvmp::ir::Condition::Hi,
            "ARM32 conditional SUB decoding mismatch");

    const std::vector<std::uint32_t> words{
        0xe2800005u, // add r0, r0, #5
        0xe0200001u, // eor r0, r0, r1
        0xe3500014u, // cmp r0, #20
        0x82400003u, // subhi r0, r0, #3
        0xe12fff1eu, // bx lr
    };
    const auto image = myvmp::bytecode::encode(myvmp::arm32::Lifter{}.lift(words));
    std::array<std::uint64_t, 32> registers{};
    registers[0] = 20;
    registers[1] = 1;
    myvmp::vm::Interpreter vm;
    require(vm.execute(image, registers).value == 21, "lifted ARM32 conditional program mismatch");
    registers[0] = 10;
    require(vm.execute(image, registers).value == 14, "lifted ARM32 false predicate mismatch");

    registers[0] = 0xffffffffu;
    registers[1] = 0;
    require(vm.execute(image, registers).value == 4, "ARM32 arithmetic must wrap at 32 bits");

    const std::vector<std::uint32_t> memory_words{
        0xe5910004u, // ldr r0, [r1, #4]
        0xe2800005u, // add r0, r0, #5
        0xe5810008u, // str r0, [r1, #8]
        0xe12fff1eu, // bx lr
    };
    const auto memory_image = myvmp::bytecode::encode(myvmp::arm32::Lifter{}.lift(memory_words));
    registers.fill(0);
    registers[1] = 32;
    std::vector<std::uint8_t> memory(64);
    memory[36] = 7;
    require(vm.execute(memory_image, registers, memory).value == 12,
            "lifted ARM32 memory program result mismatch");
    require(memory[40] == 12 && memory[41] == 0 && memory[42] == 0 && memory[43] == 0,
            "lifted ARM32 store result mismatch");
}

void test_arm32_runtime_payload_builder() {
    const std::vector<std::uint32_t> words{0xe2800005u, 0xe0200001u, 0xe12fff1eu};
    const auto profile = myvmp::bytecode::profile_from_seed(0x32u);
    const auto image = myvmp::bytecode::encode(myvmp::arm32::Lifter{}.lift(words),
                                               {.key = 0x12345678u, .profile = profile});
    const auto payload = myvmp::runtime::build_arm32_runtime_payload(image);
    require(payload.size() > myvmp::runtime::arm32_runtime_code_size() + 8,
            "ARM32 runtime payload must contain header and encrypted bytecode");
    const auto fixed_image = myvmp::bytecode::encode(myvmp::arm32::Lifter{}.lift(words));
    require(payload != myvmp::runtime::build_arm32_runtime_payload(fixed_image),
            "ARM32 runtime payload must incorporate dynamic opcode profiles");
}

void test_elf_parser_on_self(const char* self_path) {
    const auto elf = myvmp::elf::ElfFile::parse(self_path);
    require(!elf.segments().empty(), "ELF parser must find program headers");
    require(!elf.sections().empty(), "ELF parser must find section headers");
    require(!elf.symbols().empty(), "ELF parser must find symbols");
    require(elf.read_va(elf.entry(), 4).has_value(), "ELF parser must read file-backed entry bytes by VA");
}

void test_aarch64_relocation_classification() {
    require(myvmp::protect::classify_aarch64_relocation(R_AARCH64_CALL26) ==
                myvmp::protect::Aarch64RelocationClass::InstructionPcRelative,
            "AArch64 CALL26 relocation classification mismatch");
    require(myvmp::protect::classify_aarch64_relocation(R_AARCH64_ADD_ABS_LO12_NC) ==
                myvmp::protect::Aarch64RelocationClass::InstructionAbsolute,
            "AArch64 low-12 instruction relocation classification mismatch");
    require(myvmp::protect::classify_aarch64_relocation(R_AARCH64_RELATIVE) ==
                myvmp::protect::Aarch64RelocationClass::Dynamic,
            "AArch64 dynamic relocation classification mismatch");
    require(myvmp::protect::classify_aarch64_relocation(R_AARCH64_ADR_GOT_PAGE) ==
                myvmp::protect::Aarch64RelocationClass::InstructionGot,
            "AArch64 GOT instruction relocation classification mismatch");
    require(myvmp::protect::classify_aarch64_relocation(R_AARCH64_TLSDESC_CALL) ==
                myvmp::protect::Aarch64RelocationClass::InstructionTls,
            "AArch64 TLS instruction relocation classification mismatch");
    require(myvmp::protect::classify_aarch64_relocation(0xffffffffu) ==
                myvmp::protect::Aarch64RelocationClass::Unknown,
            "unknown AArch64 relocation must fail closed");
}

void test_aarch64_conditional_fp() {
    const myvmp::arm64::Decoder decoder;
    const auto fccmp = decoder.decode(0x1e220420u); // fccmp s1, s2, #0, eq
    require(fccmp && fccmp->mnemonic == myvmp::arm64::Mnemonic::FpConditionalCompare &&
                fccmp->rn == 1 && fccmp->rm == 2 && fccmp->mem_width == 4,
            "FCCMP decoder mismatch");
    const auto fccmpe = decoder.decode(0x1e64c47fu); // fccmpe d3, d4, #15, gt
    require(fccmpe && fccmpe->mnemonic == myvmp::arm64::Mnemonic::FpConditionalCompare &&
                fccmpe->imm == 0x1f && fccmpe->condition == myvmp::ir::Condition::Gt,
            "FCCMPE decoder mismatch");
    const auto fcsel = decoder.decode(0x1e6add28u); // fcsel d8, d9, d10, le
    require(fcsel && fcsel->mnemonic == myvmp::arm64::Mnemonic::FpSelect &&
                fcsel->rd == 8 && fcsel->rn == 9 && fcsel->rm == 10 &&
                fcsel->condition == myvmp::ir::Condition::Le && fcsel->mem_width == 8,
            "FCSEL decoder mismatch");
    const auto fcsel_nv = decoder.decode(0x1e22fc20u); // fcsel s0, s1, s2, nv
    require(fcsel_nv && fcsel_nv->mnemonic == myvmp::arm64::Mnemonic::FpSelect &&
                fcsel_nv->condition == myvmp::ir::Condition::Nv && fcsel_nv->mem_width == 4,
            "FCSEL condition 15 decoder mismatch");

    myvmp::ir::Program program;
    program.load_fp_imm64(1, std::bit_cast<std::uint64_t>(2.0));
    program.load_fp_imm64(2, std::bit_cast<std::uint64_t>(3.0));
    program.fp_compare64(1, 1); // EQ is true, therefore NE below is false.
    program.fp_conditional_compare(1, 2, myvmp::ir::Condition::Ne, 0x2, true);
    program.fp_select(3, 1, 2, myvmp::ir::Condition::Hs, true);
    program.fp_convert(0, 3, myvmp::ir::FpConvertKind::Fp64ToSigned64);
    program.return_reg(0);
    const auto profile = myvmp::bytecode::profile_from_seed(0x46u);
    const auto image = myvmp::bytecode::encode(program, {.profile = profile});
    myvmp::vm::Interpreter vm;
    require(vm.execute(image).value == 2,
            "conditional FP fallback NZCV/select semantics mismatch");

    myvmp::ir::Program nv_program;
    nv_program.load_fp_imm32(1, std::bit_cast<std::uint32_t>(1.0f));
    nv_program.load_fp_imm32(2, std::bit_cast<std::uint32_t>(2.0f));
    nv_program.fp_select(0, 1, 2, myvmp::ir::Condition::Nv, false);
    nv_program.fp_convert(0, 0, myvmp::ir::FpConvertKind::Fp32ToUnsigned64);
    nv_program.return_reg(0);
    require(vm.execute(myvmp::bytecode::encode(nv_program, {.profile = profile})).value == 1,
            "condition encoding 15 must execute unconditionally in the host VM");
}

void test_aarch64_exclusive_pairs() {
    const myvmp::arm64::Decoder decoder;
    const auto ldxp32=decoder.decode(0x887f0861u);
    require(ldxp32 && ldxp32->mnemonic==myvmp::arm64::Mnemonic::LoadExclusivePair &&
                ldxp32->rd==1 && ldxp32->rm==2 && ldxp32->rn==3 && ldxp32->mem_width==4,
            "32-bit LDXP decoder mismatch");
    const auto ldaxp64=decoder.decode(0xc87f94c4u);
    require(ldaxp64 && ldaxp64->mnemonic==myvmp::arm64::Mnemonic::LoadExclusivePair &&
                ldaxp64->imm==1 && ldaxp64->mem_width==8,
            "64-bit LDAXP decoder mismatch");
    const auto stxp32=decoder.decode(0x88272548u);
    require(stxp32 && stxp32->mnemonic==myvmp::arm64::Mnemonic::StoreExclusivePair &&
                stxp32->rd==7 && stxp32->rm==8 && stxp32->shift_amount==9 && stxp32->rn==10,
            "32-bit STXP decoder mismatch");
    const auto stlxp64=decoder.decode(0xc82bb5ccu);
    require(stlxp64 && stlxp64->mnemonic==myvmp::arm64::Mnemonic::StoreExclusivePair &&
                stlxp64->imm==1 && stlxp64->mem_width==8,
            "64-bit STLXP decoder mismatch");

    myvmp::ir::Program program;
    program.load_exclusive_pair(1,2,0,8,true);
    program.add_imm(1,1,1);
    program.add_imm(2,2,2);
    program.store_exclusive_pair(3,1,2,0,8,true);
    program.return_reg(3);
    std::vector<std::uint8_t> memory(16);
    for (unsigned i=0;i<8;++i) {
        memory[i]=static_cast<std::uint8_t>(std::uint64_t{0x10} >> (i*8u));
        memory[8+i]=static_cast<std::uint8_t>(std::uint64_t{0x20} >> (i*8u));
    }
    std::array<std::uint64_t,32> registers{};
    const auto profile=myvmp::bytecode::profile_from_seed(0x47u);
    myvmp::vm::Interpreter vm;
    require(vm.execute(myvmp::bytecode::encode(program,{.profile=profile}),registers,memory).value==0,
            "exclusive-pair host store must succeed in the sequential model");
    require(memory[0]==0x11 && memory[8]==0x22,
            "exclusive-pair host load/store semantics mismatch");
}

void test_sibling_vm_partition_and_bridge_contract() {
    myvmp::ir::Program program;
    program.load_imm(0, 1);
    program.compare_imm(0, 1);
    program.branch_if(myvmp::ir::Condition::Eq, 4);
    program.load_imm(0, 2);
    program.return_reg(0);
    const auto image = myvmp::bytecode::encode(program, {
        .profile = myvmp::bytecode::profile_from_seed(0x51u),
        .shuffle_blocks = true,
    });
    const auto partition = myvmp::vm::plan_sibling_partition(image, 101, 202, 0x52u);
    require(!partition.primary_blocks.empty() && !partition.secondary_blocks.empty(),
            "sibling VM planner must keep entry in primary and assign another block to secondary");
    require(partition.primary_blocks.front() == image.blocks.front().id,
            "sibling VM entry block must remain in the primary profile");
    require(!partition.bridges.empty(), "cross-profile CFG edges must produce explicit VM bridges");
    bool saw_primary_to_secondary = false;
    bool saw_secondary_to_primary = false;
    for (const auto& bridge : partition.bridges) {
        bridge.validate();
        saw_primary_to_secondary |= bridge.source_profile_id == 101 &&
                                    bridge.destination_profile_id == 202;
        saw_secondary_to_primary |= bridge.source_profile_id == 202 &&
                                    bridge.destination_profile_id == 101;
    }
    require(saw_primary_to_secondary && saw_secondary_to_primary,
            "sibling VM regression must cover bounded A-to-B-to-A continuation");

    auto invalid = partition.bridges.front();
    invalid.depth = invalid.maximum_depth;
    bool rejected = false;
    try { invalid.validate(); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "VM bridge must reject transitions beyond its bounded depth");

    const auto sibling = myvmp::vm::encode_sibling(program, {
        .primary_seed = 0x5101u,
        .secondary_seed = 0x5102u,
        .partition_seed = 0x5103u,
        .key = 0x51045105u,
    });
    require(sibling.primary_profile.manifest() != sibling.secondary_profile.manifest(),
            "sibling VM profiles must be independently seeded");
    require(myvmp::vm::Interpreter{}.execute(sibling).value == 1,
            "host sibling VM execution changed program semantics");
    const auto normalized = myvmp::vm::normalize_sibling_image(
        sibling, myvmp::bytecode::fixed_profile());
    require(normalized.blocks.size() == sibling.image.blocks.size() &&
                normalized.bytes.size() == sibling.image.bytes.size(),
            "sibling VM normalization must preserve explicit CFG metadata");
    const auto native_payload = myvmp::runtime::build_aarch64_runtime_payload(sibling);
    require(native_payload.size() > myvmp::runtime::aarch64_runtime_code_size() +
                                      sibling.image.bytes.size() + 512,
            "native sibling VM payload must carry both opcode translation domains");
    require(myvmp::bytecode::read_u32_le(
                native_payload, myvmp::runtime::aarch64_runtime_code_size() + 28) == 2,
            "native sibling VM payload must enable multi-profile dispatch");
    const auto metadata = myvmp::runtime::aarch64_runtime_code_size() + 32;
    const auto first_encrypted_owner = myvmp::bytecode::read_u32_le(
        native_payload, metadata + sibling.image.blocks.size() * 4);
    require(first_encrypted_owner != 0 && first_encrypted_owner != 1,
            "sibling VM block ownership must not be stored in plaintext");
    const auto maps = metadata + sibling.image.blocks.size() * 8 + 8;
    const auto primary_load = sibling.primary_profile.physical(myvmp::bytecode::Opcode::LoadImm64);
    require(native_payload[maps + primary_load] !=
                myvmp::bytecode::fixed_profile().physical(myvmp::bytecode::Opcode::LoadImm64),
            "sibling VM opcode translation table must be encrypted at rest");

    auto invalid_owner = sibling;
    invalid_owner.block_profile_ids.front() = 0xffffffffu;
    rejected = false;
    try { invalid_owner.validate(); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "sibling VM validation must reject unknown block owners");

    myvmp::ir::Program single_block;
    single_block.load_imm(0, 1);
    single_block.return_reg(0);
    rejected = false;
    try {
        (void)myvmp::vm::encode_sibling(single_block, {
            .primary_seed = 1, .secondary_seed = 2, .partition_seed = 3,
        });
    } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "sibling VM mode must fail closed when no cross-profile partition exists");
}

void test_riscv64_decoder_lifter_vertical_slice() {
    const myvmp::riscv64::Decoder decoder;
    const auto addi = decoder.decode(0x00500513u); // addi a0, zero, 5
    require(addi && addi->mnemonic == myvmp::riscv64::Mnemonic::AddImm &&
                addi->rd == 10 && addi->rs1 == 0 && addi->immediate == 5,
            "RV64 ADDI decoder mismatch");
    const auto srai = decoder.decode(0x40355513u); // srai a0, a0, 3
    require(srai && srai->mnemonic == myvmp::riscv64::Mnemonic::ShiftRightImm &&
                srai->shift == myvmp::ir::ShiftKind::Asr && srai->immediate == 3,
            "RV64 SRAI decoder mismatch");
    const auto ld = decoder.decode(0x00853583u); // ld a1, 8(a0)
    require(ld && ld->mnemonic == myvmp::riscv64::Mnemonic::Load && ld->width == 8 &&
                ld->is_signed && ld->immediate == 8,
            "RV64 LD decoder mismatch");

    const auto auipc = decoder.decode(0x00001517u); // auipc a0, 1
    require(auipc && auipc->mnemonic == myvmp::riscv64::Mnemonic::Auipc &&
                auipc->immediate == 4096,
            "RV64 AUIPC decoder mismatch");
    const auto lbu = decoder.decode(0x00054583u); // lbu a1, 0(a0)
    require(lbu && lbu->mnemonic == myvmp::riscv64::Mnemonic::Load &&
                lbu->width == 1 && !lbu->is_signed,
            "RV64 LBU decoder mismatch");
    const auto mul = decoder.decode(0x02b50533u); // mul a0, a0, a1
    require(mul && mul->mnemonic == myvmp::riscv64::Mnemonic::Mul,
            "RV64 MUL decoder mismatch");

    const std::vector<std::uint32_t> words{
        0x00500513u, // addi a0, zero, 5
        0x00700593u, // addi a1, zero, 7
        0x00b50533u, // add a0, a0, a1
        0x00008067u, // ret
    };
    const auto program = myvmp::riscv64::Lifter{}.lift(words);
    const auto image = myvmp::bytecode::encode(program, {
        .profile = myvmp::bytecode::profile_from_seed(0x6401u),
        .diversify_immediates = true,
        .diversification_seed = 0x6402u,
    });
    require(myvmp::vm::Interpreter{}.execute(image).value == 12,
            "lifted RV64 vertical slice changed semantics");

    const std::vector<std::uint32_t> word_and_m_words{
        0xfff00513u, // addi a0, zero, -1
        0x00100593u, // addi a1, zero, 1
        0x00b5053bu, // addw a0, a0, a1 => 0
        0xfff00593u, // addi a1, zero, -1
        0x02b50533u, // mul a0, a0, a1 => 0
        0x02054533u, // div a0, a0, zero => -1 (architectural rule)
        0x00008067u,
    };
    const auto word_and_m = myvmp::bytecode::encode(myvmp::riscv64::Lifter{}.lift(word_and_m_words));
    require(myvmp::vm::Interpreter{}.execute(word_and_m).value == ~std::uint64_t{0},
            "lifted RV64 W/M edge semantics mismatch");

    const auto encode_b = [](std::int32_t offset, std::uint8_t rs1,
                             std::uint8_t rs2, std::uint8_t funct3) {
        const auto imm = static_cast<std::uint32_t>(offset) & 0x1fffu;
        return ((imm >> 12u) & 1u) << 31u |
               ((imm >> 5u) & 0x3fu) << 25u |
               static_cast<std::uint32_t>(rs2) << 20u |
               static_cast<std::uint32_t>(rs1) << 15u |
               static_cast<std::uint32_t>(funct3) << 12u |
               ((imm >> 1u) & 0xfu) << 8u |
               ((imm >> 11u) & 1u) << 7u | 0x63u;
    };
    const std::vector<std::uint32_t> branch_words{
        0x00102293u,                 // slti t0, zero, 1 (expands into multiple IR ops)
        0x00000513u,                 // addi a0, zero, 0
        0x00300593u,                 // addi a1, zero, 3
        0x00150513u,                 // addi a0, a0, 1
        encode_b(-4, 10, 11, 4),     // blt a0, a1, previous instruction
        0x00008067u,
    };
    const auto branch_image = myvmp::bytecode::encode(myvmp::riscv64::Lifter{}.lift(branch_words));
    require(myvmp::vm::Interpreter{}.execute(branch_image).value == 3,
            "RV64 expanded-IR branch target remapping mismatch");
    require(!decoder.decode(0x00000001u), "RV64 frontend must reject compressed instructions for now");

    const auto jal = myvmp::patch::encode_riscv64_jal(0x1000, 0x1800, 5);
    require(jal && (*jal & 0x7fu) == 0x6fu && ((*jal >> 7u) & 31u) == 5,
            "RV64 runtime detour JAL encoding mismatch");
    require(!myvmp::patch::encode_riscv64_jal(0x1000, 0x101001),
            "RV64 JAL must reject odd or out-of-range targets");
    const auto far_jump = myvmp::patch::encode_riscv64_far_jump(0x1000, 0x201800, 5);
    require(far_jump && ((*far_jump)[0] & 0x7fu) == 0x17u &&
            ((*far_jump)[1] & 0x7fu) == 0x67u,
            "RV64 far runtime detour must use AUIPC/JALR");
    require(!myvmp::patch::encode_riscv64_far_jump(0x1000, 0x100000001000ull, 5),
            "RV64 far detour must reject targets outside signed 32-bit PC-relative range");
    require(!myvmp::patch::encode_riscv64_far_jump(
                std::numeric_limits<std::uint64_t>::max(), 0, 5),
            "RV64 far detour must reject an unrepresentable high-address delta");
    const auto arm_page_jump = myvmp::patch::encode_aarch64_page_jump(0x1000, 0x20012340, 16);
    require(arm_page_jump && ((*arm_page_jump)[0] & 0x9f000000u) == 0x90000000u &&
            ((*arm_page_jump)[1] & 0xffc00000u) == 0x91000000u &&
            (*arm_page_jump)[2] == 0xd61f0200u,
            "AArch64 far PIE detour must use ADRP/ADD/BR");
    require(!myvmp::patch::encode_aarch64_page_jump(0x1000, 0x100000001000ull, 16),
            "AArch64 page detour must reject targets outside ADRP range");
}

} // namespace

int main(int argc, char** argv) {
    try {
        test_ir_validation();
        test_vm_executes_encoded_program();
        test_vm_executes_register_ops();
        test_vm_executes_logical_shift_and_movk();
        test_vm_executes_branch_and_select();
        test_diversified_immediates_preserve_branches_and_hide_literal();
        test_vm_executes_immediate_and_extended_sub();
        test_vm_executes_32bit_extended_flag_arithmetic();
        test_vm_executes_internal_call_stack();
        test_vm_executes_divide_and_variable_shift();
        test_aarch64_runtime_payload_builder();
        test_dynamic_profiles_change_bytecode_not_semantics();
        test_vm_profile_demo_seed_round_trip();
        test_arm64_decoder();
        test_arm64_lifter_to_vm();
        test_arm64_lifter_divide_and_variable_shift_to_vm();
        test_arm64_lifter_zero_register_roles_to_vm();
        test_arm64_lifter_stack_pointer_extended_arithmetic_to_vm();
        test_arm64_lifter_zero_register_memory_roles_to_vm();
        test_arm64_lifter_adr_to_vm();
        test_arm64_lifter_pc_relative_with_region_va_to_vm();
        test_arm64_lifter_adrp_to_vm();
        test_arm64_lifter_ldr_literal_region_to_vm();
        test_arm64_lifter_ldr_literal_address_reader_to_vm();
        test_arm64_lifter_models_native_call_gates();
        test_arm64_lifter_multiply_family_to_vm();
        test_arm64_lifter_atomic_barrier_subset_to_vm();
        test_arm64_lifter_xzr_aliases_to_vm();
        test_arm64_lifter_32bit_immediate_flags_to_vm();
        test_arm64_lifter_32bit_shifted_flags_to_vm();
        test_arm64_lifter_movn_asr_ror_to_vm();
        test_arm64_lifter_32bit_asr_ror_to_vm();
        test_arm64_lifter_high_register_sequence_to_vm();
        test_arm64_lifter_unconditional_branch_to_vm();
        test_arm64_lifter_lt_branch_to_vm();
        test_arm64_lifter_tst_shifted_branch_to_vm();
        test_arm64_lifter_32bit_ands_tst_shifted_branch_to_vm();
        test_arm64_lifter_test_bit_branch_to_vm();
        test_arm64_lifter_add_sub_set_flags_to_vm();
        test_arm64_lifter_cmn_extended_branch_to_vm();
        test_arm64_lifter_conditional_select_family_to_vm();
        test_arm64_lifter_conditional_select_aliases_to_vm();
        test_arm64_lifter_conditional_select_preserves_x29_x30();
        test_arm64_lifter_fixture_sequence_to_vm();
        test_arm64_lifter_branch_sequence_to_vm();
        test_arm64_lifter_integer_sequence_to_vm();
        test_arm64_lifter_memory_sequence_to_vm();
        test_arm64_lifter_load_pair_sequence_to_vm();
        test_arm64_lifter_pair_writeback_sequence_to_vm();
        test_arm64_lifter_32bit_pair_and_ldpsw_sequence_to_vm();
        test_arm64_lifter_signed_load_sequence_to_vm();
        test_arm64_lifter_sign_extend_sequence_to_vm();
        test_arm64_lifter_unscaled_memory_sequence_to_vm();
        test_arm64_lifter_writeback_memory_sequence_to_vm();
        test_arm64_lifter_register_offset_memory_sequence_to_vm();
        test_arm64_lifter_signed_register_offset_memory_sequence_to_vm();
        test_arm64_lifter_signed_writeback_memory_sequence_to_vm();
        test_arm64_lifter_muldiv_sequence_to_vm();
        test_arm64_lifter_branch_zero_sequence_to_vm();
        test_arm64_lifter_narrow_sequence_to_vm();
        test_arm64_lifter_pair_sequence_to_vm();
        test_arm64_lifter_bitlogic_sequence_to_vm();
        test_arm64_lifter_condition_sequence_to_vm();
        test_aarch64_branch_encoder();
        test_arm32_branch_encoder();
        test_arm32_decoder_and_lifter_to_vm();
        test_arm32_runtime_payload_builder();
        test_aarch64_relocation_classification();
        test_aarch64_conditional_fp();
        test_aarch64_exclusive_pairs();
        test_sibling_vm_partition_and_bridge_contract();
        test_riscv64_decoder_lifter_vertical_slice();
        require(argc >= 1, "test executable path missing");
        test_elf_parser_on_self(argv[0]);
    } catch (const std::exception& ex) {
        std::cerr << "test failed: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "all tests passed\n";
    return EXIT_SUCCESS;
}
