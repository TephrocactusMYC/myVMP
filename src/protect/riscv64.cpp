#include "myvmp/protect/riscv64.hpp"

#include "myvmp/riscv64/decoder.hpp"
#include "myvmp/runtime/riscv64.hpp"
#include "myvmp/protect/sidecar.hpp"

#include <exception>
namespace myvmp::protect {

DryRunArtifact Riscv64Protector::dry_run(const elf::ElfFile& image, const elf::Symbol& function,
                                         std::uint32_t seed,
                                         std::optional<std::uint32_t> secondary_seed) const {
    DryRunArtifact artifact;
    artifact.function = function;
    if (image.machine() != elf::Machine::RiscV || image.elf_class() != elf::ElfClass::Elf64) {
        artifact.reason = "RV64 protection requires a little-endian EM_RISCV ELF64 image";
        return artifact;
    }
    if (function.size == 0 || (function.value & 1u) != 0 || (function.size & 1u) != 0) {
        artifact.reason = "RV64 function must be nonempty and 2-byte aligned";
        return artifact;
    }
    const auto bytes = image.read_va(function.value, static_cast<std::size_t>(function.size));
    if (!bytes) { artifact.reason = "RV64 function bytes are outside file-backed PT_LOAD segments"; return artifact; }
    artifact.original_bytes = *bytes;
    try {
        artifact.program = riscv64::Lifter{}.lift_bytes(
            *bytes, {.region_va = function.value, .instruction_offsets = {}});
        const auto profile = seed == 0 ? bytecode::fixed_profile() : bytecode::profile_from_seed(seed);
        const auto key = seed == 0 ? 0x64c0dec0u : seed ^ 0x9e3779b9u;
        if (secondary_seed) {
            artifact.sibling_bytecode = vm::encode_sibling(artifact.program, {
                .primary_seed = seed,
                .secondary_seed = *secondary_seed,
                .partition_seed = seed ^ (*secondary_seed * 0x85ebca6bu),
                .key = key,
            });
            artifact.bytecode = artifact.sibling_bytecode->image;
        } else {
            artifact.bytecode = bytecode::encode(artifact.program, {
                .key = key, .profile = profile,
                .diversify_immediates = seed != 0,
                .diversification_seed = seed,
                .shuffle_blocks = seed != 0,
            });
            (void)runtime::build_riscv64_runtime_payload(artifact.bytecode,
                                                         {.function_va = function.value});
        }
        artifact.liftable = true;
        artifact.reason = "liftable RV64IM function; native runtime/write-back validation follows";
    } catch (const std::exception& error) { artifact.reason = error.what(); }
    return artifact;
}

ProtectionResult Riscv64Protector::protect_file(const std::filesystem::path& input,
                                                const std::filesystem::path& output,
                                                const std::string& symbol_name,
                                                std::uint32_t seed,
                                                std::optional<std::uint32_t> secondary_seed) const {
    ProtectionResult result;
    try {
        const auto image = elf::ElfFile::parse(input);
        const auto function = image.find_symbol(symbol_name);
        if (!function) { result.reason = "symbol not found: " + symbol_name; return result; }
        result.artifact = dry_run(image, *function, seed, secondary_seed);
        if (!result.artifact.liftable) { result.reason = result.artifact.reason; return result; }
        const auto payload = result.artifact.sibling_bytecode
            ? runtime::build_riscv64_runtime_payload(*result.artifact.sibling_bytecode,
                                                     {.function_va = function->value})
            : runtime::build_riscv64_runtime_payload(result.artifact.bytecode,
                                                     {.function_va = function->value});
        result.patch = patch::Riscv64ElfWriter{}.write(input, output, *function, payload, true);
        if (!result.patch.success) { result.reason = result.patch.reason; return result; }
        if (!publish_profile_sidecar(output, result.artifact.bytecode,
                                     result.artifact.sibling_bytecode, {
            .architecture = "rv64",
            .opcode_codec = "runtime-translation-table-v1",
            .opcode_whitening = "cfg-block-derived-v1",
            .runtime_dispatch = "per-block-dual-domain-v1",
            .runtime_code_size = runtime::riscv64_runtime_code_size(),
            .encrypted_bytecode_offset = runtime::riscv64_encrypted_bytecode_offset(
                result.artifact.bytecode.blocks.empty() ? 1 : result.artifact.bytecode.blocks.size()),
        })) {
            result.reason = "protected RV64 ELF was written but profile sidecar publication failed";
            return result;
        }
        result.success = true;
        result.reason = "RV64 function virtualized with embedded dynamic-profile VM runtime";
    } catch (const std::exception& error) { result.reason = error.what(); }
    return result;
}

} // namespace myvmp::protect
