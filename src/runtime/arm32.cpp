#include "myvmp/runtime/arm32.hpp"

#include "myvmp/runtime/format.hpp"
#include "myvmp/runtime/profile_tables.hpp"
#include "myvmp/vm/multi_vm.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

namespace myvmp::runtime {
namespace {

constexpr std::uint8_t kRuntimeCode[]{
#include "myvmp/runtime/arm32_blob.inc"
};
constexpr std::size_t kHeaderFixedSize = 1048;
constexpr std::size_t kBlockEntrySize = 12;

constexpr std::array kOpcodes{
    bytecode::Opcode::LoadImm64, bytecode::Opcode::AddImm64, bytecode::Opcode::SubImm64,
    bytecode::Opcode::AndImm64, bytecode::Opcode::OrImm64, bytecode::Opcode::XorImm64,
    bytecode::Opcode::AddImmSetFlags64, bytecode::Opcode::SubImmSetFlags64,
    bytecode::Opcode::XorRegShifted64, bytecode::Opcode::AndRegShifted64,
    bytecode::Opcode::OrRegShifted64, bytecode::Opcode::AddRegShifted64,
    bytecode::Opcode::SubRegShifted64, bytecode::Opcode::MoveReg64,
    bytecode::Opcode::AddRegShiftedSetFlags64, bytecode::Opcode::SubRegShiftedSetFlags64,
    bytecode::Opcode::BitClearRegShifted64, bytecode::Opcode::NotRegShifted64,
    bytecode::Opcode::MoveKeep64, bytecode::Opcode::ShiftReg32,
    bytecode::Opcode::MulAdd64, bytecode::Opcode::CompareImm64,
    bytecode::Opcode::CompareReg64, bytecode::Opcode::CompareImmBranch,
    bytecode::Opcode::TestImm64, bytecode::Opcode::TestRegShifted32,
    bytecode::Opcode::CompareRegBranch, bytecode::Opcode::BranchIf,
    bytecode::Opcode::LoadMem, bytecode::Opcode::StoreMem,
    bytecode::Opcode::JumpBlock, bytecode::Opcode::CallVm,
    bytecode::Opcode::CallNative, bytecode::Opcode::Return, bytecode::Opcode::Halt,
};

struct RuntimeBlock {
    std::uint32_t start{};
    std::uint32_t end{};
    std::uint32_t owner{};
};

bool supported(bytecode::Opcode opcode) {
    return std::find(kOpcodes.begin(), kOpcodes.end(), opcode) != kOpcodes.end();
}

void validate_payload(const std::vector<std::uint8_t>& payload,
                      const bytecode::VmProfile& profile,
                      std::size_t begin, std::size_t end) {
    if (begin > end || end > payload.size()) throw std::runtime_error("invalid ARM32 VM block range");
    for (std::size_t pc = begin; pc < end;) {
        const auto opcode = profile.logical(payload[pc++]);
        if (!supported(opcode))
            throw std::runtime_error("ARM32 runtime does not implement bytecode opcode " +
                                     std::to_string(static_cast<unsigned>(opcode)));
        const auto operands = bytecode::operand_size(opcode);
        if (operands > end - pc)
            throw std::runtime_error("ARM32 instruction crosses its VM block boundary");
        pc += operands;
    }
}

void rewrite_branch_targets(std::vector<std::uint8_t>& payload,
                            const bytecode::VmProfile& profile) {
    std::vector<std::uint32_t> offsets;
    for (std::size_t pc = 0; pc < payload.size();) {
        offsets.push_back(static_cast<std::uint32_t>(pc));
        const auto opcode = profile.logical(payload[pc++]);
        pc += bytecode::operand_size(opcode);
        if (pc > payload.size()) throw std::runtime_error("ARM32 bytecode is truncated");
    }
    for (std::size_t pc = 0; pc < payload.size();) {
        const auto opcode = profile.logical(payload[pc]);
        const auto target_operand = bytecode::control_target_offset(opcode);
        if (target_operand && opcode != bytecode::Opcode::JumpBlock) {
            const auto location = pc + *target_operand;
            const auto target = bytecode::read_u32_le(payload, location);
            if (target >= offsets.size()) throw std::runtime_error("ARM32 VM target is out of range");
            bytecode::write_u32_le(payload, location, offsets[target]);
        }
        pc += 1 + bytecode::operand_size(opcode);
    }
}

std::vector<RuntimeBlock> runtime_blocks(const bytecode::Image& image,
                                         const std::vector<std::uint32_t>& owners,
                                         std::uint32_t primary_id) {
    const auto payload_size = bytecode::decode_payload(image).size();
    if (payload_size > std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error("ARM32 payload is too large");
    if (image.blocks.empty()) return {{0, static_cast<std::uint32_t>(payload_size), 0}};
    if (owners.size() != image.blocks.size())
        throw std::runtime_error("ARM32 block ownership count mismatch");
    std::vector<RuntimeBlock> result;
    std::size_t expected = 0;
    for (std::size_t index = 0; index < image.blocks.size(); ++index) {
        const auto& block = image.blocks[index];
        if (block.payload_offset != expected || block.payload_size == 0 ||
            block.payload_size > payload_size - expected)
            throw std::runtime_error("ARM32 blocks must contiguously cover the payload");
        expected += block.payload_size;
        result.push_back({static_cast<std::uint32_t>(block.payload_offset),
                          static_cast<std::uint32_t>(expected),
                          owners[index] == primary_id ? 0u : 1u});
    }
    if (expected != payload_size) throw std::runtime_error("ARM32 blocks do not cover payload");
    return result;
}

void encode_block(std::vector<std::uint8_t>& payload, const bytecode::VmProfile& profile,
                  std::uint32_t key, const RuntimeBlock& block) {
    for (std::size_t pc = block.start; pc < block.end;) {
        const auto physical = payload[pc];
        const auto size = bytecode::operand_size(profile.logical(physical));
        const auto base = static_cast<std::uint8_t>(key) ^ static_cast<std::uint8_t>(pc * 0x9bu) ^
                          static_cast<std::uint8_t>(physical * 0x5du) ^
                          static_cast<std::uint8_t>(block.owner * 0x6du);
        for (std::size_t index = 0; index < size; ++index)
            payload[pc + 1 + index] ^= static_cast<std::uint8_t>(base + index * 0xa7u);
        const auto opcode_mask = static_cast<std::uint8_t>(key) ^
            static_cast<std::uint8_t>(block.start * 0x3du) ^
            static_cast<std::uint8_t>(block.end * 0xc7u) ^
            static_cast<std::uint8_t>(block.owner * 0x6du);
        payload[pc] ^= opcode_mask;
        pc += 1 + size;
    }
}

std::vector<std::uint8_t> build_runtime(std::vector<std::uint8_t> payload, std::uint32_t key,
                                        const bytecode::VmProfile& primary,
                                        const bytecode::VmProfile& secondary,
                                        const std::vector<RuntimeBlock>& blocks,
                                        Arm32RuntimeContext context) {
    if (payload.size() > 65536) throw std::runtime_error("ARM32 payload exceeds 64 KiB arena");
    for (const auto& block : blocks) {
        const auto& profile = block.owner == 0 ? primary : secondary;
        validate_payload(payload, profile, block.start, block.end);
        encode_block(payload, profile, key, block);
    }
    bytecode::crypt_payload(payload, key);
    std::vector<std::uint8_t> result(std::begin(kRuntimeCode), std::end(kRuntimeCode));
    append_u32_le(result, static_cast<std::uint32_t>(payload.size()));
    append_u32_le(result, key);
    append_u32_le(result, context.function_va);
    append_u32_le(result, static_cast<std::uint32_t>(blocks.size()));
    append_u32_le(result, 0);
    append_u32_le(result, 0);
    for (const auto& profile : {primary, secondary}) {
        const auto table = physical_to_logical_table(profile);
        result.insert(result.end(), table.begin(), table.end());
    }
    for (const auto& profile : {primary, secondary}) {
        const auto table = physical_operand_size_table(profile);
        result.insert(result.end(), table.begin(), table.end());
    }
    for (const auto& block : blocks) {
        append_u32_le(result, block.start);
        append_u32_le(result, block.end);
        append_u32_le(result, block.owner);
    }
    result.insert(result.end(), payload.begin(), payload.end());
    return result;
}

} // namespace

std::vector<std::uint8_t> build_arm32_runtime_payload(const bytecode::Image& image,
                                                      Arm32RuntimeContext context) {
    auto payload = bytecode::decode_payload(image);
    if (!image.branch_targets_are_offsets) rewrite_branch_targets(payload, image.profile);
    std::vector<std::uint32_t> owners(image.blocks.size(), image.profile.seed);
    const auto blocks = runtime_blocks(image, owners, image.profile.seed);
    return build_runtime(std::move(payload), image.key, image.profile, image.profile, blocks, context);
}

std::vector<std::uint8_t> build_arm32_runtime_payload(const vm::SiblingVmImage& sibling,
                                                      Arm32RuntimeContext context) {
    sibling.validate();
    if (!sibling.image.branch_targets_are_offsets)
        throw std::runtime_error("ARM32 sibling VM requires byte-offset targets");
    auto payload = bytecode::decode_payload(sibling.image);
    const auto blocks = runtime_blocks(sibling.image, sibling.block_profile_ids,
                                       sibling.partition.primary_profile_id);
    return build_runtime(std::move(payload), sibling.image.key, sibling.primary_profile,
                         sibling.secondary_profile, blocks, context);
}

std::size_t arm32_runtime_code_size() noexcept { return sizeof(kRuntimeCode); }
std::size_t arm32_encrypted_bytecode_offset(std::size_t block_count) noexcept {
    return sizeof(kRuntimeCode) + kHeaderFixedSize + block_count * kBlockEntrySize;
}

} // namespace myvmp::runtime
