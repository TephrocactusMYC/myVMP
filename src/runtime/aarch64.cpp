#include "myvmp/runtime/aarch64.hpp"
#include "myvmp/runtime/format.hpp"
#include "myvmp/runtime/profile_tables.hpp"
#include "myvmp/vm/multi_vm.hpp"

#include <array>
#include <algorithm>
#include <iterator>
#include <stdexcept>

namespace myvmp::runtime {
namespace {

constexpr std::uint8_t kRuntimeCode[]{
#include "myvmp/runtime/aarch64_blob.inc"
};

constexpr std::size_t kRuntimeBytecodeCapacity = 64 * 1024 - 32;
// Runtime header: payload size, master key, handler mask, block count, dispatcher XOR
// key (stored as u32 for aligned native loading), and entry delta.
constexpr std::size_t kRuntimeHeaderSize = 4 + 4 + 4 + 4 + 4 + 8 + 4;
constexpr std::size_t kBlockBoundarySize = 4;
constexpr std::size_t kOperandSizeTableSize = 256;
constexpr std::uint32_t kRollingKeyInitialXor = 0x6a09e667u;
constexpr std::uint32_t kRollingKeyBlockMix = 0x85ebca6bu;
constexpr std::uint32_t kRollingKeyProfileMix = 0x27d4eb2du;
constexpr std::array<bytecode::Opcode, 90> kDispatcherOpcodes{
    bytecode::Opcode::LoadImm64, bytecode::Opcode::LoadAddress64,
    bytecode::Opcode::AddImm64,
    bytecode::Opcode::XorRegShifted64, bytecode::Opcode::AddRegShifted64,
    bytecode::Opcode::SubImm64, bytecode::Opcode::AndImm64,
    bytecode::Opcode::XorImm64,
    bytecode::Opcode::MoveKeep64, bytecode::Opcode::OrRegShifted64,
    bytecode::Opcode::SubRegExtended64, bytecode::Opcode::OrImm64,
    bytecode::Opcode::AddRegExtended64, bytecode::Opcode::BitfieldExtract64,
    bytecode::Opcode::ShiftRegVariable64, bytecode::Opcode::DivideReg64,
    bytecode::Opcode::UnsignedMulHigh64, bytecode::Opcode::MulAdd64,
    bytecode::Opcode::MulSub64, bytecode::Opcode::SignedMulLong64,
    bytecode::Opcode::UnsignedMulLong64, bytecode::Opcode::AndRegShifted64,
    bytecode::Opcode::NotRegShifted64, bytecode::Opcode::XorNotRegShifted64,
    bytecode::Opcode::OrNotRegShifted64, bytecode::Opcode::BitClearRegShifted64,
    bytecode::Opcode::ShiftReg64, bytecode::Opcode::ShiftReg32,
    bytecode::Opcode::SubRegShifted64, bytecode::Opcode::AddImmSetFlags64,
    bytecode::Opcode::SubImmSetFlags64, bytecode::Opcode::SubRegShiftedSetFlags64,
    bytecode::Opcode::AddRegShiftedSetFlags64,
    bytecode::Opcode::AddRegExtendedSetFlags64,
    bytecode::Opcode::SubRegExtendedSetFlags64,
    bytecode::Opcode::NegReg64, bytecode::Opcode::NegRegSetFlags64,
    bytecode::Opcode::MoveReg64,
    bytecode::Opcode::CompareImm64, bytecode::Opcode::CompareReg64,
    bytecode::Opcode::TestImm64, bytecode::Opcode::TestRegShifted64,
    bytecode::Opcode::TestRegShifted32, bytecode::Opcode::BranchIf,
    bytecode::Opcode::BranchRegZero, bytecode::Opcode::BranchRegBit,
    bytecode::Opcode::Select64, bytecode::Opcode::SelectInc64,
    bytecode::Opcode::SelectInv64, bytecode::Opcode::SelectNeg64,
    bytecode::Opcode::LoadMem,
    bytecode::Opcode::StoreMem, bytecode::Opcode::Load64,
    bytecode::Opcode::Store64, bytecode::Opcode::LoadRegOffsetMem,
    bytecode::Opcode::StoreRegOffsetMem, bytecode::Opcode::StorePair64,
    bytecode::Opcode::CallNative, bytecode::Opcode::CallNativeReg,
    bytecode::Opcode::CallVm, bytecode::Opcode::LoadFpImm64,
    bytecode::Opcode::FpAdd64, bytecode::Opcode::FpSub64,
    bytecode::Opcode::FpMul64, bytecode::Opcode::FpDiv64,
    bytecode::Opcode::FpMadd64, bytecode::Opcode::FpCompare64,
    bytecode::Opcode::LoadFpImm32, bytecode::Opcode::FpAdd32,
    bytecode::Opcode::FpSub32, bytecode::Opcode::FpMul32,
    bytecode::Opcode::FpDiv32, bytecode::Opcode::FpMadd32,
    bytecode::Opcode::FpCompare32,
    bytecode::Opcode::LoadAcquire, bytecode::Opcode::StoreRelease,
    bytecode::Opcode::LoadExclusive, bytecode::Opcode::StoreExclusive,
    bytecode::Opcode::CompareSwap,
    bytecode::Opcode::FpUnary,
    bytecode::Opcode::FpConvert, bytecode::Opcode::FpMinMax,
    bytecode::Opcode::FpConditionalCompare, bytecode::Opcode::FpSelect,
    bytecode::Opcode::LoadExclusivePair, bytecode::Opcode::StoreExclusivePair,
    bytecode::Opcode::NoOp, bytecode::Opcode::Return, bytecode::Opcode::JumpBlock,
    bytecode::Opcode::Halt,
};

std::uint32_t read_code_word(const std::vector<std::uint8_t>& code, std::size_t offset) {
    return static_cast<std::uint32_t>(code[offset]) |
           (static_cast<std::uint32_t>(code[offset + 1]) << 8u) |
           (static_cast<std::uint32_t>(code[offset + 2]) << 16u) |
           (static_cast<std::uint32_t>(code[offset + 3]) << 24u);
}

void write_code_word(std::vector<std::uint8_t>& code, std::size_t offset, std::uint32_t word) {
    code[offset] = static_cast<std::uint8_t>(word);
    code[offset + 1] = static_cast<std::uint8_t>(word >> 8u);
    code[offset + 2] = static_cast<std::uint8_t>(word >> 16u);
    code[offset + 3] = static_cast<std::uint8_t>(word >> 24u);
}

template <std::size_t N>
void patch_dispatch_comparisons(std::vector<std::uint8_t>& code,
                                std::uint32_t comparison_signature,
                                const std::array<std::uint8_t, N>& physical_opcodes,
                                const char* family) {
    // A64 `cmp Wn, #imm` is the SUBS-immediate alias. The physical opcode occupies imm12;
    // dedicated source registers in the assembly template distinguish patch families.
    constexpr std::uint32_t kCmpImmediateMask = 0xffc003ffu;
    constexpr std::uint32_t kImmediateFieldMask = 0xfffu << 10u;
    std::size_t patched = 0;
    for (std::size_t offset = 0; offset + 4 <= code.size() && patched < N; offset += 4) {
        const auto word = read_code_word(code, offset);
        if ((word & kCmpImmediateMask) != comparison_signature) continue;
        write_code_word(code, offset, (word & ~kImmediateFieldMask) |
                                          (static_cast<std::uint32_t>(physical_opcodes[patched++]) << 10u));
    }
    if (patched != N) {
        throw std::runtime_error(std::string("AArch64 ") + family +
                                 " dispatcher template is out of sync");
    }
}

void patch_dispatcher(std::vector<std::uint8_t>& code, const bytecode::VmProfile& profile) {
    constexpr std::uint32_t kCmpW10Immediate = 0x7100015fu;
    std::array<std::uint8_t, kDispatcherOpcodes.size()> primary{};
    std::transform(kDispatcherOpcodes.begin(), kDispatcherOpcodes.end(), primary.begin(),
                   [&](bytecode::Opcode opcode) {
                       return static_cast<std::uint8_t>(profile.physical(opcode) ^
                                                        profile.dispatcher_xor_key);
                   });
    patch_dispatch_comparisons(code, kCmpW10Immediate, primary, "primary");

    constexpr std::uint32_t kCmpW17Immediate = 0x7100023fu;
    const std::array<std::uint8_t, 7> duplicates{
        static_cast<std::uint8_t>(profile.load_imm64_duplicate ^ profile.dispatcher_xor_key),
        static_cast<std::uint8_t>(profile.add_imm64_duplicate ^ profile.dispatcher_xor_key),
        static_cast<std::uint8_t>(profile.sub_imm64_duplicate ^ profile.dispatcher_xor_key),
        static_cast<std::uint8_t>(profile.and_imm64_duplicate ^ profile.dispatcher_xor_key),
        static_cast<std::uint8_t>(profile.xor_imm64_duplicate ^ profile.dispatcher_xor_key),
        static_cast<std::uint8_t>(profile.or_imm64_duplicate ^ profile.dispatcher_xor_key),
        static_cast<std::uint8_t>(profile.move_reg64_duplicate ^ profile.dispatcher_xor_key),
    };
    patch_dispatch_comparisons(code, kCmpW17Immediate, duplicates, "duplicate-opcode");
    patch_dispatch_comparisons(code, 0x7100021fu,
                               std::array{static_cast<std::uint8_t>(profile.compare_imm_branch ^ profile.dispatcher_xor_key)}, "compare-immediate superoperator");
    patch_dispatch_comparisons(code, 0x710001ffu,
                               std::array{static_cast<std::uint8_t>(profile.compare_reg_branch ^ profile.dispatcher_xor_key)}, "compare-register superoperator");
    patch_dispatch_comparisons(code, 0x710001dfu,
                               std::array{static_cast<std::uint8_t>(profile.test_imm_branch ^ profile.dispatcher_xor_key)}, "test-immediate superoperator");
}

std::array<std::uint8_t, 256> opcode_translation(const bytecode::VmProfile& source,
                                                 const bytecode::VmProfile& transport,
                                                 std::uint32_t key,
                                                 std::uint32_t profile_index) {
    std::array<std::uint8_t, 256> table{};
    table.fill(0xffu);
    for (std::size_t physical = 0; physical < table.size(); ++physical) {
        try {
            table[physical] = transport.physical(source.logical(static_cast<std::uint8_t>(physical)));
        } catch (const std::runtime_error&) {
            // Invalid physical values remain fail-closed and reach myvmp_bad_opcode.
        }
        table[physical] ^= static_cast<std::uint8_t>(key) ^
                           static_cast<std::uint8_t>(profile_index * 0x5du) ^
                           static_cast<std::uint8_t>(physical * 0x9bu);
    }
    return table;
}

void encode_operand_window(std::vector<std::uint8_t>& payload, std::size_t pc,
                           bytecode::Opcode opcode, std::uint32_t key) {
    const auto count = bytecode::operand_size(opcode);
    if (pc >= payload.size() || count > payload.size() - pc - 1) {
        throw std::runtime_error("AArch64 runtime operand window is truncated");
    }
    const auto base = static_cast<std::uint8_t>(key) ^
                      static_cast<std::uint8_t>(pc * 0x9bu) ^
                      static_cast<std::uint8_t>(payload[pc] * 0x5du);
    for (std::size_t index = 0; index < count; ++index) {
        payload[pc + 1 + index] ^=
            static_cast<std::uint8_t>(base + static_cast<std::uint8_t>(index * 0xa7u));
    }
}

void encode_runtime_operands(std::vector<std::uint8_t>& payload,
                             const bytecode::VmProfile& profile,
                             std::uint32_t key) {
    for (std::size_t pc = 0; pc < payload.size();) {
        const auto opcode = profile.logical(payload[pc]);
        encode_operand_window(payload, pc, opcode, key);
        pc += 1 + bytecode::operand_size(opcode);
    }
}

void encode_multi_runtime_operands(std::vector<std::uint8_t>& payload,
                                   const vm::SiblingVmImage& sibling) {
    for (std::size_t block_index = 0; block_index < sibling.image.blocks.size(); ++block_index) {
        const auto& block = sibling.image.blocks[block_index];
        const auto& profile = sibling.block_profile_ids[block_index] ==
                                      sibling.partition.primary_profile_id
                                  ? sibling.primary_profile : sibling.secondary_profile;
        const auto end = block.payload_offset + block.payload_size;
        const auto owner = sibling.block_profile_ids[block_index] ==
                                   sibling.partition.primary_profile_id ? 0u : 1u;
        const auto opcode_mask = static_cast<std::uint8_t>(sibling.image.key) ^
                                 static_cast<std::uint8_t>(block_index * 0x3du) ^
                                 static_cast<std::uint8_t>(owner * 0x6du);
        for (std::size_t pc = block.payload_offset; pc < end;) {
            const auto opcode = profile.logical(payload[pc]);
            payload[pc] ^= opcode_mask;
            encode_operand_window(payload, pc, opcode, sibling.image.key);
            pc += 1 + bytecode::operand_size(opcode);
        }
    }
}

void require_bytes(const std::vector<std::uint8_t>& payload, std::size_t pc, std::size_t count) {
    if (pc > payload.size() || count > payload.size() - pc) {
        throw std::runtime_error("AArch64 runtime bytecode operand is truncated");
    }
}

std::uint64_t read_u64(const std::vector<std::uint8_t>& payload, std::size_t offset) {
    require_bytes(payload, offset, 8);
    std::uint64_t value = 0;
    for (unsigned shift = 0; shift < 64; shift += 8) {
        value |= static_cast<std::uint64_t>(payload[offset + shift / 8]) << shift;
    }
    return value;
}

void write_u64(std::vector<std::uint8_t>& payload, std::size_t offset, std::uint64_t value) {
    require_bytes(payload, offset, 8);
    for (unsigned shift = 0; shift < 64; shift += 8) {
        payload[offset + shift / 8] = static_cast<std::uint8_t>(value >> shift);
    }
}

void rewrite_branch_targets(std::vector<std::uint8_t>& payload,
                            const bytecode::VmProfile& profile) {
    std::size_t pc = 0;
    std::vector<std::size_t> offsets;
    while (pc < payload.size()) {
        offsets.push_back(pc);
        const auto physical = payload[pc++];
        const auto opcode = profile.logical(physical);
        const auto operands = bytecode::operand_size(opcode);
        require_bytes(payload, pc, operands);
        pc += operands;
        if (opcode == bytecode::Opcode::Halt) {
            if (pc != payload.size()) {
                throw std::runtime_error("AArch64 runtime bytecode continues after Halt");
            }
            break;
        }
    }
    pc = 0;
    while (pc < payload.size()) {
        const auto opcode = profile.logical(payload[pc]);
        const auto operand_offset = bytecode::control_target_offset(opcode);
        if (operand_offset.has_value() && opcode != bytecode::Opcode::JumpBlock) {
            const auto target_offset = pc + *operand_offset;
            const auto target = bytecode::read_u32_le(payload, target_offset);
            if (target >= offsets.size() || offsets[target] > 0xffffffffu) {
                throw std::runtime_error("AArch64 runtime branch target is out of range");
            }
            bytecode::write_u32_le(payload, target_offset, static_cast<std::uint32_t>(offsets[target]));
        }
        const auto operands = bytecode::operand_size(opcode);
        pc += 1 + operands;
        if (opcode == bytecode::Opcode::Halt) {
            break;
        }
    }
}

std::uint32_t next_block_key(std::uint32_t key, std::uint32_t block_index) {
    key ^= key << 13u;
    key ^= key >> 17u;
    key ^= key << 5u;
    return key ^ (block_index * kRollingKeyBlockMix);
}

void crypt_aarch64_runtime_payload(std::vector<std::uint8_t>& payload, std::uint32_t key,
                                   const std::vector<bytecode::BasicBlock>& blocks) {
    bytecode::crypt_payload(payload, key);
    if (blocks.empty()) return;
    std::uint32_t block_key = key ^ kRollingKeyInitialXor;
    std::size_t expected_offset = 0;
    for (std::size_t block_index = 0; block_index < blocks.size(); ++block_index) {
        const auto& block = blocks[block_index];
        if (block.payload_offset != expected_offset || block.payload_offset > payload.size() ||
            block.payload_size > payload.size() - block.payload_offset) {
            throw std::runtime_error("AArch64 bytecode block range is invalid");
        }
        if (block_index != 0) block_key = next_block_key(block_key, static_cast<std::uint32_t>(block_index));
        for (std::size_t relative = 0; relative < block.payload_size; ++relative) {
            payload[block.payload_offset + relative] ^=
                static_cast<std::uint8_t>(block_key >> ((relative & 3u) * 8u));
        }
        expected_offset += block.payload_size;
    }
    if (expected_offset != payload.size()) {
        throw std::runtime_error("AArch64 bytecode blocks do not cover the payload");
    }
}

void crypt_multi_runtime_payload(std::vector<std::uint8_t>& payload, std::uint32_t key,
                                 const std::vector<bytecode::BasicBlock>& blocks,
                                 const std::vector<std::uint32_t>& profile_ids,
                                 std::uint32_t primary_profile_id) {
    bytecode::crypt_payload(payload, key);
    if (blocks.size() != profile_ids.size()) {
        throw std::runtime_error("AArch64 multi-VM rolling-key ownership mismatch");
    }
    std::uint32_t block_key = key ^ kRollingKeyInitialXor;
    std::size_t expected_offset = 0;
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        const auto& block = blocks[index];
        if (block.payload_offset != expected_offset ||
            block.payload_size > payload.size() - block.payload_offset) {
            throw std::runtime_error("AArch64 multi-VM bytecode block range is invalid");
        }
        if (index != 0) block_key = next_block_key(block_key, static_cast<std::uint32_t>(index));
        const auto owner = profile_ids[index] == primary_profile_id ? 0u : 1u;
        block_key ^= owner * kRollingKeyProfileMix;
        for (std::size_t relative = 0; relative < block.payload_size; ++relative) {
            payload[block.payload_offset + relative] ^=
                static_cast<std::uint8_t>(block_key >> ((relative & 3u) * 8u));
        }
        expected_offset += block.payload_size;
    }
    if (expected_offset != payload.size()) {
        throw std::runtime_error("AArch64 multi-VM blocks do not cover the payload");
    }
}

void rewrite_native_operands(std::vector<std::uint8_t>& payload, std::size_t pc,
                             bytecode::Opcode opcode, std::uint64_t function_va) {
    if (opcode == bytecode::Opcode::CallNative) {
        if (function_va == 0) {
            throw std::invalid_argument("native call bytecode requires runtime addresses");
        }
        write_u64(payload, pc + 1, read_u64(payload, pc + 1) - function_va);
        const auto link = read_u64(payload, pc + 9);
        if (link != 0) {
            write_u64(payload, pc + 9, link - function_va);
        }
    } else if (opcode == bytecode::Opcode::CallNativeReg) {
        if (function_va == 0) {
            throw std::invalid_argument("native call bytecode requires runtime addresses");
        }
        const auto link = read_u64(payload, pc + 2);
        if (link != 0) {
            write_u64(payload, pc + 2, link - function_va);
        }
    } else if (opcode == bytecode::Opcode::LoadAddress64) {
        if (function_va == 0) {
            throw std::invalid_argument("address bytecode requires runtime addresses");
        }
        write_u64(payload, pc + 2, read_u64(payload, pc + 2) - function_va);
    }
}

void rewrite_native_call_addresses(std::vector<std::uint8_t>& payload,
                                   const bytecode::VmProfile& profile,
                                   std::uint64_t function_va,
                                   bool shuffled) {
    std::size_t pc = 0;
    while (pc < payload.size()) {
        const auto opcode = profile.logical(payload[pc]);
        rewrite_native_operands(payload, pc, opcode, function_va);
        pc += 1 + bytecode::operand_size(opcode);
        if (opcode == bytecode::Opcode::Halt && !shuffled) {
            break;
        }
    }
}

} // namespace

