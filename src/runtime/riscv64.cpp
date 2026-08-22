#include "myvmp/runtime/riscv64.hpp"
#include "myvmp/runtime/format.hpp"
#include "myvmp/runtime/profile_tables.hpp"
#include "myvmp/vm/multi_vm.hpp"

#include <array>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace myvmp::runtime {
namespace {

constexpr std::uint8_t kRuntimeCode[]{
#include "myvmp/runtime/riscv64_blob.inc"
};

constexpr std::size_t kRuntimeHeaderFixedSize = 1048;
constexpr std::size_t kRuntimeBlockEntrySize = 12;

struct RuntimeBlock {
    std::uint32_t start{};
    std::uint32_t end{};
    std::uint32_t owner{};
};

void encode_operand_windows(std::vector<std::uint8_t>& payload,
                            const bytecode::VmProfile& profile, std::uint32_t key,
                            std::size_t begin, std::size_t end, std::uint32_t owner) {
    for (std::size_t pc = begin; pc < end;) {
        const auto physical = payload[pc];
        const auto size = bytecode::operand_size(profile.logical(physical));
        const auto base = static_cast<std::uint8_t>(key) ^ static_cast<std::uint8_t>(pc * 0x9bu) ^
                          static_cast<std::uint8_t>(physical * 0x5du) ^
                          static_cast<std::uint8_t>(owner * 0x6du);
        if (1 + size > end - pc)
            throw std::runtime_error("RV64 instruction crosses its VM block boundary");
        for (std::size_t index = 0; index < size; ++index)
            payload[pc + 1 + index] ^= static_cast<std::uint8_t>(base + index * 0xa7u);
        const auto opcode_mask = static_cast<std::uint8_t>(key) ^
            static_cast<std::uint8_t>(begin * 0x3du) ^
            static_cast<std::uint8_t>(end * 0xc7u) ^
            static_cast<std::uint8_t>(owner * 0x6du);
        payload[pc] ^= opcode_mask;
        pc += 1 + size;
    }
}

bool supported(bytecode::Opcode opcode) {
    static constexpr std::array allowed{
        bytecode::Opcode::LoadImm64, bytecode::Opcode::LoadAddress64,
        bytecode::Opcode::AddImm64, bytecode::Opcode::SubImm64,
        bytecode::Opcode::AndImm64, bytecode::Opcode::OrImm64, bytecode::Opcode::XorImm64,
        bytecode::Opcode::AddRegShifted64, bytecode::Opcode::SubRegShifted64,
        bytecode::Opcode::AndRegShifted64, bytecode::Opcode::OrRegShifted64,
        bytecode::Opcode::XorRegShifted64, bytecode::Opcode::ShiftReg64,
        bytecode::Opcode::ShiftReg32, bytecode::Opcode::ShiftRegVariable64,
        bytecode::Opcode::DivideReg64, bytecode::Opcode::UnsignedMulHigh64,
        bytecode::Opcode::MulAdd64, bytecode::Opcode::CompareImm64,
        bytecode::Opcode::CompareReg64, bytecode::Opcode::CompareImmBranch,
        bytecode::Opcode::CompareRegBranch, bytecode::Opcode::BranchIf,
        bytecode::Opcode::Select64, bytecode::Opcode::LoadMem,
        bytecode::Opcode::StoreMem, bytecode::Opcode::JumpBlock,
        bytecode::Opcode::CallNative, bytecode::Opcode::CallNativeReg,
        bytecode::Opcode::CallVm,
        bytecode::Opcode::NoOp, bytecode::Opcode::Return, bytecode::Opcode::Halt,
    };
    return std::find(allowed.begin(), allowed.end(), opcode) != allowed.end();
}

void validate_payload(const std::vector<std::uint8_t>& payload,
                      const bytecode::VmProfile& profile, std::size_t begin, std::size_t end) {
    if (begin > end || end > payload.size()) throw std::runtime_error("invalid RV64 VM block range");
    for (std::size_t pc = begin; pc < end;) {
        const auto opcode = profile.logical(payload[pc++]);
        if (!supported(opcode))
            throw std::runtime_error("RV64 runtime does not implement bytecode opcode " +
                                     std::to_string(static_cast<unsigned>(opcode)));
        const auto operands = bytecode::operand_size(opcode);
        if (operands > end - pc) throw std::runtime_error("RV64 bytecode is truncated at a VM block boundary");
        pc += operands;
    }
}

std::vector<RuntimeBlock> runtime_blocks(const bytecode::Image& image,
                                         const std::vector<std::uint32_t>& owners,
                                         std::uint32_t primary_id) {
    const auto payload_size = bytecode::decode_payload(image).size();
    if (payload_size > std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error("RV64 bytecode payload is too large");
    if (image.blocks.empty()) return {{0, static_cast<std::uint32_t>(payload_size), 0}};
    if (owners.size() != image.blocks.size()) throw std::runtime_error("RV64 VM block ownership count mismatch");
    std::vector<RuntimeBlock> result;
    result.reserve(image.blocks.size());
    std::size_t expected = 0;
    for (std::size_t index = 0; index < image.blocks.size(); ++index) {
        const auto& block = image.blocks[index];
        if (block.payload_offset != expected || block.payload_size == 0 ||
            block.payload_size > payload_size - expected)
            throw std::runtime_error("RV64 VM blocks must exactly and contiguously cover the payload");
        const auto end = expected + block.payload_size;
        result.push_back({static_cast<std::uint32_t>(expected), static_cast<std::uint32_t>(end),
                          owners[index] == primary_id ? 0u : 1u});
        expected = end;
    }
    if (expected != payload_size) throw std::runtime_error("RV64 VM blocks do not cover the payload");
    return result;
}

void rewrite_branch_targets(std::vector<std::uint8_t>& payload,
                            const bytecode::VmProfile& profile) {
    std::vector<std::uint32_t> offsets;
    for (std::size_t pc = 0; pc < payload.size();) {
        offsets.push_back(static_cast<std::uint32_t>(pc));
        const auto opcode = profile.logical(payload[pc++]);
        pc += bytecode::operand_size(opcode);
        if (pc > payload.size()) throw std::runtime_error("RV64 bytecode is truncated while indexing branches");
    }
    for (std::size_t pc = 0; pc < payload.size();) {
        const auto opcode = profile.logical(payload[pc]);
        const auto target_operand = bytecode::control_target_offset(opcode);
        if (target_operand && opcode != bytecode::Opcode::JumpBlock) {
            const auto location = pc + *target_operand;
            const auto target = bytecode::read_u32_le(payload, location);
            if (target >= offsets.size()) throw std::runtime_error("RV64 branch target is out of range");
            bytecode::write_u32_le(payload, location, offsets[target]);
        }
        pc += 1 + bytecode::operand_size(opcode);
    }
}

std::vector<std::uint8_t> build_runtime(std::vector<std::uint8_t> payload, std::uint32_t key,
                                        const bytecode::VmProfile& primary,
                                        const bytecode::VmProfile& secondary,
                                        const std::vector<RuntimeBlock>& blocks,
                                        Riscv64RuntimeContext context) {
    if (payload.size() > 65536) throw std::runtime_error("RV64 runtime payload exceeds its 64 KiB arena");
    for (const auto& block : blocks) {
        const auto& profile = block.owner == 0 ? primary : secondary;
        validate_payload(payload, profile, block.start, block.end);
        encode_operand_windows(payload, profile, key, block.start, block.end, block.owner);
    }
    bytecode::crypt_payload(payload, key);
    std::vector<std::uint8_t> result(std::begin(kRuntimeCode), std::end(kRuntimeCode));
    append_u32_le(result, static_cast<std::uint32_t>(payload.size()));
    append_u32_le(result, key);
    append_u64_le(result, context.function_va);
    append_u32_le(result, static_cast<std::uint32_t>(blocks.size()));
    append_u32_le(result, 0);
    for (const auto& profile : {primary, secondary}) {
        const auto table = physical_to_logical_table(profile);
        result.insert(result.end(), table.begin(), table.end());
    }
    for (const auto& profile : {primary, secondary}) {
        const auto sizes = physical_operand_size_table(profile);
        result.insert(result.end(), sizes.begin(), sizes.end());
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

std::vector<std::uint8_t> build_riscv64_runtime_payload(const bytecode::Image& image,
                                                        Riscv64RuntimeContext context) {
    auto payload = bytecode::decode_payload(image);
    if (!image.branch_targets_are_offsets) rewrite_branch_targets(payload, image.profile);
    std::vector<std::uint32_t> owners(image.blocks.size(), image.profile.seed);
    const auto blocks = runtime_blocks(image, owners, image.profile.seed);
    return build_runtime(std::move(payload), image.key, image.profile, image.profile, blocks, context);
}

std::size_t riscv64_runtime_code_size() noexcept { return sizeof(kRuntimeCode); }
std::size_t riscv64_encrypted_bytecode_offset(std::size_t block_count) noexcept {
    return sizeof(kRuntimeCode) + kRuntimeHeaderFixedSize + block_count * kRuntimeBlockEntrySize;
}

std::vector<std::uint8_t> build_riscv64_runtime_payload(const vm::SiblingVmImage& image,
                                                        Riscv64RuntimeContext context) {
    image.validate();
    if (!image.image.branch_targets_are_offsets)
        throw std::runtime_error("RV64 sibling VM requires byte-offset control-flow targets");
    auto payload = bytecode::decode_payload(image.image);
    const auto blocks = runtime_blocks(image.image, image.block_profile_ids,
                                       image.partition.primary_profile_id);
    return build_runtime(std::move(payload), image.image.key, image.primary_profile,
                         image.secondary_profile, blocks, context);
}

} // namespace myvmp::runtime
