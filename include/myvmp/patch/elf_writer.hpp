#pragma once

#include "myvmp/elf/elf.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace myvmp::patch {

struct ElfPatchResult {
    bool success = false;
    std::string reason;
    std::uint64_t payload_va = 0;
    std::uint64_t payload_offset = 0;
};

class Aarch64ElfWriter {
public:
    ElfPatchResult write(const std::filesystem::path& input,
                         const std::filesystem::path& output,
                         const elf::Symbol& function,
                         const std::vector<std::uint8_t>& executable_payload,
                         bool erase_original_body = false) const;
};

class Arm32ElfWriter {
public:
    ElfPatchResult write(const std::filesystem::path& input,
                         const std::filesystem::path& output,
                         const elf::Symbol& function,
                         const std::vector<std::uint8_t>& executable_payload,
                         bool erase_original_body = false) const;
};

class Riscv64ElfWriter {
public:
    ElfPatchResult write(const std::filesystem::path& input,
                         const std::filesystem::path& output,
                         const elf::Symbol& function,
                         const std::vector<std::uint8_t>& executable_payload,
                         bool erase_original_body = false) const;
};

} // namespace myvmp::patch
