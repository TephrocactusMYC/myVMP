#include "myvmp/vm/vm.hpp"
#include "myvmp/vm/multi_vm.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <sstream>
#include <limits>
#include <stdexcept>
#include <string>

namespace myvmp::vm {
namespace {

std::uint8_t read_u8(const std::vector<std::uint8_t>& bytes, std::size_t& pc) {
    if (pc >= bytes.size()) {
        throw std::runtime_error("VM bytecode read past end at " + std::to_string(pc) + " of " + std::to_string(bytes.size()));
    }
    return bytes[pc++];
}

std::uint64_t read_u64(const std::vector<std::uint8_t>& bytes, std::size_t& pc) {
    if (pc + 8 > bytes.size()) {
        throw std::runtime_error("VM bytecode u64 read past end");
    }
    std::uint64_t value = 0;
    for (int shift = 0; shift < 64; shift += 8) {
        value |= static_cast<std::uint64_t>(bytes[pc++]) << shift;
    }
    return value;
}

void write_mem64(std::vector<std::uint8_t>& memory, std::uint64_t addr, std::uint64_t value) {
    if (addr > memory.size() || 8 > memory.size() - static_cast<std::size_t>(addr)) {
        throw std::runtime_error("VM memory write out of range");
    }
    for (int shift = 0; shift < 64; shift += 8) {
        memory[static_cast<std::size_t>(addr) + static_cast<std::size_t>(shift / 8)] =
            static_cast<std::uint8_t>((value >> shift) & 0xffu);
    }
}

void write_mem(std::vector<std::uint8_t>& memory, std::uint64_t addr, std::uint64_t value, std::uint8_t width) {
    if (width != 1 && width != 2 && width != 4 && width != 8) {
        throw std::runtime_error("VM memory write width is invalid");
    }
    if (addr > memory.size() || width > memory.size() - static_cast<std::size_t>(addr)) {
        throw std::runtime_error("VM memory write out of range");
    }
    for (std::uint8_t index = 0; index < width; ++index) {
        memory[static_cast<std::size_t>(addr) + index] = static_cast<std::uint8_t>((value >> (index * 8)) & 0xffu);
    }
}

std::uint64_t read_mem64(const std::vector<std::uint8_t>& memory, std::uint64_t addr) {
    if (addr > memory.size() || 8 > memory.size() - static_cast<std::size_t>(addr)) {
        throw std::runtime_error("VM memory read out of range");
    }
    std::uint64_t value = 0;
    for (int shift = 0; shift < 64; shift += 8) {
        value |= static_cast<std::uint64_t>(memory[static_cast<std::size_t>(addr) + static_cast<std::size_t>(shift / 8)]) << shift;
    }
    return value;
}

std::uint64_t read_mem(const std::vector<std::uint8_t>& memory, std::uint64_t addr, std::uint8_t width) {
    if (width != 1 && width != 2 && width != 4 && width != 8) {
        throw std::runtime_error("VM memory read width is invalid");
    }
    if (addr > memory.size() || width > memory.size() - static_cast<std::size_t>(addr)) {
        throw std::runtime_error("VM memory read out of range");
    }
    std::uint64_t value = 0;
    for (std::uint8_t index = 0; index < width; ++index) {
        value |= static_cast<std::uint64_t>(memory[static_cast<std::size_t>(addr) + index]) << (index * 8);
    }
    return value;
}

std::uint32_t read_u32(const std::vector<std::uint8_t>& bytes, std::size_t& pc) {
    if (pc + 4 > bytes.size()) {
        throw std::runtime_error("VM bytecode u32 read past end");
    }
    std::uint32_t value = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        value |= static_cast<std::uint32_t>(bytes[pc++]) << shift;
    }
    return value;
}

void check_reg(std::uint8_t reg) {
    if (reg >= ir::kVirtualRegisterCount) {
        throw std::runtime_error("VM register index out of range");
    }
}

void check_vector_reg(std::uint8_t reg) {
    if (reg >= 32) throw std::runtime_error("VM SIMD register index out of range");
}

void check_vector_width(std::uint8_t width) {
    if (width != 1 && width != 2 && width != 4 && width != 8) {
        throw std::runtime_error("VM SIMD lane width is invalid");
    }
}

template <typename Lane, typename Operation>
ir::Vector128 vector_map(const ir::Vector128& lhs, const ir::Vector128& rhs,
                         Operation operation) {
    return ir::Vector128::map_binary<Lane>(lhs, rhs, operation);
}

template <typename Lane, typename Operation>
ir::Vector128 vector_map_unary(const ir::Vector128& source, Operation operation) {
    ir::Vector128 result;
    for (std::size_t index = 0; index < ir::Vector128::kBytes / sizeof(Lane); ++index) {
        result.set_lane<Lane>(index, operation(source.lane<Lane>(index)));
    }
    return result;
}

template <typename Float, typename Bits, typename Operation>
ir::Vector128 vector_fp_map(const ir::Vector128& lhs, const ir::Vector128& rhs,
                            Operation operation) {
    ir::Vector128 result;
    for (std::size_t index = 0; index < ir::Vector128::kBytes / sizeof(Float); ++index) {
        const auto left = std::bit_cast<Float>(lhs.lane<Bits>(index));
        const auto right = std::bit_cast<Float>(rhs.lane<Bits>(index));
        result.set_lane<Bits>(index, std::bit_cast<Bits>(operation(left, right)));
    }
    return result;
}

bool test_condition(const Flags& flags, ir::Condition condition) {
    switch (condition) {
    case ir::Condition::Eq:
        return flags.z;
    case ir::Condition::Ne:
        return !flags.z;
    case ir::Condition::Hs:
        return flags.c;
    case ir::Condition::Lo:
        return !flags.c;
    case ir::Condition::Mi:
        return flags.n;
    case ir::Condition::Pl:
        return !flags.n;
    case ir::Condition::Vs:
        return flags.v;
    case ir::Condition::Vc:
        return !flags.v;
    case ir::Condition::Hi:
        return flags.c && !flags.z;
    case ir::Condition::Ls:
        return !flags.c || flags.z;
    case ir::Condition::Ge:
        return flags.n == flags.v;
    case ir::Condition::Lt:
        return flags.n != flags.v;
    case ir::Condition::Gt:
        return !flags.z && (flags.n == flags.v);
    case ir::Condition::Le:
        return flags.z || (flags.n != flags.v);
    case ir::Condition::Al:
    case ir::Condition::Nv:
        return true;
    }
    return false;
}

Flags subtract_flags(std::uint64_t lhs, std::uint64_t rhs) {
    const auto result = lhs - rhs;
    const auto lhs_sign = (lhs >> 63) != 0;
    const auto rhs_sign = (rhs >> 63) != 0;
    const auto result_sign = (result >> 63) != 0;
    return Flags{
        result == 0,
        result_sign,
        lhs >= rhs,
        (lhs_sign != rhs_sign) && (result_sign != lhs_sign),
    };
}

Flags subtract_flags32(std::uint32_t lhs, std::uint32_t rhs) {
    const auto result = static_cast<std::uint32_t>(lhs - rhs);
    const auto lhs_sign = (lhs >> 31) != 0;
    const auto rhs_sign = (rhs >> 31) != 0;
    const auto result_sign = (result >> 31) != 0;
    return Flags{
        result == 0,
        result_sign,
        lhs >= rhs,
        (lhs_sign != rhs_sign) && (result_sign != lhs_sign),
    };
}

std::uint32_t apply_shift32(std::uint32_t value, ir::ShiftKind kind, std::uint8_t amount) {
    switch (kind) {
    case ir::ShiftKind::Lsl:
        return static_cast<std::uint32_t>(value << amount);
    case ir::ShiftKind::Lsr:
        return static_cast<std::uint32_t>(value >> amount);
    case ir::ShiftKind::Asr:
        return static_cast<std::uint32_t>(static_cast<std::int32_t>(value) >> amount);
    case ir::ShiftKind::Ror:
        if (amount == 0) {
            return value;
        }
        return static_cast<std::uint32_t>((value >> amount) | (value << (32 - amount)));
    }
    return value;
}

Flags add_flags(std::uint64_t lhs, std::uint64_t rhs) {
    const auto result = lhs + rhs;
    const auto lhs_sign = (lhs >> 63) != 0;
    const auto rhs_sign = (rhs >> 63) != 0;
    const auto result_sign = (result >> 63) != 0;
    return Flags{
        result == 0,
        result_sign,
        result < lhs,
        (lhs_sign == rhs_sign) && (result_sign != lhs_sign),
    };
}

Flags add_flags32(std::uint32_t lhs, std::uint32_t rhs) {
    const auto result = static_cast<std::uint32_t>(lhs + rhs);
    const auto lhs_sign = (lhs >> 31) != 0;
    const auto rhs_sign = (rhs >> 31) != 0;
    const auto result_sign = (result >> 31) != 0;
    return Flags{
        result == 0,
        result_sign,
        result < lhs,
        (lhs_sign == rhs_sign) && (result_sign != lhs_sign),
    };
}

std::uint64_t mul_high_u64(std::uint64_t lhs, std::uint64_t rhs) {
    const auto lhs_lo = lhs & 0xffffffffull;
    const auto lhs_hi = lhs >> 32;
    const auto rhs_lo = rhs & 0xffffffffull;
    const auto rhs_hi = rhs >> 32;

    const auto p0 = lhs_lo * rhs_lo;
    const auto p1 = lhs_lo * rhs_hi;
    const auto p2 = lhs_hi * rhs_lo;
    const auto p3 = lhs_hi * rhs_hi;

    const auto carry = ((p0 >> 32) + (p1 & 0xffffffffull) + (p2 & 0xffffffffull)) >> 32;
    return p3 + (p1 >> 32) + (p2 >> 32) + carry;
}

std::uint64_t select_operand(const std::array<std::uint64_t, ir::kVirtualRegisterCount>& regs,
                             std::uint8_t reg,
                             bool is_zero) {
    check_reg(reg);
    return is_zero ? 0 : regs[reg];
}

} // namespace

