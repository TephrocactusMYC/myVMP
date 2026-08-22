#include "myvmp/arm32/decoder.hpp"

#include <stdexcept>

namespace myvmp::arm32 {
namespace {

std::uint32_t ror32(std::uint32_t value, unsigned amount) {
    amount &= 31u;
    return amount == 0 ? value : (value >> amount) | (value << (32u - amount));
}

ir::Condition condition_of(std::uint32_t word) {
    const auto value = static_cast<std::uint8_t>(word >> 28u);
    if (value > static_cast<std::uint8_t>(ir::Condition::Al)) {
        throw std::runtime_error("ARM32 unconditional-extension encoding is unsupported");
    }
    return static_cast<ir::Condition>(value);
}

ir::Condition invert(ir::Condition condition) {
    return static_cast<ir::Condition>(static_cast<std::uint8_t>(condition) ^ 1u);
}

std::uint8_t map_register(std::uint8_t reg) {
    if (reg <= 12) return reg;
    if (reg == 13) return ir::kStackPointerRegister;
    if (reg == 14) return 30;
    throw std::runtime_error("ARM32 PC operands are not supported by this instruction");
}

bool needs_32bit_normalization(Mnemonic mnemonic) {
    switch (mnemonic) {
    case Mnemonic::AddImm:
    case Mnemonic::SubImm:
    case Mnemonic::MovReg:
    case Mnemonic::AddReg:
    case Mnemonic::SubReg:
    case Mnemonic::AndReg:
    case Mnemonic::OrrReg:
    case Mnemonic::EorReg:
        return true;
    default:
        return false;
    }
}

} // namespace

std::optional<DecodedInstruction> Decoder::decode(std::uint32_t word) const {
    const auto condition = condition_of(word);
    if ((word & 0x0ffffff0u) == 0x012fff10u) {
        return DecodedInstruction{Mnemonic::BranchExchange, 0, 0,
                                  static_cast<std::uint8_t>(word & 0xfu), 0, condition};
    }
    if ((word & 0x0e000000u) == 0x0a000000u) {
        auto imm24 = static_cast<std::int32_t>(word & 0x00ffffffu);
        if ((imm24 & 0x00800000) != 0) imm24 |= static_cast<std::int32_t>(0xff000000u);
        return DecodedInstruction{(word & (1u << 24u)) != 0 ? Mnemonic::BranchLink : Mnemonic::Branch,
                                  0, 0, 0, 0, condition, 8 + (imm24 << 2)};
    }
    if ((word & 0x0fc000f0u) == 0x00000090u && (word & (1u << 21u)) == 0) {
        return DecodedInstruction{Mnemonic::Multiply, static_cast<std::uint8_t>((word >> 16u) & 0xfu),
                                  static_cast<std::uint8_t>(word & 0xfu),
                                  static_cast<std::uint8_t>((word >> 8u) & 0xfu), 0, condition};
    }
    if ((word & 0x0c000000u) == 0x04000000u && (word & (1u << 25u)) == 0 &&
        (word & (1u << 24u)) != 0 && (word & (1u << 21u)) == 0) {
        const auto add = (word & (1u << 23u)) != 0;
        const auto offset = static_cast<std::int32_t>(word & 0xfffu) * (add ? 1 : -1);
        DecodedInstruction decoded{(word & (1u << 20u)) != 0 ? Mnemonic::LoadImmOffset : Mnemonic::StoreImmOffset,
                                   static_cast<std::uint8_t>((word >> 12u) & 0xfu),
                                   static_cast<std::uint8_t>((word >> 16u) & 0xfu), 0, 0, condition};
        decoded.branch_offset = offset;
        decoded.mem_width = (word & (1u << 22u)) != 0 ? 1 : 4;
        return decoded;
    }
    if ((word & 0x0c000000u) != 0) return std::nullopt;
    const auto opcode = static_cast<std::uint8_t>((word >> 21u) & 0xfu);
    const auto immediate = (word & (1u << 25u)) != 0;
    const auto rn = static_cast<std::uint8_t>((word >> 16u) & 0xfu);
    const auto rd = static_cast<std::uint8_t>((word >> 12u) & 0xfu);
    if (immediate) {
        const auto imm = ror32(word & 0xffu, ((word >> 8u) & 0xfu) * 2u);
        if ((word & (1u << 20u)) != 0 && opcode != 10) return std::nullopt;
        if (opcode == 4) return DecodedInstruction{Mnemonic::AddImm, rd, rn, 0, imm, condition};
        if (opcode == 2) return DecodedInstruction{Mnemonic::SubImm, rd, rn, 0, imm, condition};
        if (opcode == 13) return DecodedInstruction{Mnemonic::MovImm, rd, 0, 0, imm, condition};
        if (opcode == 10 && (word & (1u << 20u)) != 0)
            return DecodedInstruction{Mnemonic::CompareImm, 0, rn, 0, imm, condition};
        return std::nullopt;
    }
    if ((word & (1u << 20u)) != 0 && opcode != 10) return std::nullopt;
    if ((word & (1u << 4u)) == 0) {
        const auto shift = static_cast<ir::ShiftKind>((word >> 5u) & 3u);
        Mnemonic mnemonic{};
        switch (opcode) {
        case 0: mnemonic = Mnemonic::AndReg; break;
        case 1: mnemonic = Mnemonic::EorReg; break;
        case 2: mnemonic = Mnemonic::SubReg; break;
        case 4: mnemonic = Mnemonic::AddReg; break;
        case 10:
            if ((word & (1u << 20u)) == 0 || ((word >> 7u) & 0x1fu) != 0) return std::nullopt;
            mnemonic = Mnemonic::CompareReg; break;
        case 12: mnemonic = Mnemonic::OrrReg; break;
        case 13: mnemonic = Mnemonic::MovReg; break;
        default: return std::nullopt;
        }
        return DecodedInstruction{mnemonic, rd, rn,
                                  static_cast<std::uint8_t>(word & 0xfu), 0, condition, 0,
                                  shift, static_cast<std::uint8_t>((word >> 7u) & 0x1fu)};
    }
    return std::nullopt;
}

ir::Program Lifter::lift(const std::vector<std::uint32_t>& words) const {
    Decoder decoder;
    ir::Program program;
    std::vector<DecodedInstruction> decoded_words;
    decoded_words.reserve(words.size());
    std::vector<std::size_t> arm_to_ir(words.size() + 1);
    std::size_t ir_count = 0;
    for (std::size_t index = 0; index < words.size(); ++index) {
        const auto decoded = decoder.decode(words[index]);
        if (!decoded) throw std::runtime_error("unsupported ARM32 instruction at index " + std::to_string(index));
        arm_to_ir[index] = ir_count;
        ir_count += 1 + (needs_32bit_normalization(decoded->mnemonic) ? 1 : 0) +
                    (decoded->condition != ir::Condition::Al && decoded->mnemonic != Mnemonic::Branch ? 1 : 0);
        decoded_words.push_back(*decoded);
    }
    arm_to_ir[words.size()] = ir_count;
    for (std::size_t index = 0; index < words.size(); ++index) {
        const auto& decoded = decoded_words[index];
        if (decoded.condition != ir::Condition::Al && decoded.mnemonic != Mnemonic::Branch) {
            program.branch_if(invert(decoded.condition), static_cast<std::uint32_t>(arm_to_ir[index + 1]));
        }
        switch (decoded.mnemonic) {
        case Mnemonic::AddImm: program.add_imm(map_register(decoded.rd), map_register(decoded.rn), decoded.imm); break;
        case Mnemonic::SubImm: program.sub_imm(map_register(decoded.rd), map_register(decoded.rn), decoded.imm); break;
        case Mnemonic::MovImm: program.load_imm(map_register(decoded.rd), decoded.imm); break;
        case Mnemonic::MovReg:
            if (decoded.shift_amount == 0) program.move_reg(map_register(decoded.rd), map_register(decoded.rm));
            else program.shift_reg32(map_register(decoded.rd), map_register(decoded.rm), decoded.shift_kind, decoded.shift_amount);
            break;
        case Mnemonic::AddReg: program.add_reg_shifted(map_register(decoded.rd), map_register(decoded.rn), map_register(decoded.rm), decoded.shift_kind, decoded.shift_amount); break;
        case Mnemonic::SubReg: program.sub_reg_shifted(map_register(decoded.rd), map_register(decoded.rn), map_register(decoded.rm), decoded.shift_kind, decoded.shift_amount); break;
        case Mnemonic::AndReg: program.and_reg_shifted(map_register(decoded.rd), map_register(decoded.rn), map_register(decoded.rm), decoded.shift_kind, decoded.shift_amount); break;
        case Mnemonic::OrrReg: program.or_reg_shifted(map_register(decoded.rd), map_register(decoded.rn), map_register(decoded.rm), decoded.shift_kind, decoded.shift_amount); break;
        case Mnemonic::EorReg: program.xor_reg_shifted(map_register(decoded.rd), map_register(decoded.rn),
                                                       map_register(decoded.rm), decoded.shift_kind, decoded.shift_amount); break;
        case Mnemonic::CompareImm: program.compare_imm(map_register(decoded.rn), decoded.imm); break;
        case Mnemonic::CompareReg: program.compare_reg(map_register(decoded.rn), map_register(decoded.rm)); break;
        case Mnemonic::Multiply: program.mul_add(map_register(decoded.rd), map_register(decoded.rn), map_register(decoded.rm), 0, true, true); break;
        case Mnemonic::LoadImmOffset: program.load_mem(map_register(decoded.rd), map_register(decoded.rn), static_cast<std::uint64_t>(static_cast<std::int64_t>(decoded.branch_offset)), decoded.mem_width); break;
        case Mnemonic::StoreImmOffset: program.store_mem(map_register(decoded.rd), map_register(decoded.rn), static_cast<std::uint64_t>(static_cast<std::int64_t>(decoded.branch_offset)), decoded.mem_width); break;
        case Mnemonic::Branch: {
            const auto byte_target = static_cast<std::int64_t>(index * 4u) + decoded.branch_offset;
            if (byte_target < 0 || (byte_target & 3) != 0 || byte_target / 4 > static_cast<std::int64_t>(words.size()))
                throw std::runtime_error("ARM32 branch target is outside lifted function");
            program.branch_if(decoded.condition, static_cast<std::uint32_t>(arm_to_ir[static_cast<std::size_t>(byte_target / 4)]));
            break;
        }
        case Mnemonic::BranchLink: throw std::runtime_error("ARM32 BL call gates are not implemented");
        case Mnemonic::BranchExchange:
            if (decoded.rm != 14) throw std::runtime_error("only ARM32 BX LR return is supported");
            program.return_reg(0); break;
        }
        if (needs_32bit_normalization(decoded.mnemonic)) {
            program.shift_reg32(map_register(decoded.rd), map_register(decoded.rd), ir::ShiftKind::Lsl, 0);
        }
    }
    program.validate();
    return program;
}

const char* to_string(Mnemonic mnemonic) noexcept {
    switch (mnemonic) {
    case Mnemonic::AddImm: return "ADD";
    case Mnemonic::SubImm: return "SUB";
    case Mnemonic::MovImm:
    case Mnemonic::MovReg: return "MOV";
    case Mnemonic::AddReg: return "ADD";
    case Mnemonic::SubReg: return "SUB";
    case Mnemonic::AndReg: return "AND";
    case Mnemonic::OrrReg: return "ORR";
    case Mnemonic::EorReg: return "EOR";
    case Mnemonic::CompareImm: return "CMP";
    case Mnemonic::CompareReg: return "CMP";
    case Mnemonic::Multiply: return "MUL";
    case Mnemonic::LoadImmOffset: return "LDR";
    case Mnemonic::StoreImmOffset: return "STR";
    case Mnemonic::Branch: return "B";
    case Mnemonic::BranchLink: return "BL";
    case Mnemonic::BranchExchange: return "BX";
    }
    return "unknown";
}

} // namespace myvmp::arm32