std::vector<std::uint8_t> build_aarch64_runtime_payload(const bytecode::Image& image,
                                                        Aarch64RuntimeContext context) {
    auto payload = bytecode::decode_payload(image);
    if (!image.branch_targets_are_offsets) rewrite_branch_targets(payload, image.profile);
    if ((context.function_va == 0) != (context.payload_va == 0)) {
        throw std::invalid_argument("AArch64 runtime addresses must be supplied together");
    }
    rewrite_native_call_addresses(payload, image.profile, context.function_va,
                                  image.branch_targets_are_offsets);
    encode_runtime_operands(payload, image.profile, image.key);
    if (payload.size() > kRuntimeBytecodeCapacity) {
        throw std::runtime_error("AArch64 runtime bytecode exceeds the 64 KiB frame capacity");
    }
    crypt_aarch64_runtime_payload(payload, image.key, image.blocks);

    std::vector<std::uint8_t> runtime_code(std::begin(kRuntimeCode), std::end(kRuntimeCode));
    patch_dispatcher(runtime_code, image.profile);
    std::vector<std::uint8_t> output;
    output.reserve(runtime_code.size() + kRuntimeHeaderSize +
                   image.blocks.size() * kBlockBoundarySize + kOperandSizeTableSize +
                   payload.size());
    output.insert(output.end(), runtime_code.begin(), runtime_code.end());
    append_u32_le(output, static_cast<std::uint32_t>(payload.size()));
    append_u32_le(output, image.key);
    append_u32_le(output, image.profile.handler_variant_mask);
    append_u32_le(output, static_cast<std::uint32_t>(image.blocks.size()));
    append_u32_le(output, image.profile.dispatcher_xor_key);
    const auto header_va = context.payload_va + runtime_code.size();
    const auto entry_delta = context.function_va == 0 ? 0 : context.function_va - header_va;
    append_u64_le(output, entry_delta);
    append_u32_le(output, 0);
    for (const auto& block : image.blocks) {
        const auto end = block.payload_offset + block.payload_size;
        if (end > 0xffffffffu) throw std::runtime_error("AArch64 bytecode block offset is too large");
        append_u32_le(output, static_cast<std::uint32_t>(end));
    }
    const auto sizes = physical_operand_size_table(image.profile,
                                                    image.profile.dispatcher_xor_key);
    output.insert(output.end(), sizes.begin(), sizes.end());
    output.insert(output.end(), payload.begin(), payload.end());
    return output;
}

