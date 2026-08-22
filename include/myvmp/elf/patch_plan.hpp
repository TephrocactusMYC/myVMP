#pragma once

#include "myvmp/elf/elf.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace myvmp::elf {

enum class DetourKind {
    Unsupported,
    AArch64DirectBranch,
    AArch64NeedsVeneer,
};

struct PatchPlan {
    DetourKind detour_kind = DetourKind::Unsupported;
    std::uint64_t target_va = 0;
    std::uint64_t target_size = 0;
    std::uint64_t payload_va = 0;
    std::uint64_t payload_capacity = 0;
    std::string reason;
};

class PatchPlanner {
public:
    PatchPlan plan_function_vm_entry(const ElfFile& elf,
                                     const Symbol& function,
                                     std::uint64_t payload_size) const;
};

std::string to_string(DetourKind kind);

} // namespace myvmp::elf

