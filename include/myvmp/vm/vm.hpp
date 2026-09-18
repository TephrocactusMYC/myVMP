#pragma once

#include "myvmp/bytecode/bytecode.hpp"
#include "myvmp/ir/vector.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace myvmp::vm {

struct SiblingVmImage;

struct Result {
    std::uint64_t value{};
    std::uint64_t instructions_executed{};
};

struct Flags {
    bool z = false;
    bool n = false;
    bool c = false;
    bool v = false;
};

class Interpreter {
public:
    Result execute(const bytecode::Image& image);
    Result execute(const bytecode::Image& image, const std::array<std::uint64_t, 32>& initial_registers);
    Result execute(const bytecode::Image& image,
                   const std::array<std::uint64_t, 32>& initial_registers,
                   std::vector<std::uint8_t>& memory);
    Result execute(const bytecode::Image& image,
                   const std::array<std::uint64_t, 32>& initial_registers,
                   const ir::VectorRegisterFile& initial_vectors,
                   std::vector<std::uint8_t>& memory);
    Result execute(const SiblingVmImage& image);
    [[nodiscard]] const ir::VectorRegisterFile& vector_registers() const noexcept {
        return vector_regs_;
    }

private:
    std::array<std::uint64_t, ir::kVirtualRegisterCount> regs_{};
    std::array<std::uint64_t, 32> fp_regs_{};
    ir::VectorRegisterFile vector_regs_{};
    Flags flags_{};
};

} // namespace myvmp::vm