std::vector<std::uint8_t> build_aarch64_runtime_payload(const vm::SiblingVmImage& sibling,
                                                        Aarch64RuntimeContext context) {
    sibling.validate();
    auto payload = bytecode::decode_payload(sibling.image);
    if (!sibling.image.branch_targets_are_offsets) {
        throw std::invalid_argument("sibling VM runtime requires explicit byte-offset targets");
    }
    if ((context.function_va == 0) != (context.payload_va == 0)) {
        throw std::invalid_argument("AArch64 runtime addresses must be supplied together");
    }
    for (std::size_t index = 0; index < sibling.image.blocks.size(); ++index) {
        const auto& block = sibling.image.blocks[index];
        const auto& profile = sibling.block_profile_ids[index] == sibling.partition.primary_profile_id
                                  ? sibling.primary_profile : sibling.secondary_profile;
        std::size_t pc = block.payload_offset;
        const auto end = block.payload_offset + block.payload_size;
        while (pc < end) {
            const auto opcode = profile.logical(payload[pc]);
            rewrite_native_operands(payload, pc, opcode, context.function_va);
            pc += 1 + bytecode::operand_size(opcode);
        }
        if (pc != end) throw std::runtime_error("sibling VM block ends inside an instruction");
    }
    if (payload.size() > kRuntimeBytecodeCapacity) {
        throw std::runtime_error("AArch64 sibling VM bytecode exceeds the 64 KiB frame capacity");
    }
    encode_multi_runtime_operands(payload, sibling);
    crypt_multi_runtime_payload(payload, sibling.image.key, sibling.image.blocks,
                                sibling.block_profile_ids,
                                sibling.partition.primary_profile_id);

    const auto transport = bytecode::fixed_profile();
    std::vector<std::uint8_t> runtime_code(std::begin(kRuntimeCode), std::end(kRuntimeCode));
    patch_dispatcher(runtime_code, transport);
    std::vector<std::uint8_t> output;
    output.reserve(runtime_code.size() + kRuntimeHeaderSize + sibling.image.blocks.size() * 8 +
                   8 + 512 + kOperandSizeTableSize + payload.size());
    output.insert(output.end(), runtime_code.begin(), runtime_code.end());
    append_u32_le(output, static_cast<std::uint32_t>(payload.size()));
    append_u32_le(output, sibling.image.key);
    append_u32_le(output, sibling.primary_profile.handler_variant_mask);
    append_u32_le(output, static_cast<std::uint32_t>(sibling.image.blocks.size()));
    append_u32_le(output, 0);
    const auto header_va = context.payload_va + runtime_code.size();
    const auto entry_delta = context.function_va == 0 ? 0 : context.function_va - header_va;
    append_u64_le(output, entry_delta);
    constexpr std::uint32_t kProfileCount = 2;
    append_u32_le(output, kProfileCount);
    for (const auto& block : sibling.image.blocks) {
        const auto end = block.payload_offset + block.payload_size;
        if (end > 0xffffffffu) throw std::runtime_error("AArch64 sibling VM block offset is too large");
        append_u32_le(output, static_cast<std::uint32_t>(end));
    }
    for (std::size_t index = 0; index < sibling.block_profile_ids.size(); ++index) {
        const auto owner = sibling.block_profile_ids[index] == sibling.partition.primary_profile_id ? 0u : 1u;
        append_u32_le(output, owner ^ sibling.image.key ^
                           (static_cast<std::uint32_t>(index) * 0x9e37u));
    }
    append_u32_le(output, sibling.primary_profile.handler_variant_mask);
    append_u32_le(output, sibling.secondary_profile.handler_variant_mask);
    const auto primary_map = opcode_translation(sibling.primary_profile, transport,
                                                sibling.image.key, 0);
    const auto secondary_map = opcode_translation(sibling.secondary_profile, transport,
                                                  sibling.image.key, 1);
    output.insert(output.end(), primary_map.begin(), primary_map.end());
    output.insert(output.end(), secondary_map.begin(), secondary_map.end());
    const auto sizes = physical_operand_size_table(transport);
    output.insert(output.end(), sizes.begin(), sizes.end());
    output.insert(output.end(), payload.begin(), payload.end());
    return output;
}

std::size_t aarch64_runtime_code_size() noexcept {
    return sizeof(kRuntimeCode);
}

std::size_t aarch64_runtime_bytecode_offset(const bytecode::Image& image) noexcept {
    return sizeof(kRuntimeCode) + kRuntimeHeaderSize + image.blocks.size() * kBlockBoundarySize +
           kOperandSizeTableSize;
}

std::size_t aarch64_runtime_bytecode_offset(const vm::SiblingVmImage& image) noexcept {
    constexpr std::size_t kProfileHandlerMasksSize = 8;
    constexpr std::size_t kOpcodeTranslationTablesSize = 2 * 256;
    return sizeof(kRuntimeCode) + kRuntimeHeaderSize +
           image.image.blocks.size() * (kBlockBoundarySize + sizeof(std::uint32_t)) +
           kProfileHandlerMasksSize + kOpcodeTranslationTablesSize + kOperandSizeTableSize;
}

} // namespace myvmp::runtime
