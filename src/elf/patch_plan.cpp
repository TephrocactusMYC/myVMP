#include "myvmp/elf/patch_plan.hpp"

#include "myvmp/patch/aarch64_trampoline.hpp"

#include <elf.h>

namespace myvmp::elf {
namespace {

std::uint64_t align_up(std::uint64_t value, std::uint64_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

} // namespace

PatchPlan PatchPlanner::plan_function_vm_entry(const ElfFile& elf,
                                               const Symbol& function,
                                               std::uint64_t payload_size) const {
    PatchPlan plan;
    plan.target_va = function.value;
    plan.target_size = function.size;

    if (elf.machine() != Machine::AArch64) {
        plan.reason = "only AArch64 detour planning is implemented";
        return plan;
    }
    if (function.size < 4) {
        plan.reason = "function is too small for an AArch64 branch detour";
        return plan;
    }

    const Segment* executable = nullptr;
    for (const auto& segment : elf.segments()) {
        if (segment.type == PT_LOAD && (segment.flags & PF_X) != 0) {
            executable = &segment;
        }
    }
    if (executable == nullptr) {
        plan.reason = "no executable PT_LOAD segment found";
        return plan;
    }

    // Initial planning uses append-after-text as a model. Real writing must update
    // program headers or delegate segment insertion to a format-aware backend.
    plan.payload_va = align_up(executable->vaddr + executable->memsz, 16);
    plan.payload_capacity = payload_size;

    if (myvmp::patch::encode_aarch64_b(function.value, plan.payload_va).has_value()) {
        plan.detour_kind = DetourKind::AArch64DirectBranch;
        plan.reason = "direct AArch64 branch can reach planned payload";
    } else {
        plan.detour_kind = DetourKind::AArch64NeedsVeneer;
        plan.reason = "payload is outside direct branch range; nearby veneer required";
    }
    return plan;
}

std::string to_string(DetourKind kind) {
    switch (kind) {
    case DetourKind::AArch64DirectBranch:
        return "aarch64-direct-branch";
    case DetourKind::AArch64NeedsVeneer:
        return "aarch64-needs-veneer";
    case DetourKind::Unsupported:
        return "unsupported";
    }
    return "unsupported";
}

} // namespace myvmp::elf
