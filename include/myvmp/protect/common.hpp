#pragma once

#include "myvmp/bytecode/bytecode.hpp"
#include "myvmp/elf/elf.hpp"
#include "myvmp/elf/patch_plan.hpp"
#include "myvmp/ir/ir.hpp"
#include "myvmp/patch/elf_writer.hpp"
#include "myvmp/vm/multi_vm.hpp"

#include <optional>
#include <string>
#include <vector>

namespace myvmp::protect {

struct DryRunArtifact {
    bool liftable = false;
    std::string reason;
    elf::Symbol function;
    std::vector<std::uint8_t> original_bytes;
    ir::Program program;
    bytecode::Image bytecode;
    std::optional<vm::SiblingVmImage> sibling_bytecode;
    elf::PatchPlan patch_plan;
    std::vector<std::uint8_t> planned_entry_bytes;
};

struct ProtectionResult {
    bool success = false;
    std::string reason;
    DryRunArtifact artifact;
    patch::ElfPatchResult patch;
};

} // namespace myvmp::protect
