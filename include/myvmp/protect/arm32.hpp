#pragma once

#include "myvmp/protect/common.hpp"

namespace myvmp::protect {

class Arm32Protector {
public:
    DryRunArtifact dry_run(const elf::ElfFile& image, const elf::Symbol& function,
                           std::uint32_t seed = 0,
                           std::optional<std::uint32_t> secondary_seed = std::nullopt) const;
    ProtectionResult protect_file(const std::filesystem::path& input,
                                  const std::filesystem::path& output,
                                  const std::string& symbol_name,
                                  std::uint32_t seed = 0,
                                  std::optional<std::uint32_t> secondary_seed = std::nullopt) const;
};

} // namespace myvmp::protect
