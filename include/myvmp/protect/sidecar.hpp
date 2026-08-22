#pragma once

#include "myvmp/bytecode/bytecode.hpp"
#include "myvmp/vm/multi_vm.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string_view>

namespace myvmp::protect {

struct ProtectionManifestOptions {
    std::string_view architecture;
    std::string_view opcode_codec;
    std::string_view operand_codec{"rolling-window-v1"};
    std::string_view opcode_whitening;
    std::string_view runtime_dispatch;
    std::size_t runtime_code_size{};
    std::size_t encrypted_bytecode_offset{};
};

[[nodiscard]] bool publish_profile_sidecar(
    const std::filesystem::path& protected_output,
    const bytecode::Image& image,
    const std::optional<vm::SiblingVmImage>& sibling,
    const ProtectionManifestOptions& options);

} // namespace myvmp::protect
