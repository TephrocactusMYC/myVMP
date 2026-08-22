#include "myvmp/protect/arm32.hpp"

#include "myvmp/arm32/decoder.hpp"
#include "myvmp/runtime/arm32.hpp"

#include <exception>

namespace myvmp::protect {

DryRunArtifact Arm32Protector::dry_run(const elf::ElfFile& image, const elf::Symbol& function,
                                       std::uint32_t seed) const {
    DryRunArtifact artifact;
    artifact.function = function;
    if (image.machine() != elf::Machine::Arm || image.elf_class() != elf::ElfClass::Elf32) {
        artifact.reason = "ARM32 protection requires a little-endian EM_ARM ELF32 image";
        return artifact;
    }
    if (function.size == 0 || (function.size & 3u) != 0 || (function.value & 3u) != 0) {
        artifact.reason = "ARM32 backend currently requires a nonempty 4-byte-aligned ARM-state function";
        return artifact;
    }
    if (!image.relocations_in_range(function.value, function.size).empty()) {
        artifact.reason = "ARM32 function contains relocation records; rewriting is not implemented";
        return artifact;
    }
    const auto bytes = image.read_va(function.value, function.size);
    if (!bytes) { artifact.reason = "ARM32 function bytes are outside file-backed PT_LOAD segments"; return artifact; }
    artifact.original_bytes = *bytes;
    std::vector<std::uint32_t> words;
    for (std::size_t offset = 0; offset < bytes->size(); offset += 4) {
        words.push_back(static_cast<std::uint32_t>((*bytes)[offset]) |
                        (static_cast<std::uint32_t>((*bytes)[offset + 1]) << 8u) |
                        (static_cast<std::uint32_t>((*bytes)[offset + 2]) << 16u) |
                        (static_cast<std::uint32_t>((*bytes)[offset + 3]) << 24u));
    }
    try {
        artifact.program = arm32::Lifter{}.lift(words);
        const auto profile = seed == 0 ? bytecode::fixed_profile() : bytecode::profile_from_seed(seed);
        const auto key = seed == 0 ? 0xc0dec0deu : seed ^ 0xa5a5a5a5u;
        artifact.bytecode = bytecode::encode(artifact.program, {.key = key, .profile = profile});
        // Building performs the authoritative native-handler coverage check.
        (void)runtime::build_arm32_runtime_payload(artifact.bytecode);
        artifact.liftable = true;
        artifact.reason = "liftable ARM-state function; direct ARM32 detour will be validated during write-back";
    } catch (const std::exception& error) {
        artifact.reason = error.what();
    }
    return artifact;
}

ProtectionResult Arm32Protector::protect_file(const std::filesystem::path& input,
                                              const std::filesystem::path& output,
                                              const std::string& symbol_name,
                                              std::uint32_t seed) const {
    ProtectionResult result;
    try {
        const auto image = elf::ElfFile::parse(input);
        const auto function = image.find_symbol(symbol_name);
        if (!function) { result.reason = "symbol not found: " + symbol_name; return result; }
        result.artifact = dry_run(image, *function, seed);
        if (!result.artifact.liftable) { result.reason = result.artifact.reason; return result; }
        const auto payload = runtime::build_arm32_runtime_payload(result.artifact.bytecode);
        result.patch = patch::Arm32ElfWriter{}.write(input, output, *function, payload, true);
        if (!result.patch.success) { result.reason = result.patch.reason; return result; }
        result.success = true;
        result.reason = "ARM32 function virtualized with embedded dynamic VM runtime";
    } catch (const std::exception& error) {
        result.reason = error.what();
    }
    return result;
}

} // namespace myvmp::protect
