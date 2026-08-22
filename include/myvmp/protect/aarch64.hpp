#pragma once

#include "myvmp/protect/common.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <optional>

namespace myvmp::protect {

enum class Aarch64RelocationClass {
    InstructionPcRelative,
    InstructionAbsolute,
    InstructionGot,
    InstructionTls,
    Data,
    Dynamic,
    Unknown,
};

[[nodiscard]] Aarch64RelocationClass classify_aarch64_relocation(std::uint32_t type) noexcept;
[[nodiscard]] std::string to_string(Aarch64RelocationClass relocation_class);

class Aarch64Protector {
public:
    DryRunArtifact dry_run(const elf::ElfFile& elf,
                           const elf::Symbol& function,
                           std::uint32_t seed = 0,
                           std::optional<std::uint32_t> secondary_seed = std::nullopt) const;
    ProtectionResult protect_file(const std::filesystem::path& input,
                                  const std::filesystem::path& output,
                                  const std::string& symbol_name,
                                  std::uint32_t seed = 0,
                                  std::optional<std::uint32_t> secondary_seed = std::nullopt) const;
};

} // namespace myvmp::protect
