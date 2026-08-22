#include "myvmp/runtime/arm32.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace myvmp::runtime {
namespace {

constexpr std::uint8_t kRuntimeCode[]{
#include "myvmp/runtime/arm32_blob.inc"
};

constexpr std::array<bytecode::Opcode, 18> kOpcodes{
    bytecode::Opcode::LoadImm64, bytecode::Opcode::AddImm64, bytecode::Opcode::SubImm64,
    bytecode::Opcode::XorRegShifted64,
    bytecode::Opcode::AndRegShifted64, bytecode::Opcode::OrRegShifted64,
    bytecode::Opcode::AddRegShifted64, bytecode::Opcode::SubRegShifted64,
    bytecode::Opcode::MoveReg64,
    bytecode::Opcode::ShiftReg32,
    bytecode::Opcode::MulAdd64, bytecode::Opcode::CompareImm64,
    bytecode::Opcode::CompareReg64, bytecode::Opcode::BranchIf,
    bytecode::Opcode::LoadMem, bytecode::Opcode::StoreMem,
    bytecode::Opcode::Return, bytecode::Opcode::Halt,
};

std::size_t supported_operand_size(bytecode::Opcode opcode) {
    if (std::find(kOpcodes.begin(), kOpcodes.end(), opcode) == kOpcodes.end()) {
        throw std::runtime_error("ARM32 runtime does not implement bytecode opcode " +
                                 std::to_string(static_cast<unsigned>(opcode)));
    }
    return bytecode::operand_size(opcode);
}

void rewrite_branches(std::vector<std::uint8_t>& payload, const bytecode::VmProfile& profile) {
    std::vector<std::uint32_t> offsets;
    for (std::size_t pc = 0; pc < payload.size();) {
        offsets.push_back(static_cast<std::uint32_t>(pc));
        const auto opcode = profile.logical(payload[pc++]);
        pc += supported_operand_size(opcode);
        if (pc > payload.size()) throw std::runtime_error("ARM32 bytecode is truncated");
        if (opcode == bytecode::Opcode::Halt) {
            if (pc != payload.size()) throw std::runtime_error("ARM32 bytecode continues after Halt");
            break;
        }
    }
    for (std::size_t pc = 0; pc < payload.size();) {
        const auto opcode = profile.logical(payload[pc++]);
        if (opcode == bytecode::Opcode::BranchIf) {
            const auto target = bytecode::read_u32_le(payload, pc + 1);
            if (target >= offsets.size()) throw std::runtime_error("ARM32 VM branch target is out of range");
            bytecode::write_u32_le(payload, pc + 1, offsets[target]);
        }
        pc += supported_operand_size(opcode);
    }
}

void patch_dispatcher(std::vector<std::uint8_t>& code, const bytecode::VmProfile& profile) {
    std::size_t index = 0;
    for (std::size_t offset = 0; offset + 4 <= code.size(); offset += 4) {
        auto word = bytecode::read_u32_le(code, offset);
        if ((word & 0xfffff000u) != 0xe35a0000u) continue;
        if (index >= kOpcodes.size()) throw std::runtime_error("ARM32 runtime has unexpected dispatcher comparison");
        word = (word & ~0xffu) | profile.physical(kOpcodes[index++]);
        bytecode::write_u32_le(code, offset, word);
    }
    if (index != kOpcodes.size()) throw std::runtime_error("ARM32 runtime dispatcher template is out of sync");
}

} // namespace

std::vector<std::uint8_t> build_arm32_runtime_payload(const bytecode::Image& image) {
    auto payload = bytecode::decode_payload(image);
    rewrite_branches(payload, image.profile);
    bytecode::crypt_payload(payload, image.key);
    std::vector<std::uint8_t> result(std::begin(kRuntimeCode), std::end(kRuntimeCode));
    patch_dispatcher(result, image.profile);
    const auto size = static_cast<std::uint32_t>(payload.size());
    for (unsigned shift = 0; shift < 32; shift += 8) result.push_back(static_cast<std::uint8_t>(size >> shift));
    for (unsigned shift = 0; shift < 32; shift += 8) result.push_back(static_cast<std::uint8_t>(image.key >> shift));
    result.insert(result.end(), payload.begin(), payload.end());
    return result;
}

std::size_t arm32_runtime_code_size() noexcept { return sizeof(kRuntimeCode); }

} // namespace myvmp::runtime