Result Interpreter::execute(const bytecode::Image& image) {
    std::array<std::uint64_t, 32> initial_registers{};
    initial_registers.fill(0);
    std::vector<std::uint8_t> memory(4096);
    return execute(image, initial_registers, memory);
}

Result Interpreter::execute(const SiblingVmImage& image) {
    return execute(normalize_sibling_image(image, bytecode::fixed_profile()));
}

Result Interpreter::execute(const bytecode::Image& image, const std::array<std::uint64_t, 32>& initial_registers) {
    std::vector<std::uint8_t> memory(4096);
    return execute(image, initial_registers, memory);
}

Result Interpreter::execute(const bytecode::Image& image,
                            const std::array<std::uint64_t, 32>& initial_registers,
                            std::vector<std::uint8_t>& memory) {
    return execute(image, initial_registers, {}, memory);
}

Result Interpreter::execute(const bytecode::Image& image,
                            const std::array<std::uint64_t, 32>& initial_registers,
                            const ir::VectorRegisterFile& initial_vectors,
                            std::vector<std::uint8_t>& memory) {
    regs_.fill(0);
    fp_regs_.fill(0);
    vector_regs_ = initial_vectors;
    std::copy(initial_registers.begin(), initial_registers.end(), regs_.begin());
    flags_ = {};
    const auto bytes = bytecode::decode_payload(image);
    std::size_t pc = 0;
    std::vector<std::size_t> instruction_offsets;
    for (std::size_t scan = 0; scan < bytes.size();) {
        instruction_offsets.push_back(scan);
        const auto physical_opcode = read_u8(bytes, scan);
        bytecode::Opcode opcode{};
        try {
            opcode = image.profile.logical(physical_opcode);
        } catch (const std::exception&) {
            std::ostringstream message;
            message << "unknown VM opcode while scanning at byte " << scan << ": 0x" << std::hex << static_cast<unsigned>(physical_opcode);
            throw std::runtime_error(message.str());
        }
        const auto operands = bytecode::operand_size(opcode);
        if (scan > bytes.size() || operands > bytes.size() - scan) {
            throw std::runtime_error("VM bytecode operand is truncated while scanning");
        }
        scan += operands;
        if (opcode == bytecode::Opcode::Halt) {
            if (!image.branch_targets_are_offsets) {
                if (scan != bytes.size()) throw std::runtime_error("VM bytecode continues after Halt");
                break;
            }
        }
    }
    Result result{};
    std::vector<std::size_t> vm_call_stack;
    const auto resolve_target = [&](std::uint32_t target) {
        if (image.branch_targets_are_offsets) {
            if (!std::binary_search(instruction_offsets.begin(), instruction_offsets.end(), target)) {
                throw std::runtime_error("VM byte-offset target is not an instruction boundary");
            }
            return static_cast<std::size_t>(target);
        }
        if (target >= instruction_offsets.size()) throw std::runtime_error("VM instruction target out of range");
        return instruction_offsets[target];
    };

    while (pc < bytes.size()) {
        regs_[ir::kZeroRegister] = 0;
        const auto physical_opcode = read_u8(bytes, pc);
        bytecode::Opcode opcode{};
        try {
            opcode = image.profile.logical(physical_opcode);
        } catch (const std::exception&) {
            std::ostringstream message;
            message << "unknown VM opcode at byte " << pc << ": 0x" << std::hex << static_cast<unsigned>(physical_opcode);
            throw std::runtime_error(message.str());
        }
        ++result.instructions_executed;

        switch (opcode) {
        case bytecode::Opcode::VectorMoveImmZero: {
            const auto dst = read_u8(bytes, pc);
            check_vector_reg(dst);
            vector_regs_[dst] = {};
            break;
        }
        case bytecode::Opcode::VectorBinary: {
            const auto dst = read_u8(bytes, pc), lhs = read_u8(bytes, pc), rhs = read_u8(bytes, pc);
            const auto operation = read_u8(bytes, pc), width = read_u8(bytes, pc);
            check_vector_reg(dst); check_vector_reg(lhs); check_vector_reg(rhs); check_vector_width(width);
            if (operation > static_cast<std::uint8_t>(ir::VectorBinaryOperation::Xor)) {
                throw std::runtime_error("VM SIMD binary operation is invalid");
            }
            vector_regs_[dst] = ir::Vector128::apply(
                static_cast<ir::VectorBinaryOperation>(operation),
                static_cast<ir::VectorLaneWidth>(width), vector_regs_[lhs], vector_regs_[rhs]);
            break;
        }
        case bytecode::Opcode::VectorCompare: {
            const auto dst = read_u8(bytes, pc), lhs = read_u8(bytes, pc), rhs = read_u8(bytes, pc);
            const auto operation = read_u8(bytes, pc), width = read_u8(bytes, pc);
            check_vector_reg(dst); check_vector_reg(lhs); check_vector_reg(rhs); check_vector_width(width);
            if (operation > 1) throw std::runtime_error("VM SIMD compare operation is invalid");
            const auto compare = [operation](auto left, auto right) {
                using Lane = decltype(left);
                bool matches = false;
                if (operation == 0) matches = left == right;
                if (operation == 1) matches = static_cast<std::make_signed_t<Lane>>(left) > static_cast<std::make_signed_t<Lane>>(right);
                return matches ? std::numeric_limits<Lane>::max() : Lane{0};
            };
            switch (width) {
            case 1: vector_regs_[dst] = vector_map<std::uint8_t>(vector_regs_[lhs], vector_regs_[rhs], compare); break;
            case 2: vector_regs_[dst] = vector_map<std::uint16_t>(vector_regs_[lhs], vector_regs_[rhs], compare); break;
            case 4: vector_regs_[dst] = vector_map<std::uint32_t>(vector_regs_[lhs], vector_regs_[rhs], compare); break;
            case 8: vector_regs_[dst] = vector_map<std::uint64_t>(vector_regs_[lhs], vector_regs_[rhs], compare); break;
            default: throw std::runtime_error("VM SIMD lane width is invalid");
            }
            break;
        }
        case bytecode::Opcode::VectorSelect: {
            const auto dst = read_u8(bytes, pc), lhs = read_u8(bytes, pc), rhs = read_u8(bytes, pc);
            const auto mask = read_u8(bytes, pc);
            check_vector_reg(dst); check_vector_reg(lhs); check_vector_reg(rhs); check_vector_reg(mask);
            std::array<std::uint8_t, ir::Vector128::kBytes> selected{};
            for (std::size_t index = 0; index < selected.size(); ++index) {
                const auto bits = vector_regs_[mask].bytes()[index];
                selected[index] = static_cast<std::uint8_t>((bits & vector_regs_[lhs].bytes()[index]) |
                    (static_cast<std::uint8_t>(~bits) & vector_regs_[rhs].bytes()[index]));
            }
            vector_regs_[dst] = ir::Vector128{selected};
            break;
        }
        case bytecode::Opcode::VectorShiftImm: {
            const auto dst = read_u8(bytes, pc), src = read_u8(bytes, pc), kind = read_u8(bytes, pc);
            const auto amount = read_u8(bytes, pc), width = read_u8(bytes, pc);
            check_vector_reg(dst); check_vector_reg(src); check_vector_width(width);
            const auto lane_bits = static_cast<std::uint8_t>(width * 8);
            if (kind > 1 || amount > lane_bits ||
                (kind == 0 && amount == lane_bits) || (kind == 1 && amount == 0)) {
                throw std::runtime_error("VM SIMD shift operand is invalid");
            }
            const auto shift = [kind, amount, lane_bits](auto value) {
                using Lane = decltype(value);
                if (kind == 1 && amount == lane_bits) return static_cast<Lane>(0);
                if (kind == 0) return static_cast<Lane>(value << amount);
                if (kind == 1) return static_cast<Lane>(value >> amount);
                return static_cast<Lane>(value >> amount);
            };
            switch (width) {
            case 1: vector_regs_[dst] = vector_map_unary<std::uint8_t>(vector_regs_[src], shift); break;
            case 2: vector_regs_[dst] = vector_map_unary<std::uint16_t>(vector_regs_[src], shift); break;
            case 4: vector_regs_[dst] = vector_map_unary<std::uint32_t>(vector_regs_[src], shift); break;
            case 8: vector_regs_[dst] = vector_map_unary<std::uint64_t>(vector_regs_[src], shift); break;
            default: throw std::runtime_error("VM SIMD lane width is invalid");
            }
            break;
        }
        case bytecode::Opcode::VectorPermute: {
            const auto dst = read_u8(bytes, pc), lhs = read_u8(bytes, pc), rhs = read_u8(bytes, pc);
            const auto control = read_u8(bytes, pc);
            check_vector_reg(dst); check_vector_reg(lhs); check_vector_reg(rhs);
            const auto operation = control >> 5u;
            const auto index = control & 0x1fu;
            if (operation != 0 || index >= 16) throw std::runtime_error("VM SIMD permute operand is invalid");
            std::array<std::uint8_t, ir::Vector128::kBytes> output{};
            for (std::size_t byte = 0; byte < output.size(); ++byte) {
                const auto source_index = byte + index;
                output[byte] = source_index < 16 ? vector_regs_[lhs].bytes()[source_index]
                                                 : vector_regs_[rhs].bytes()[source_index - 16];
            }
            vector_regs_[dst] = ir::Vector128{output};
            break;
        }
        case bytecode::Opcode::VectorLoad:
        case bytecode::Opcode::VectorStore: {
            const auto vector_reg = read_u8(bytes, pc), base = read_u8(bytes, pc);
            const auto offset = read_u64(bytes, pc);
            const auto width = read_u8(bytes, pc);
            check_vector_reg(vector_reg); check_reg(base);
            if (width != 16) throw std::runtime_error("VM SIMD memory width is invalid");
            const auto address = regs_[base] + offset;
            if (address > memory.size() || width > memory.size() - static_cast<std::size_t>(address)) {
                throw std::runtime_error("VM SIMD memory access out of range");
            }
            if (opcode == bytecode::Opcode::VectorLoad) {
                std::array<std::uint8_t, ir::Vector128::kBytes> loaded{};
                std::copy_n(memory.begin() + static_cast<std::ptrdiff_t>(address), loaded.size(), loaded.begin());
                vector_regs_[vector_reg] = ir::Vector128{loaded};
            } else {
                std::copy(vector_regs_[vector_reg].bytes().begin(), vector_regs_[vector_reg].bytes().end(),
                          memory.begin() + static_cast<std::ptrdiff_t>(address));
            }
            break;
        }
        case bytecode::Opcode::VectorFpBinary: {
            const auto dst = read_u8(bytes, pc), lhs = read_u8(bytes, pc), rhs = read_u8(bytes, pc);
            const auto operation = read_u8(bytes, pc), width = read_u8(bytes, pc);
            check_vector_reg(dst); check_vector_reg(lhs); check_vector_reg(rhs);
            if (operation > 3 || (width != 4 && width != 8)) throw std::runtime_error("VM SIMD FP operand is invalid");
            const auto arithmetic = [operation](auto left, auto right) {
                if (operation == 0) return left + right;
                if (operation == 1) return left - right;
                if (operation == 2) return left * right;
                return left / right;
            };
            vector_regs_[dst] = width == 4
                ? vector_fp_map<float, std::uint32_t>(vector_regs_[lhs], vector_regs_[rhs], arithmetic)
                : vector_fp_map<double, std::uint64_t>(vector_regs_[lhs], vector_regs_[rhs], arithmetic);
            break;
        }
        case bytecode::Opcode::LoadImm64: {
            const auto dst = read_u8(bytes, pc);
            check_reg(dst);
            regs_[dst] = read_u64(bytes, pc);
            break;
        }
        case bytecode::Opcode::LoadAddress64: {
            const auto dst = read_u8(bytes, pc);
            check_reg(dst);
            regs_[dst] = read_u64(bytes, pc);
            break;
        }
        case bytecode::Opcode::AddImm64: {
            const auto dst = read_u8(bytes, pc);
            const auto src = read_u8(bytes, pc);
            check_reg(dst);
            check_reg(src);
            regs_[dst] = regs_[src] + read_u64(bytes, pc);
            break;
        }
        case bytecode::Opcode::SubImm64: {
            const auto dst = read_u8(bytes, pc);
            const auto src = read_u8(bytes, pc);
            check_reg(dst);
            check_reg(src);
            regs_[dst] = regs_[src] - read_u64(bytes, pc);
            break;
        }
        case bytecode::Opcode::AndImm64: {
            const auto dst = read_u8(bytes, pc);
            const auto src = read_u8(bytes, pc);
            check_reg(dst);
            check_reg(src);
            regs_[dst] = regs_[src] & read_u64(bytes, pc);
            break;
        }
        case bytecode::Opcode::OrImm64: {
            const auto dst = read_u8(bytes, pc);
            const auto src = read_u8(bytes, pc);
            check_reg(dst);
            check_reg(src);
            regs_[dst] = regs_[src] | read_u64(bytes, pc);
            break;
        }
        case bytecode::Opcode::XorImm64: {
            const auto dst = read_u8(bytes, pc);
            const auto src = read_u8(bytes, pc);
            check_reg(dst);
            check_reg(src);
            regs_[dst] = regs_[src] ^ read_u64(bytes, pc);
            break;
        }
        case bytecode::Opcode::AddImmSetFlags64: {
            const auto dst = read_u8(bytes, pc);
            const auto src = read_u8(bytes, pc);
            check_reg(dst);
            check_reg(src);
            const auto rhs = read_u64(bytes, pc);
            const auto width = read_u8(bytes, pc);
            std::uint64_t value = 0;
            if (width == 4) {
                const auto lhs32 = static_cast<std::uint32_t>(regs_[src]);
                const auto rhs32 = static_cast<std::uint32_t>(rhs);
                value = static_cast<std::uint32_t>(lhs32 + rhs32);
                flags_ = add_flags32(lhs32, rhs32);
            } else if (width == 8) {
                value = regs_[src] + rhs;
                flags_ = add_flags(regs_[src], rhs);
            } else {
                throw std::runtime_error("VM add-set-flags width is invalid");
            }
            if (dst < 31) {
                regs_[dst] = value;
            }
            break;
        }
        case bytecode::Opcode::SubImmSetFlags64: {
            const auto dst = read_u8(bytes, pc);
            const auto src = read_u8(bytes, pc);
            check_reg(dst);
            check_reg(src);
            const auto rhs = read_u64(bytes, pc);
            const auto width = read_u8(bytes, pc);
            std::uint64_t value = 0;
            if (width == 4) {
                const auto lhs32 = static_cast<std::uint32_t>(regs_[src]);
                const auto rhs32 = static_cast<std::uint32_t>(rhs);
                value = static_cast<std::uint32_t>(lhs32 - rhs32);
                flags_ = subtract_flags32(lhs32, rhs32);
            } else if (width == 8) {
                value = regs_[src] - rhs;
                flags_ = subtract_flags(regs_[src], rhs);
            } else {
                throw std::runtime_error("VM sub-set-flags width is invalid");
            }
            if (dst < 31) {
                regs_[dst] = value;
            }
            break;
        }
        case bytecode::Opcode::AddRegShifted64: {
            const auto dst = read_u8(bytes, pc);
            const auto lhs = read_u8(bytes, pc);
            const auto rhs = read_u8(bytes, pc);
            const auto shift_kind = static_cast<ir::ShiftKind>(read_u8(bytes, pc));
            const auto shift_amount = read_u8(bytes, pc);
            check_reg(dst);
            check_reg(lhs);
            check_reg(rhs);
            regs_[dst] = regs_[lhs] + ir::apply_shift(regs_[rhs], shift_kind, shift_amount);
            break;
        }
        case bytecode::Opcode::AddRegShiftedSetFlags64: {
            const auto dst = read_u8(bytes, pc);
            const auto lhs = read_u8(bytes, pc);
            const auto rhs = read_u8(bytes, pc);
            const auto shift_kind = static_cast<ir::ShiftKind>(read_u8(bytes, pc));
            const auto shift_amount = read_u8(bytes, pc);
            const auto width = read_u8(bytes, pc);
            check_reg(dst);
            check_reg(lhs);
            check_reg(rhs);
            std::uint64_t value = 0;
            if (width == 4) {
                const auto lhs32 = static_cast<std::uint32_t>(regs_[lhs]);
                const auto shifted = apply_shift32(static_cast<std::uint32_t>(regs_[rhs]), shift_kind, shift_amount);
                value = static_cast<std::uint32_t>(lhs32 + shifted);
                flags_ = add_flags32(lhs32, shifted);
            } else if (width == 8) {
                const auto shifted = ir::apply_shift(regs_[rhs], shift_kind, shift_amount);
                value = regs_[lhs] + shifted;
                flags_ = add_flags(regs_[lhs], shifted);
            } else {
                throw std::runtime_error("VM add-shifted-set-flags width is invalid");
            }
            if (dst < 31) {
                regs_[dst] = value;
            }
            break;
        }
        case bytecode::Opcode::AddRegExtended64: {
            const auto dst = read_u8(bytes, pc);
            const auto lhs = read_u8(bytes, pc);
            const auto rhs = read_u8(bytes, pc);
            const auto extend_kind = static_cast<ir::ExtendKind>(read_u8(bytes, pc));
            const auto shift_amount = read_u8(bytes, pc);
            check_reg(dst);
            check_reg(lhs);
            check_reg(rhs);
            regs_[dst] = regs_[lhs] + (ir::apply_extend(regs_[rhs], extend_kind) << shift_amount);
            break;
        }
        case bytecode::Opcode::AddRegExtendedSetFlags64: {
            const auto dst = read_u8(bytes, pc);
            const auto lhs = read_u8(bytes, pc);
            const auto rhs = read_u8(bytes, pc);
            const auto extend_kind = static_cast<ir::ExtendKind>(read_u8(bytes, pc));
            const auto shift_amount = read_u8(bytes, pc);
            const auto width = read_u8(bytes, pc);
            check_reg(dst);
            check_reg(lhs);
            check_reg(rhs);
            const auto extended = ir::apply_extend(regs_[rhs], extend_kind) << shift_amount;
            std::uint64_t value = 0;
            if (width == 4) {
                const auto lhs32 = static_cast<std::uint32_t>(regs_[lhs]);
                const auto rhs32 = static_cast<std::uint32_t>(extended);
                value = static_cast<std::uint32_t>(lhs32 + rhs32);
                flags_ = add_flags32(lhs32, rhs32);
            } else if (width == 8) {
                value = regs_[lhs] + extended;
                flags_ = add_flags(regs_[lhs], extended);
            } else {
                throw std::runtime_error("VM add-extended-set-flags width is invalid");
            }
            if (dst < 31) {
                regs_[dst] = value;
            }
            break;
        }
        case bytecode::Opcode::SubRegShifted64: {
            const auto dst = read_u8(bytes, pc);
            const auto lhs = read_u8(bytes, pc);
            const auto rhs = read_u8(bytes, pc);
            const auto shift_kind = static_cast<ir::ShiftKind>(read_u8(bytes, pc));
            const auto shift_amount = read_u8(bytes, pc);
            check_reg(dst);
            check_reg(lhs);
            check_reg(rhs);
            regs_[dst] = regs_[lhs] - ir::apply_shift(regs_[rhs], shift_kind, shift_amount);
            break;
        }
        case bytecode::Opcode::SubRegShiftedSetFlags64: {
            const auto dst = read_u8(bytes, pc);
            const auto lhs = read_u8(bytes, pc);
            const auto rhs = read_u8(bytes, pc);
            const auto shift_kind = static_cast<ir::ShiftKind>(read_u8(bytes, pc));
            const auto shift_amount = read_u8(bytes, pc);
            const auto width = read_u8(bytes, pc);
            check_reg(dst);
            check_reg(lhs);
            check_reg(rhs);
            std::uint64_t value = 0;
            if (width == 4) {
                const auto lhs32 = static_cast<std::uint32_t>(regs_[lhs]);
                const auto shifted = apply_shift32(static_cast<std::uint32_t>(regs_[rhs]), shift_kind, shift_amount);
                value = static_cast<std::uint32_t>(lhs32 - shifted);
                flags_ = subtract_flags32(lhs32, shifted);
            } else if (width == 8) {
                const auto shifted = ir::apply_shift(regs_[rhs], shift_kind, shift_amount);
                value = regs_[lhs] - shifted;
                flags_ = subtract_flags(regs_[lhs], shifted);
            } else {
                throw std::runtime_error("VM sub-shifted-set-flags width is invalid");
            }
            if (dst < 31) {
                regs_[dst] = value;
            }
            break;
        }
        case bytecode::Opcode::SubRegExtended64: {
            const auto dst = read_u8(bytes, pc);
            const auto lhs = read_u8(bytes, pc);
            const auto rhs = read_u8(bytes, pc);
            const auto extend_kind = static_cast<ir::ExtendKind>(read_u8(bytes, pc));
            const auto shift_amount = read_u8(bytes, pc);
            check_reg(dst);
            check_reg(lhs);
            check_reg(rhs);
            regs_[dst] = regs_[lhs] - (ir::apply_extend(regs_[rhs], extend_kind) << shift_amount);
            break;
        }
        case bytecode::Opcode::SubRegExtendedSetFlags64: {
            const auto dst = read_u8(bytes, pc);
            const auto lhs = read_u8(bytes, pc);
            const auto rhs = read_u8(bytes, pc);
            const auto extend_kind = static_cast<ir::ExtendKind>(read_u8(bytes, pc));
            const auto shift_amount = read_u8(bytes, pc);
            const auto width = read_u8(bytes, pc);
            check_reg(dst);
            check_reg(lhs);
            check_reg(rhs);
            const auto extended = ir::apply_extend(regs_[rhs], extend_kind) << shift_amount;
            std::uint64_t value = 0;
            if (width == 4) {
                const auto lhs32 = static_cast<std::uint32_t>(regs_[lhs]);
                const auto rhs32 = static_cast<std::uint32_t>(extended);
                value = static_cast<std::uint32_t>(lhs32 - rhs32);
                flags_ = subtract_flags32(lhs32, rhs32);
            } else if (width == 8) {
                value = regs_[lhs] - extended;
                flags_ = subtract_flags(regs_[lhs], extended);
            } else {
                throw std::runtime_error("VM sub-extended-set-flags width is invalid");
            }
            if (dst < 31) {
                regs_[dst] = value;
            }
            break;
        }
        case bytecode::Opcode::NegReg64: {
            const auto dst = read_u8(bytes, pc);
            const auto src = read_u8(bytes, pc);
            const auto shift_kind = static_cast<ir::ShiftKind>(read_u8(bytes, pc));
            const auto shift_amount = read_u8(bytes, pc);
            const auto width = read_u8(bytes, pc);
            check_reg(dst);
            check_reg(src);
            if (width == 4) {
                const auto rhs = apply_shift32(static_cast<std::uint32_t>(regs_[src]), shift_kind, shift_amount);
                regs_[dst] = static_cast<std::uint32_t>(0u - rhs);
            } else if (width == 8) {
                regs_[dst] = 0 - ir::apply_shift(regs_[src], shift_kind, shift_amount);
            } else {
                throw std::runtime_error("VM neg width is invalid");
            }
            break;
        }
        case bytecode::Opcode::NegRegSetFlags64: {
            const auto dst = read_u8(bytes, pc);
            const auto src = read_u8(bytes, pc);
            const auto shift_kind = static_cast<ir::ShiftKind>(read_u8(bytes, pc));
            const auto shift_amount = read_u8(bytes, pc);
            const auto width = read_u8(bytes, pc);
            check_reg(dst);
            check_reg(src);
            if (width == 4) {
                const auto rhs = apply_shift32(static_cast<std::uint32_t>(regs_[src]), shift_kind, shift_amount);
                const auto value = static_cast<std::uint32_t>(0u - rhs);
                flags_ = subtract_flags32(0, rhs);
                if (dst < 31) {
                    regs_[dst] = value;
                }
            } else if (width == 8) {
                const auto rhs = ir::apply_shift(regs_[src], shift_kind, shift_amount);
                const auto value = 0 - rhs;
                flags_ = subtract_flags(0, rhs);
                if (dst < 31) {
                    regs_[dst] = value;
                }
            } else {
                throw std::runtime_error("VM neg-set-flags width is invalid");
            }
            break;
        }
        case bytecode::Opcode::AndRegShifted64: {
            const auto dst = read_u8(bytes, pc);
            const auto lhs = read_u8(bytes, pc);
            const auto rhs = read_u8(bytes, pc);
            const auto shift_kind = static_cast<ir::ShiftKind>(read_u8(bytes, pc));
            const auto shift_amount = read_u8(bytes, pc);
            check_reg(dst);
            check_reg(lhs);
            check_reg(rhs);
            regs_[dst] = regs_[lhs] & ir::apply_shift(regs_[rhs], shift_kind, shift_amount);
            break;
        }
        case bytecode::Opcode::BitClearRegShifted64: {
            const auto dst = read_u8(bytes, pc);
            const auto lhs = read_u8(bytes, pc);
            const auto rhs = read_u8(bytes, pc);
            const auto shift_kind = static_cast<ir::ShiftKind>(read_u8(bytes, pc));
            const auto shift_amount = read_u8(bytes, pc);
            check_reg(dst);
            check_reg(lhs);
            check_reg(rhs);
            regs_[dst] = regs_[lhs] & ~ir::apply_shift(regs_[rhs], shift_kind, shift_amount);
            break;
        }
        case bytecode::Opcode::NotRegShifted64: {
            const auto dst = read_u8(bytes, pc);
            const auto src = read_u8(bytes, pc);
            const auto shift_kind = static_cast<ir::ShiftKind>(read_u8(bytes, pc));
            const auto shift_amount = read_u8(bytes, pc);
            const auto width = read_u8(bytes, pc);
            check_reg(dst);
            check_reg(src);
            if (width == 4) {
                const auto shifted = apply_shift32(static_cast<std::uint32_t>(regs_[src]), shift_kind, shift_amount);
                regs_[dst] = static_cast<std::uint32_t>(~shifted);
            } else if (width == 8) {
                regs_[dst] = ~ir::apply_shift(regs_[src], shift_kind, shift_amount);
            } else {
                throw std::runtime_error("VM not width is invalid");
            }
            break;
        }
        case bytecode::Opcode::OrRegShifted64: {
            const auto dst = read_u8(bytes, pc);
            const auto lhs = read_u8(bytes, pc);
            const auto rhs = read_u8(bytes, pc);
            const auto shift_kind = static_cast<ir::ShiftKind>(read_u8(bytes, pc));
            const auto shift_amount = read_u8(bytes, pc);
            check_reg(dst);
            check_reg(lhs);
            check_reg(rhs);
            regs_[dst] = regs_[lhs] | ir::apply_shift(regs_[rhs], shift_kind, shift_amount);
            break;
        }
        case bytecode::Opcode::OrNotRegShifted64: {
            const auto dst = read_u8(bytes, pc);
            const auto lhs = read_u8(bytes, pc);
            const auto rhs = read_u8(bytes, pc);
            const auto shift_kind = static_cast<ir::ShiftKind>(read_u8(bytes, pc));
            const auto shift_amount = read_u8(bytes, pc);
            check_reg(dst);
            check_reg(lhs);
            check_reg(rhs);
            regs_[dst] = regs_[lhs] | ~ir::apply_shift(regs_[rhs], shift_kind, shift_amount);
            break;
        }
        case bytecode::Opcode::XorRegShifted64: {
            const auto dst = read_u8(bytes, pc);
            const auto lhs = read_u8(bytes, pc);
            const auto rhs = read_u8(bytes, pc);
            const auto shift_kind = static_cast<ir::ShiftKind>(read_u8(bytes, pc));
            const auto shift_amount = read_u8(bytes, pc);
            check_reg(dst);
            check_reg(lhs);
            check_reg(rhs);
            regs_[dst] = regs_[lhs] ^ ir::apply_shift(regs_[rhs], shift_kind, shift_amount);
            break;
        }
        case bytecode::Opcode::XorNotRegShifted64: {
            const auto dst = read_u8(bytes, pc);
            const auto lhs = read_u8(bytes, pc);
            const auto rhs = read_u8(bytes, pc);
            const auto shift_kind = static_cast<ir::ShiftKind>(read_u8(bytes, pc));
            const auto shift_amount = read_u8(bytes, pc);
            check_reg(dst);
            check_reg(lhs);
            check_reg(rhs);
            regs_[dst] = regs_[lhs] ^ ~ir::apply_shift(regs_[rhs], shift_kind, shift_amount);
            break;
        }
        case bytecode::Opcode::ShiftReg64: {
            const auto dst = read_u8(bytes, pc);
            const auto src = read_u8(bytes, pc);
            const auto shift_kind = static_cast<ir::ShiftKind>(read_u8(bytes, pc));
            const auto shift_amount = read_u8(bytes, pc);
            check_reg(dst);
            check_reg(src);
            regs_[dst] = ir::apply_shift(regs_[src], shift_kind, shift_amount);
            break;
        }
        case bytecode::Opcode::ShiftReg32: {
            const auto dst = read_u8(bytes, pc);
            const auto src = read_u8(bytes, pc);
            const auto shift_kind = static_cast<ir::ShiftKind>(read_u8(bytes, pc));
            const auto shift_amount = read_u8(bytes, pc);
            check_reg(dst);
            check_reg(src);
            const auto value32 = static_cast<std::uint32_t>(regs_[src]);
            std::uint32_t shifted{};
            switch (shift_kind) {
            case ir::ShiftKind::Lsl:
                shifted = shift_amount == 0 ? value32 : static_cast<std::uint32_t>(value32 << shift_amount);
                break;
            case ir::ShiftKind::Lsr:
                shifted = shift_amount == 0 ? value32 : static_cast<std::uint32_t>(value32 >> shift_amount);
                break;
            case ir::ShiftKind::Asr:
                shifted = static_cast<std::uint32_t>(static_cast<std::int32_t>(value32) >> shift_amount);
                break;
            case ir::ShiftKind::Ror:
                shifted = shift_amount == 0 ? value32 : static_cast<std::uint32_t>((value32 >> shift_amount) | (value32 << (32u - shift_amount)));
                break;
            }
            regs_[dst] = shifted;
            break;
        }
        case bytecode::Opcode::ShiftRegVariable64: {
            const auto dst = read_u8(bytes, pc);
            const auto src = read_u8(bytes, pc);
            const auto amount_reg = read_u8(bytes, pc);
            const auto shift_kind = static_cast<ir::ShiftKind>(read_u8(bytes, pc));
            const auto width = read_u8(bytes, pc);
            check_reg(dst);
            check_reg(src);
            check_reg(amount_reg);
            if (width == 4) {
                const auto amount = static_cast<std::uint8_t>(regs_[amount_reg] & 31u);
                regs_[dst] = apply_shift32(static_cast<std::uint32_t>(regs_[src]), shift_kind, amount);
            } else if (width == 8) {
                const auto amount = static_cast<std::uint8_t>(regs_[amount_reg] & 63u);
                regs_[dst] = ir::apply_shift(regs_[src], shift_kind, amount);
            } else {
                throw std::runtime_error("VM variable-shift width is invalid");
            }
            break;
        }
        case bytecode::Opcode::DivideReg64: {
            const auto dst = read_u8(bytes, pc);
            const auto lhs = read_u8(bytes, pc);
            const auto rhs = read_u8(bytes, pc);
            const bool is_signed = read_u8(bytes, pc) != 0;
            const auto width = read_u8(bytes, pc);
            check_reg(dst);
            check_reg(lhs);
            check_reg(rhs);
            if (width == 4) {
                const auto divisor = static_cast<std::uint32_t>(regs_[rhs]);
                if (divisor == 0) {
                    regs_[dst] = 0;
                } else if (is_signed) {
                    const auto dividend = static_cast<std::int32_t>(regs_[lhs]);
                    const auto signed_divisor = static_cast<std::int32_t>(divisor);
                    if (dividend == std::numeric_limits<std::int32_t>::min() && signed_divisor == -1) {
                        regs_[dst] = static_cast<std::uint32_t>(dividend);
                    } else {
                        regs_[dst] = static_cast<std::uint32_t>(dividend / signed_divisor);
                    }
                } else {
                    regs_[dst] = static_cast<std::uint32_t>(regs_[lhs]) / divisor;
                }
            } else if (width == 8) {
                const auto divisor = regs_[rhs];
                if (divisor == 0) {
                    regs_[dst] = 0;
                } else if (is_signed) {
                    const auto dividend = static_cast<std::int64_t>(regs_[lhs]);
                    const auto signed_divisor = static_cast<std::int64_t>(divisor);
                    if (dividend == std::numeric_limits<std::int64_t>::min() && signed_divisor == -1) {
                        regs_[dst] = static_cast<std::uint64_t>(dividend);
                    } else {
                        regs_[dst] = static_cast<std::uint64_t>(dividend / signed_divisor);
                    }
                } else {
                    regs_[dst] = regs_[lhs] / divisor;
                }
            } else {
                throw std::runtime_error("VM divide width is invalid");
            }
            break;
        }
        case bytecode::Opcode::BitfieldExtract64: {
            const auto dst = read_u8(bytes, pc);
            const auto src = read_u8(bytes, pc);
            const auto lsb = read_u8(bytes, pc);
            const auto width = read_u8(bytes, pc);
            check_reg(dst);
            check_reg(src);
            const auto mask = width == 64 ? ~0ull : ((1ull << width) - 1ull);
            regs_[dst] = (regs_[src] >> lsb) & mask;
            break;
        }
        case bytecode::Opcode::UnsignedMulHigh64: {
            const auto dst = read_u8(bytes, pc);
            const auto lhs = read_u8(bytes, pc);
            const auto rhs = read_u8(bytes, pc);
            check_reg(dst);
            check_reg(lhs);
            check_reg(rhs);
            regs_[dst] = mul_high_u64(regs_[lhs], regs_[rhs]);
            break;
        }
        case bytecode::Opcode::MulAdd64: {
            const auto dst = read_u8(bytes, pc);
            const auto lhs = read_u8(bytes, pc);
            const auto rhs = read_u8(bytes, pc);
            const auto addend = read_u8(bytes, pc);
            const auto width = read_u8(bytes, pc);
            const auto flags = read_u8(bytes, pc);
            check_reg(dst);
            check_reg(lhs);
            check_reg(rhs);
            check_reg(addend);
            const auto addend_value = (flags & ir::kMulAccumulatorZero) != 0 ? 0 : regs_[addend];
            if (width == 4) {
                regs_[dst] = static_cast<std::uint32_t>(static_cast<std::uint32_t>(regs_[lhs]) *
                                                        static_cast<std::uint32_t>(regs_[rhs]) +
                                                        static_cast<std::uint32_t>(addend_value));
            } else if (width == 8) {
                regs_[dst] = regs_[lhs] * regs_[rhs] + addend_value;
            } else {
                throw std::runtime_error("VM multiply-add width is invalid");
            }
            break;
        }
        case bytecode::Opcode::MulSub64: {
            const auto dst = read_u8(bytes, pc);
            const auto lhs = read_u8(bytes, pc);
            const auto rhs = read_u8(bytes, pc);
            const auto minuend = read_u8(bytes, pc);
            const auto width = read_u8(bytes, pc);
            const auto flags = read_u8(bytes, pc);
            check_reg(dst);
            check_reg(lhs);
            check_reg(rhs);
            check_reg(minuend);
            const auto minuend_value = (flags & ir::kMulAccumulatorZero) != 0 ? 0 : regs_[minuend];
            if (width == 4) {
                regs_[dst] = static_cast<std::uint32_t>(static_cast<std::uint32_t>(minuend_value) -
                                                        static_cast<std::uint32_t>(regs_[lhs]) *
                                                            static_cast<std::uint32_t>(regs_[rhs]));
            } else if (width == 8) {
                regs_[dst] = minuend_value - regs_[lhs] * regs_[rhs];
            } else {
                throw std::runtime_error("VM multiply-sub width is invalid");
            }
            break;
        }
        case bytecode::Opcode::SignedMulLong64: {
            const auto dst = read_u8(bytes, pc);
            const auto lhs = read_u8(bytes, pc);
            const auto rhs = read_u8(bytes, pc);
            check_reg(dst);
            check_reg(lhs);
            check_reg(rhs);
            regs_[dst] = static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(regs_[lhs])) *
                                                    static_cast<std::int64_t>(static_cast<std::int32_t>(regs_[rhs])));
            break;
        }
        case bytecode::Opcode::UnsignedMulLong64: {
            const auto dst = read_u8(bytes, pc);
            const auto lhs = read_u8(bytes, pc);
            const auto rhs = read_u8(bytes, pc);
            check_reg(dst);
            check_reg(lhs);
            check_reg(rhs);
            regs_[dst] = static_cast<std::uint64_t>(static_cast<std::uint32_t>(regs_[lhs])) *
                         static_cast<std::uint64_t>(static_cast<std::uint32_t>(regs_[rhs]));
            break;
        }
        case bytecode::Opcode::MoveKeep64: {
            const auto dst = read_u8(bytes, pc);
            const auto shift_amount = read_u8(bytes, pc);
            const auto imm = read_u64(bytes, pc) & 0xffffu;
            check_reg(dst);
            const auto mask = ~(0xffffull << shift_amount);
            regs_[dst] = (regs_[dst] & mask) | (imm << shift_amount);
            break;
        }
        case bytecode::Opcode::MoveReg64: {
            const auto dst = read_u8(bytes, pc);
            const auto src = read_u8(bytes, pc);
            check_reg(dst);
            check_reg(src);
            regs_[dst] = regs_[src];
            break;
        }
        case bytecode::Opcode::CompareImm64: {
            const auto src = read_u8(bytes, pc);
            check_reg(src);
            const auto value = read_u64(bytes, pc);
            flags_ = subtract_flags(regs_[src], value);
            break;
        }
        case bytecode::Opcode::CompareImmBranch: {
            const auto src = read_u8(bytes, pc);
            check_reg(src);
            const auto value = read_u64(bytes, pc);
            flags_ = subtract_flags(regs_[src], value);
            const auto condition = static_cast<ir::Condition>(read_u8(bytes, pc));
            const auto target = read_u32(bytes, pc);
            if (test_condition(flags_, condition)) {
                pc = resolve_target(target);
            }
            break;
        }
        case bytecode::Opcode::CompareRegBranch: {
            const auto lhs = read_u8(bytes, pc);
            const auto rhs = read_u8(bytes, pc);
            check_reg(lhs); check_reg(rhs);
            flags_ = subtract_flags(regs_[lhs], regs_[rhs]);
            const auto condition = static_cast<ir::Condition>(read_u8(bytes, pc));
            const auto target = read_u32(bytes, pc);
            if (test_condition(flags_, condition)) pc = resolve_target(target);
            break;
        }
        case bytecode::Opcode::TestImmBranch: {
            const auto src = read_u8(bytes, pc);
            check_reg(src);
            const auto value = regs_[src] & read_u64(bytes, pc);
            flags_ = Flags{value == 0, (value >> 63) != 0, false, false};
            const auto condition = static_cast<ir::Condition>(read_u8(bytes, pc));
            const auto target = read_u32(bytes, pc);
            if (test_condition(flags_, condition)) pc = resolve_target(target);
            break;
        }
        case bytecode::Opcode::CompareReg64: {
            const auto lhs = read_u8(bytes, pc);
            const auto rhs = read_u8(bytes, pc);
            check_reg(lhs);
            check_reg(rhs);
            flags_ = subtract_flags(regs_[lhs], regs_[rhs]);
            break;
        }
        case bytecode::Opcode::TestImm64: {
            const auto src = read_u8(bytes, pc);
            check_reg(src);
            const auto value = regs_[src] & read_u64(bytes, pc);
            flags_ = Flags{value == 0, (value >> 63) != 0, false, false};
            break;
        }
        case bytecode::Opcode::TestRegShifted64: {
            const auto lhs = read_u8(bytes, pc);
            const auto rhs = read_u8(bytes, pc);
            const auto shift_kind = static_cast<ir::ShiftKind>(read_u8(bytes, pc));
            const auto shift_amount = read_u8(bytes, pc);
            check_reg(lhs);
            check_reg(rhs);
            const auto value = regs_[lhs] & ir::apply_shift(regs_[rhs], shift_kind, shift_amount);
            flags_ = Flags{value == 0, (value >> 63) != 0, false, false};
            break;
        }
        case bytecode::Opcode::TestRegShifted32: {
            const auto lhs = read_u8(bytes, pc);
            const auto rhs = read_u8(bytes, pc);
            const auto shift_kind = static_cast<ir::ShiftKind>(read_u8(bytes, pc));
            const auto shift_amount = read_u8(bytes, pc);
            check_reg(lhs);
            check_reg(rhs);
            const auto left = static_cast<std::uint32_t>(regs_[lhs]);
            const auto right = static_cast<std::uint32_t>(regs_[rhs]);
            std::uint32_t shifted{};
            switch (shift_kind) {
            case ir::ShiftKind::Lsl:
                shifted = shift_amount == 0 ? right : static_cast<std::uint32_t>(right << shift_amount);
                break;
            case ir::ShiftKind::Lsr:
                shifted = shift_amount == 0 ? right : static_cast<std::uint32_t>(right >> shift_amount);
                break;
            case ir::ShiftKind::Asr:
                shifted = static_cast<std::uint32_t>(static_cast<std::int32_t>(right) >> shift_amount);
                break;
            case ir::ShiftKind::Ror:
                shifted = shift_amount == 0 ? right : static_cast<std::uint32_t>((right >> shift_amount) | (right << (32u - shift_amount)));
                break;
            }
            const auto value = static_cast<std::uint32_t>(left & shifted);
            flags_ = Flags{value == 0, (value >> 31) != 0, false, false};
            break;
        }
        case bytecode::Opcode::BranchIf: {
            const auto condition = static_cast<ir::Condition>(read_u8(bytes, pc));
            const auto target = read_u32(bytes, pc);
            if (test_condition(flags_, condition)) {
                pc = resolve_target(target);
            }
            break;
        }
        case bytecode::Opcode::BranchRegZero: {
            const auto src = read_u8(bytes, pc);
            const auto branch_when_zero = read_u8(bytes, pc) != 0;
            const auto target = read_u32(bytes, pc);
            check_reg(src);
            if ((regs_[src] == 0) == branch_when_zero) {
                pc = resolve_target(target);
            }
            break;
        }
        case bytecode::Opcode::BranchRegBit: {
            const auto src = read_u8(bytes, pc);
            const auto bit = read_u8(bytes, pc);
            const auto branch_when_zero = read_u8(bytes, pc) != 0;
            const auto target = read_u32(bytes, pc);
            check_reg(src);
            if (bit >= 64) {
                throw std::runtime_error("VM branch bit index out of range");
            }
            const bool bit_is_zero = ((regs_[src] >> bit) & 1u) == 0;
            if (bit_is_zero == branch_when_zero) {
                pc = resolve_target(target);
            }
            break;
        }
        case bytecode::Opcode::Select64: {
            const auto dst = read_u8(bytes, pc);
            const auto true_src = read_u8(bytes, pc);
            const auto false_src = read_u8(bytes, pc);
            const auto condition = static_cast<ir::Condition>(read_u8(bytes, pc));
            const auto source_flags = read_u8(bytes, pc);
            check_reg(dst);
            const auto true_value = select_operand(regs_, true_src, (source_flags & ir::kSelectTrueZero) != 0);
            const auto false_value = select_operand(regs_, false_src, (source_flags & ir::kSelectFalseZero) != 0);
            regs_[dst] = test_condition(flags_, condition) ? true_value : false_value;
            break;
        }
        case bytecode::Opcode::SelectInc64: {
            const auto dst = read_u8(bytes, pc);
            const auto true_src = read_u8(bytes, pc);
            const auto false_src = read_u8(bytes, pc);
            const auto condition = static_cast<ir::Condition>(read_u8(bytes, pc));
            const auto source_flags = read_u8(bytes, pc);
            check_reg(dst);
            const auto true_value = select_operand(regs_, true_src, (source_flags & ir::kSelectTrueZero) != 0);
            const auto false_value = select_operand(regs_, false_src, (source_flags & ir::kSelectFalseZero) != 0);
            regs_[dst] = test_condition(flags_, condition) ? true_value : false_value + 1;
            break;
        }
        case bytecode::Opcode::SelectInv64: {
            const auto dst = read_u8(bytes, pc);
            const auto true_src = read_u8(bytes, pc);
            const auto false_src = read_u8(bytes, pc);
            const auto condition = static_cast<ir::Condition>(read_u8(bytes, pc));
            const auto source_flags = read_u8(bytes, pc);
            check_reg(dst);
            const auto true_value = select_operand(regs_, true_src, (source_flags & ir::kSelectTrueZero) != 0);
            const auto false_value = select_operand(regs_, false_src, (source_flags & ir::kSelectFalseZero) != 0);
            regs_[dst] = test_condition(flags_, condition) ? true_value : ~false_value;
            break;
        }
        case bytecode::Opcode::SelectNeg64: {
            const auto dst = read_u8(bytes, pc);
            const auto true_src = read_u8(bytes, pc);
            const auto false_src = read_u8(bytes, pc);
            const auto condition = static_cast<ir::Condition>(read_u8(bytes, pc));
            const auto source_flags = read_u8(bytes, pc);
            check_reg(dst);
            const auto true_value = select_operand(regs_, true_src, (source_flags & ir::kSelectTrueZero) != 0);
            const auto false_value = select_operand(regs_, false_src, (source_flags & ir::kSelectFalseZero) != 0);
            regs_[dst] = test_condition(flags_, condition) ? true_value : 0 - false_value;
            break;
        }
        case bytecode::Opcode::Load64: {
            const auto dst = read_u8(bytes, pc);
            const auto base = read_u8(bytes, pc);
            check_reg(dst);
            check_reg(base);
            regs_[dst] = read_mem64(memory, regs_[base] + read_u64(bytes, pc));
            break;
        }
        case bytecode::Opcode::Store64: {
            const auto src = read_u8(bytes, pc);
            const auto base = read_u8(bytes, pc);
            check_reg(src);
            check_reg(base);
            const auto offset = read_u64(bytes, pc);
            const auto post_index = static_cast<std::int64_t>(read_u64(bytes, pc));
            write_mem64(memory, regs_[base] + offset, regs_[src]);
            regs_[base] = static_cast<std::uint64_t>(static_cast<std::int64_t>(regs_[base]) + post_index);
            break;
        }
        case bytecode::Opcode::LoadMem: {
            const auto dst = read_u8(bytes, pc);
            const auto base = read_u8(bytes, pc);
            const auto width = read_u8(bytes, pc);
            check_reg(dst);
            check_reg(base);
            regs_[dst] = read_mem(memory, regs_[base] + read_u64(bytes, pc), width);
            break;
        }
        case bytecode::Opcode::StoreMem: {
            const auto src = read_u8(bytes, pc);
            const auto base = read_u8(bytes, pc);
            const auto width = read_u8(bytes, pc);
            check_reg(src);
            check_reg(base);
            const auto offset = read_u64(bytes, pc);
            const auto post_index = static_cast<std::int64_t>(read_u64(bytes, pc));
            write_mem(memory, regs_[base] + offset, regs_[src], width);
            regs_[base] = static_cast<std::uint64_t>(static_cast<std::int64_t>(regs_[base]) + post_index);
            break;
        }
        case bytecode::Opcode::LoadRegOffsetMem: {
            const auto dst = read_u8(bytes, pc);
            const auto base = read_u8(bytes, pc);
            const auto index = read_u8(bytes, pc);
            const auto extend_kind = static_cast<ir::ExtendKind>(read_u8(bytes, pc));
            const auto shift_amount = read_u8(bytes, pc);
            const auto width = read_u8(bytes, pc);
            check_reg(dst);
            check_reg(base);
            check_reg(index);
            const auto offset = ir::apply_extend(regs_[index], extend_kind) << shift_amount;
            regs_[dst] = read_mem(memory, regs_[base] + offset, width);
            break;
        }
        case bytecode::Opcode::StoreRegOffsetMem: {
            const auto src = read_u8(bytes, pc);
            const auto base = read_u8(bytes, pc);
            const auto index = read_u8(bytes, pc);
            const auto extend_kind = static_cast<ir::ExtendKind>(read_u8(bytes, pc));
            const auto shift_amount = read_u8(bytes, pc);
            const auto width = read_u8(bytes, pc);
            check_reg(src);
            check_reg(base);
            check_reg(index);
            const auto offset = ir::apply_extend(regs_[index], extend_kind) << shift_amount;
            write_mem(memory, regs_[base] + offset, regs_[src], width);
            break;
        }
        case bytecode::Opcode::StorePair64: {
            const auto first = read_u8(bytes, pc);
            const auto second = read_u8(bytes, pc);
            const auto base = read_u8(bytes, pc);
            check_reg(first);
            check_reg(second);
            check_reg(base);
            const auto offset = read_u64(bytes, pc);
            write_mem64(memory, regs_[base] + offset, regs_[first]);
            write_mem64(memory, regs_[base] + offset + 8, regs_[second]);
            break;
        }
        case bytecode::Opcode::LoadAcquire:
        case bytecode::Opcode::LoadExclusive: {
            const auto dst = read_u8(bytes, pc);
            const auto base = read_u8(bytes, pc);
            const auto width = static_cast<std::uint8_t>(read_u8(bytes, pc) & 0x0fu);
            check_reg(dst);
            check_reg(base);
            regs_[dst] = read_mem(memory, regs_[base], width);
            break;
        }
        case bytecode::Opcode::StoreRelease: {
            const auto src = read_u8(bytes, pc);
            const auto base = read_u8(bytes, pc);
            const auto width = read_u8(bytes, pc);
            check_reg(src);
            check_reg(base);
            write_mem(memory, regs_[base], regs_[src], width);
            break;
        }
        case bytecode::Opcode::StoreExclusive: {
            const auto status = read_u8(bytes, pc);
            const auto src = read_u8(bytes, pc);
            const auto base = read_u8(bytes, pc);
            const auto width = static_cast<std::uint8_t>(read_u8(bytes, pc) & 0x0fu);
            check_reg(status);
            check_reg(src);
            check_reg(base);
            write_mem(memory, regs_[base], regs_[src], width);
            regs_[status] = 0;
            break;
        }
        case bytecode::Opcode::LoadExclusivePair: {
            const auto first=read_u8(bytes,pc), second=read_u8(bytes,pc), base=read_u8(bytes,pc);
            const auto width=static_cast<std::uint8_t>(read_u8(bytes,pc)&0x0fu);
            check_reg(first); check_reg(second); check_reg(base);
            if(width!=4&&width!=8) throw std::runtime_error("invalid exclusive-pair load width");
            regs_[first]=read_mem(memory,regs_[base],width);
            regs_[second]=read_mem(memory,regs_[base]+width,width);
            break;
        }
        case bytecode::Opcode::StoreExclusivePair: {
            const auto status=read_u8(bytes,pc), first=read_u8(bytes,pc), second=read_u8(bytes,pc);
            const auto base=read_u8(bytes,pc), width=static_cast<std::uint8_t>(read_u8(bytes,pc)&0x0fu);
            check_reg(status); check_reg(first); check_reg(second); check_reg(base);
            if(width!=4&&width!=8) throw std::runtime_error("invalid exclusive-pair store width");
            write_mem(memory,regs_[base],regs_[first],width);
            write_mem(memory,regs_[base]+width,regs_[second],width);
            regs_[status]=0;
            break;
        }
        case bytecode::Opcode::CompareSwap: {
            const auto expected_reg = read_u8(bytes, pc);
            const auto desired_reg = read_u8(bytes, pc);
            const auto base_reg = read_u8(bytes, pc);
            const auto width_flags = read_u8(bytes, pc);
            const auto width = static_cast<std::uint8_t>(width_flags & 0x0fu);
            check_reg(expected_reg);
            check_reg(desired_reg);
            check_reg(base_reg);
            const auto observed = read_mem(memory, regs_[base_reg], width);
            const auto mask = width == 8 ? ~0ull : ((1ull << (width * 8u)) - 1ull);
            if ((regs_[expected_reg] & mask) == observed) {
                write_mem(memory, regs_[base_reg], regs_[desired_reg], width);
            }
            regs_[expected_reg] = observed;
            break;
        }
        case bytecode::Opcode::CallNative:
            throw std::runtime_error("host VM cannot execute an AArch64 native call gate");
        case bytecode::Opcode::CallNativeReg:
            throw std::runtime_error("host VM cannot execute an AArch64 indirect call gate");
        case bytecode::Opcode::CallVm: {
            const auto target = read_u32(bytes, pc);
            vm_call_stack.push_back(pc);
            pc = resolve_target(target);
            break;
        }
        case bytecode::Opcode::JumpBlock:
            pc = resolve_target(read_u32(bytes, pc));
            break;
        case bytecode::Opcode::LoadFpImm64: {
            const auto dst = read_u8(bytes, pc); if (dst >= fp_regs_.size()) throw std::runtime_error("FP register out of range");
            fp_regs_[dst] = read_u64(bytes, pc); break;
        }
        case bytecode::Opcode::FpAdd64:
        case bytecode::Opcode::FpSub64:
        case bytecode::Opcode::FpMul64:
        case bytecode::Opcode::FpDiv64: {
            const auto dst=read_u8(bytes,pc), lhs=read_u8(bytes,pc), rhs=read_u8(bytes,pc);
            if (dst>=32 || lhs>=32 || rhs>=32) throw std::runtime_error("FP register out of range");
            const auto a=std::bit_cast<double>(fp_regs_[lhs]), b=std::bit_cast<double>(fp_regs_[rhs]);
            const double value = opcode==bytecode::Opcode::FpAdd64 ? a+b : opcode==bytecode::Opcode::FpSub64 ? a-b : opcode==bytecode::Opcode::FpMul64 ? a*b : a/b;
            fp_regs_[dst]=std::bit_cast<std::uint64_t>(value); break;
        }
        case bytecode::Opcode::FpMadd64: {
            const auto dst=read_u8(bytes,pc), lhs=read_u8(bytes,pc), rhs=read_u8(bytes,pc), add=read_u8(bytes,pc);
            if (dst>=32 || lhs>=32 || rhs>=32 || add>=32) throw std::runtime_error("FP register out of range");
            fp_regs_[dst]=std::bit_cast<std::uint64_t>(std::fma(std::bit_cast<double>(fp_regs_[lhs]), std::bit_cast<double>(fp_regs_[rhs]), std::bit_cast<double>(fp_regs_[add]))); break;
        }
        case bytecode::Opcode::FpCompare64: {
            const auto lhs=read_u8(bytes,pc), rhs=read_u8(bytes,pc); if(lhs>=32||rhs>=32) throw std::runtime_error("FP register out of range");
            const auto a=std::bit_cast<double>(fp_regs_[lhs]), b=std::bit_cast<double>(fp_regs_[rhs]);
            if (std::isnan(a)||std::isnan(b)) flags_={false,false,true,true}; else flags_={a==b,a<b,a>=b,false};
            break;
        }
        case bytecode::Opcode::LoadFpImm32: {
            const auto dst=read_u8(bytes,pc); if(dst>=32) throw std::runtime_error("FP register out of range");
            fp_regs_[dst]=static_cast<std::uint32_t>(read_u64(bytes,pc)); break;
        }
        case bytecode::Opcode::FpAdd32:
        case bytecode::Opcode::FpSub32:
        case bytecode::Opcode::FpMul32:
        case bytecode::Opcode::FpDiv32: {
            const auto dst=read_u8(bytes,pc), lhs=read_u8(bytes,pc), rhs=read_u8(bytes,pc);
            if(dst>=32||lhs>=32||rhs>=32) throw std::runtime_error("FP register out of range");
            const auto a=std::bit_cast<float>(static_cast<std::uint32_t>(fp_regs_[lhs]));
            const auto b=std::bit_cast<float>(static_cast<std::uint32_t>(fp_regs_[rhs]));
            const float value=opcode==bytecode::Opcode::FpAdd32?a+b:opcode==bytecode::Opcode::FpSub32?a-b:opcode==bytecode::Opcode::FpMul32?a*b:a/b;
            fp_regs_[dst]=std::bit_cast<std::uint32_t>(value); break;
        }
        case bytecode::Opcode::FpMadd32: {
            const auto dst=read_u8(bytes,pc), lhs=read_u8(bytes,pc), rhs=read_u8(bytes,pc), add=read_u8(bytes,pc);
            if(dst>=32||lhs>=32||rhs>=32||add>=32) throw std::runtime_error("FP register out of range");
            const auto value=std::fma(std::bit_cast<float>(static_cast<std::uint32_t>(fp_regs_[lhs])), std::bit_cast<float>(static_cast<std::uint32_t>(fp_regs_[rhs])), std::bit_cast<float>(static_cast<std::uint32_t>(fp_regs_[add])));
            fp_regs_[dst]=std::bit_cast<std::uint32_t>(value); break;
        }
        case bytecode::Opcode::FpCompare32: {
            const auto lhs=read_u8(bytes,pc), rhs=read_u8(bytes,pc); if(lhs>=32||rhs>=32) throw std::runtime_error("FP register out of range");
            const auto a=std::bit_cast<float>(static_cast<std::uint32_t>(fp_regs_[lhs])), b=std::bit_cast<float>(static_cast<std::uint32_t>(fp_regs_[rhs]));
            if(std::isnan(a)||std::isnan(b)) flags_={false,false,true,true}; else flags_={a==b,a<b,a>=b,false}; break;
        }
        case bytecode::Opcode::FpUnary: {
            const auto dst=read_u8(bytes,pc), src=read_u8(bytes,pc), kind=read_u8(bytes,pc);
            if(dst>=32||src>=32||kind>7) throw std::runtime_error("invalid FP unary operand");
            if (kind == 0 || kind == 2 || kind == 4 || kind == 6) {
                const auto value = std::bit_cast<float>(static_cast<std::uint32_t>(fp_regs_[src]));
                if (kind == 0) fp_regs_[dst] = std::bit_cast<std::uint32_t>(std::fabs(value));
                else if (kind == 2) fp_regs_[dst] = std::bit_cast<std::uint32_t>(-value);
                else if (kind == 4) fp_regs_[dst] = std::bit_cast<std::uint32_t>(std::sqrt(value));
                else fp_regs_[dst] = std::bit_cast<std::uint64_t>(static_cast<double>(value));
            } else {
                const auto value = std::bit_cast<double>(fp_regs_[src]);
                if (kind == 1) fp_regs_[dst] = std::bit_cast<std::uint64_t>(std::fabs(value));
                else if (kind == 3) fp_regs_[dst] = std::bit_cast<std::uint64_t>(-value);
                else if (kind == 5) fp_regs_[dst] = std::bit_cast<std::uint64_t>(std::sqrt(value));
                else fp_regs_[dst] = std::bit_cast<std::uint32_t>(static_cast<float>(value));
            }
            break;
        }
        case bytecode::Opcode::FpConvert: {
            const auto dst=read_u8(bytes,pc), src=read_u8(bytes,pc), kind=read_u8(bytes,pc);
            if(dst>=33||src>=33||kind>15) throw std::runtime_error("invalid FP conversion operand");
            switch (kind) {
            case 0: fp_regs_[dst]=std::bit_cast<std::uint32_t>(static_cast<float>(static_cast<std::int32_t>(regs_[src]))); break;
            case 1: fp_regs_[dst]=std::bit_cast<std::uint64_t>(static_cast<double>(static_cast<std::int32_t>(regs_[src]))); break;
            case 2: fp_regs_[dst]=std::bit_cast<std::uint32_t>(static_cast<float>(static_cast<std::int64_t>(regs_[src]))); break;
            case 3: fp_regs_[dst]=std::bit_cast<std::uint64_t>(static_cast<double>(static_cast<std::int64_t>(regs_[src]))); break;
            case 4: fp_regs_[dst]=std::bit_cast<std::uint32_t>(static_cast<float>(static_cast<std::uint32_t>(regs_[src]))); break;
            case 5: fp_regs_[dst]=std::bit_cast<std::uint64_t>(static_cast<double>(static_cast<std::uint32_t>(regs_[src]))); break;
            case 6: fp_regs_[dst]=std::bit_cast<std::uint32_t>(static_cast<float>(regs_[src])); break;
            case 7: fp_regs_[dst]=std::bit_cast<std::uint64_t>(static_cast<double>(regs_[src])); break;
            case 8: regs_[dst]=static_cast<std::uint32_t>(static_cast<std::int32_t>(std::trunc(std::bit_cast<float>(static_cast<std::uint32_t>(fp_regs_[src]))))); break;
            case 9: regs_[dst]=static_cast<std::uint32_t>(static_cast<std::int32_t>(std::trunc(std::bit_cast<double>(fp_regs_[src])))); break;
            case 10: regs_[dst]=static_cast<std::uint64_t>(static_cast<std::int64_t>(std::trunc(std::bit_cast<float>(static_cast<std::uint32_t>(fp_regs_[src]))))); break;
            case 11: regs_[dst]=static_cast<std::uint64_t>(static_cast<std::int64_t>(std::trunc(std::bit_cast<double>(fp_regs_[src])))); break;
            case 12: regs_[dst]=static_cast<std::uint32_t>(std::trunc(std::bit_cast<float>(static_cast<std::uint32_t>(fp_regs_[src])))); break;
            case 13: regs_[dst]=static_cast<std::uint32_t>(std::trunc(std::bit_cast<double>(fp_regs_[src]))); break;
            case 14: regs_[dst]=static_cast<std::uint64_t>(std::trunc(std::bit_cast<float>(static_cast<std::uint32_t>(fp_regs_[src])))); break;
            default: regs_[dst]=static_cast<std::uint64_t>(std::trunc(std::bit_cast<double>(fp_regs_[src]))); break;
            }
            break;
        }
        case bytecode::Opcode::FpMinMax: {
            const auto dst=read_u8(bytes,pc), lhs=read_u8(bytes,pc), rhs=read_u8(bytes,pc), kind=read_u8(bytes,pc);
            if(dst>=32||lhs>=32||rhs>=32||kind>7) throw std::runtime_error("invalid FP min/max operand");
            if ((kind & 1u) == 0) {
                const auto a=std::bit_cast<float>(static_cast<std::uint32_t>(fp_regs_[lhs]));
                const auto b=std::bit_cast<float>(static_cast<std::uint32_t>(fp_regs_[rhs]));
                const auto value=(kind==0||kind==1||kind==4||kind==5)?std::fmin(a,b):std::fmax(a,b);
                fp_regs_[dst]=std::bit_cast<std::uint32_t>(value);
            } else {
                const auto a=std::bit_cast<double>(fp_regs_[lhs]), b=std::bit_cast<double>(fp_regs_[rhs]);
                fp_regs_[dst]=std::bit_cast<std::uint64_t>((kind==0||kind==1||kind==4||kind==5)?std::fmin(a,b):std::fmax(a,b));
            }
            break;
        }
        case bytecode::Opcode::FpConditionalCompare: {
            const auto lhs=read_u8(bytes,pc), rhs=read_u8(bytes,pc), condition=read_u8(bytes,pc);
            const auto fallback=read_u8(bytes,pc), width=read_u8(bytes,pc);
            (void)read_u8(bytes,pc); // Signaling affects FP exceptions, which this model does not expose.
            if(lhs>=32||rhs>=32||condition>15||(width!=4&&width!=8)||fallback>15)
                throw std::runtime_error("invalid conditional FP compare operand");
            if (test_condition(flags_, static_cast<ir::Condition>(condition))) {
                if (width == 4) {
                    const auto a=std::bit_cast<float>(static_cast<std::uint32_t>(fp_regs_[lhs]));
                    const auto b=std::bit_cast<float>(static_cast<std::uint32_t>(fp_regs_[rhs]));
                    if(std::isnan(a)||std::isnan(b)) flags_={false,false,true,true}; else flags_={a==b,a<b,a>=b,false};
                } else {
                    const auto a=std::bit_cast<double>(fp_regs_[lhs]), b=std::bit_cast<double>(fp_regs_[rhs]);
                    if(std::isnan(a)||std::isnan(b)) flags_={false,false,true,true}; else flags_={a==b,a<b,a>=b,false};
                }
            } else {
                flags_={(fallback&4u)!=0,(fallback&8u)!=0,(fallback&2u)!=0,(fallback&1u)!=0};
            }
            break;
        }
        case bytecode::Opcode::FpSelect: {
            const auto dst=read_u8(bytes,pc), lhs=read_u8(bytes,pc), rhs=read_u8(bytes,pc);
            const auto condition=read_u8(bytes,pc), width=read_u8(bytes,pc);
            if(dst>=32||lhs>=32||rhs>=32||condition>15||(width!=4&&width!=8))
                throw std::runtime_error("invalid conditional FP select operand");
            const auto selected=test_condition(flags_,static_cast<ir::Condition>(condition))?lhs:rhs;
            fp_regs_[dst]=width==4?static_cast<std::uint32_t>(fp_regs_[selected]):fp_regs_[selected];
            break;
        }
        case bytecode::Opcode::NoOp:
            break;
        case bytecode::Opcode::Return: {
            const auto src = read_u8(bytes, pc);
            check_reg(src);
            if (!vm_call_stack.empty()) {
                pc = vm_call_stack.back();
                vm_call_stack.pop_back();
                break;
            }
            result.value = regs_[src];
            return result;
        }
        case bytecode::Opcode::Halt:
            return result;
        default:
            throw std::runtime_error("unknown VM opcode");
        }
    }

    return result;
}

} // namespace myvmp::vm
