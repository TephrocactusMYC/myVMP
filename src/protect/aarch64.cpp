#include "myvmp/protect/aarch64.hpp"

#include "myvmp/arm64/decoder.hpp"
#include "myvmp/patch/aarch64_trampoline.hpp"
#include "myvmp/protect/sidecar.hpp"
#include "myvmp/runtime/aarch64.hpp"

#include <elf.h>
#include <exception>
#include <iomanip>
#include <sstream>

namespace myvmp::protect {
namespace {

std::vector<std::uint8_t> word_bytes(std::uint32_t word) {
    return {
        static_cast<std::uint8_t>(word),
        static_cast<std::uint8_t>(word >> 8u),
        static_cast<std::uint8_t>(word >> 16u),
        static_cast<std::uint8_t>(word >> 24u),
    };
}

std::string hex_offset(std::uint64_t value) {
    std::ostringstream out;
    out << std::hex << value;
    return out.str();
}

} // namespace

Aarch64RelocationClass classify_aarch64_relocation(std::uint32_t type) noexcept {
    switch (type) {
    case R_AARCH64_LD_PREL_LO19:
    case R_AARCH64_ADR_PREL_LO21:
    case R_AARCH64_ADR_PREL_PG_HI21:
#ifdef R_AARCH64_ADR_PREL_PG_HI21_NC
    case R_AARCH64_ADR_PREL_PG_HI21_NC:
#endif
    case R_AARCH64_TSTBR14:
    case R_AARCH64_CONDBR19:
    case R_AARCH64_JUMP26:
    case R_AARCH64_CALL26:
        return Aarch64RelocationClass::InstructionPcRelative;
    case R_AARCH64_ADR_GOT_PAGE:
    case R_AARCH64_GOT_LD_PREL19:
    case R_AARCH64_LD64_GOTOFF_LO15:
    case R_AARCH64_LD64_GOT_LO12_NC:
    case R_AARCH64_LD64_GOTPAGE_LO15:
    case R_AARCH64_MOVW_GOTOFF_G0:
    case R_AARCH64_MOVW_GOTOFF_G0_NC:
    case R_AARCH64_MOVW_GOTOFF_G1:
    case R_AARCH64_MOVW_GOTOFF_G1_NC:
    case R_AARCH64_MOVW_GOTOFF_G2:
    case R_AARCH64_MOVW_GOTOFF_G2_NC:
    case R_AARCH64_MOVW_GOTOFF_G3:
        return Aarch64RelocationClass::InstructionGot;
    case R_AARCH64_TLSGD_ADR_PREL21:
    case R_AARCH64_TLSGD_ADR_PAGE21:
    case R_AARCH64_TLSGD_ADD_LO12_NC:
    case R_AARCH64_TLSLD_ADR_PREL21:
    case R_AARCH64_TLSLD_ADR_PAGE21:
    case R_AARCH64_TLSLD_ADD_LO12_NC:
    case R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21:
    case R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC:
    case R_AARCH64_TLSIE_LD_GOTTPREL_PREL19:
    case R_AARCH64_TLSLE_ADD_TPREL_HI12:
    case R_AARCH64_TLSLE_ADD_TPREL_LO12:
    case R_AARCH64_TLSLE_ADD_TPREL_LO12_NC:
    case R_AARCH64_TLSDESC_LD_PREL19:
    case R_AARCH64_TLSDESC_ADR_PREL21:
    case R_AARCH64_TLSDESC_ADR_PAGE21:
    case R_AARCH64_TLSDESC_LD64_LO12:
    case R_AARCH64_TLSDESC_ADD_LO12:
    case R_AARCH64_TLSDESC_CALL:
        return Aarch64RelocationClass::InstructionTls;
    case R_AARCH64_ADD_ABS_LO12_NC:
    case R_AARCH64_LDST8_ABS_LO12_NC:
    case R_AARCH64_LDST16_ABS_LO12_NC:
    case R_AARCH64_LDST32_ABS_LO12_NC:
    case R_AARCH64_LDST64_ABS_LO12_NC:
#ifdef R_AARCH64_LDST128_ABS_LO12_NC
    case R_AARCH64_LDST128_ABS_LO12_NC:
#endif
        return Aarch64RelocationClass::InstructionAbsolute;
    case R_AARCH64_ABS64:
    case R_AARCH64_ABS32:
    case R_AARCH64_ABS16:
    case R_AARCH64_PREL64:
    case R_AARCH64_PREL32:
    case R_AARCH64_PREL16:
        return Aarch64RelocationClass::Data;
    case R_AARCH64_COPY:
    case R_AARCH64_GLOB_DAT:
    case R_AARCH64_JUMP_SLOT:
    case R_AARCH64_RELATIVE:
    case R_AARCH64_IRELATIVE:
        return Aarch64RelocationClass::Dynamic;
    default:
        return Aarch64RelocationClass::Unknown;
    }
}

std::string to_string(Aarch64RelocationClass relocation_class) {
    switch (relocation_class) {
    case Aarch64RelocationClass::InstructionPcRelative: return "instruction-pc-relative";
    case Aarch64RelocationClass::InstructionAbsolute: return "instruction-absolute";
    case Aarch64RelocationClass::InstructionGot: return "instruction-got";
    case Aarch64RelocationClass::InstructionTls: return "instruction-tls";
    case Aarch64RelocationClass::Data: return "data";
    case Aarch64RelocationClass::Dynamic: return "dynamic";
    case Aarch64RelocationClass::Unknown: return "unknown";
    }
    return "unknown";
}

DryRunArtifact Aarch64Protector::dry_run(const elf::ElfFile& image,
                                         const elf::Symbol& function,
                                         std::uint32_t seed,
                                         std::optional<std::uint32_t> secondary_seed) const {
    DryRunArtifact artifact;
    artifact.function = function;
    if (image.machine() != elf::Machine::AArch64) {
        artifact.reason = "only AArch64 ELF function protection is implemented";
        return artifact;
    }
    if (function.size == 0 || (function.size % 4u) != 0) {
        artifact.reason = "function size must be nonzero and 4-byte aligned";
        return artifact;
    }
    const auto relocations = image.relocations_in_range(function.value, function.size);
    if (!relocations.empty()) {
        const auto& relocation = relocations.front();
        artifact.reason = "function contains unsupported " +
                          to_string(classify_aarch64_relocation(relocation.type)) +
                          " AArch64 relocation type " + std::to_string(relocation.type) +
                          " at +0x" + hex_offset(relocation.offset - function.value);
        artifact.reason += "; " + std::to_string(relocations.size()) +
                           " in-range relocation record(s) require rewriting";
        return artifact;
    }
    const auto bytes = image.read_va(function.value, static_cast<std::size_t>(function.size));
    if (!bytes.has_value()) {
        artifact.reason = "function bytes are outside file-backed PT_LOAD segments";
        return artifact;
    }
    artifact.original_bytes = *bytes;
    try {
        const arm64::LiftContext context{
            .region_va = function.value,
            .read_memory = [&image](std::uint64_t address, std::uint8_t width) {
                return image.read_value_va(address, width);
            },
        };
        const arm64::Lifter lifter;
        artifact.program = lifter.lift(artifact.original_bytes,
                                       static_cast<std::size_t>(function.size / 4u),
                                       context);
        const auto profile = seed == 0 ? bytecode::fixed_profile() : bytecode::profile_from_seed(seed);
        const auto key = seed == 0 ? 0xC0DEC0DEu : seed ^ 0xA5A5A5A5u;
        if (secondary_seed.has_value()) {
            artifact.sibling_bytecode = vm::encode_sibling(artifact.program, {
                .primary_seed = seed,
                .secondary_seed = *secondary_seed,
                .partition_seed = seed ^ (*secondary_seed * 0x9e3779b9u),
                .key = key,
            });
            artifact.bytecode = artifact.sibling_bytecode->image;
        } else {
            artifact.bytecode = bytecode::encode(artifact.program, {
                .key = key,
                .profile = profile,
                .diversify_immediates = seed != 0,
                .diversification_seed = seed,
                .shuffle_blocks = seed != 0,
            });
        }
        const elf::PatchPlanner planner;
        const runtime::Aarch64RuntimeContext sizing_context{
            .function_va = function.value,
            .payload_va = function.value,
        };
        const auto runtime_size = artifact.sibling_bytecode
            ? runtime::build_aarch64_runtime_payload(*artifact.sibling_bytecode, sizing_context).size()
            : runtime::build_aarch64_runtime_payload(artifact.bytecode, sizing_context).size();
        artifact.patch_plan = planner.plan_function_vm_entry(image, function, runtime_size);
        if (artifact.patch_plan.detour_kind == elf::DetourKind::Unsupported) {
            artifact.reason = artifact.patch_plan.reason;
            return artifact;
        }
        if (artifact.patch_plan.detour_kind == elf::DetourKind::AArch64DirectBranch) {
            const auto branch = patch::encode_aarch64_b(function.value, artifact.patch_plan.payload_va);
            if (!branch.has_value()) {
                artifact.reason = "planned direct branch cannot be encoded";
                return artifact;
            }
            artifact.planned_entry_bytes = word_bytes(*branch);
        }
        artifact.liftable = true;
        artifact.reason = artifact.patch_plan.detour_kind == elf::DetourKind::AArch64NeedsVeneer
                              ? "liftable; nearby veneer required before patching"
                              : "liftable; direct entry detour planned";
    } catch (const std::exception& error) {
        artifact.reason = error.what();
    }
    return artifact;
}

ProtectionResult Aarch64Protector::protect_file(const std::filesystem::path& input,
                                                const std::filesystem::path& output,
                                                const std::string& symbol_name,
                                                std::uint32_t seed,
                                                std::optional<std::uint32_t> secondary_seed) const {
    ProtectionResult result;
    try {
        const auto image = elf::ElfFile::parse(input);
        const auto function = image.find_symbol(symbol_name);
        if (!function.has_value()) {
            result.reason = "symbol not found: " + symbol_name;
            return result;
        }
        result.artifact = dry_run(image, *function, seed, secondary_seed);
        if (!result.artifact.liftable) {
            result.reason = result.artifact.reason;
            return result;
        }
        const runtime::Aarch64RuntimeContext context{.function_va = function->value,
                                                     .payload_va = result.artifact.patch_plan.payload_va};
        const auto payload = result.artifact.sibling_bytecode
            ? runtime::build_aarch64_runtime_payload(*result.artifact.sibling_bytecode, context)
            : runtime::build_aarch64_runtime_payload(result.artifact.bytecode, context);
        const patch::Aarch64ElfWriter writer;
        result.patch = writer.write(input, output, *function, payload, true);
        if (!result.patch.success) {
            result.reason = result.patch.reason;
            return result;
        }
        const auto encrypted_offset = result.artifact.sibling_bytecode
            ? runtime::aarch64_runtime_bytecode_offset(*result.artifact.sibling_bytecode)
            : runtime::aarch64_runtime_bytecode_offset(result.artifact.bytecode);
        if (!publish_profile_sidecar(output, result.artifact.bytecode,
                                     result.artifact.sibling_bytecode, {
            .architecture = "aarch64",
            .opcode_codec = "runtime-profile-domain-v1",
            .opcode_whitening = result.artifact.sibling_bytecode
                ? "cfg-block-derived-v1" : "profile-domain-v1",
            .runtime_dispatch = result.artifact.sibling_bytecode
                ? "per-block-dual-domain-v1" : "profile-domain-v1",
            .runtime_code_size = runtime::aarch64_runtime_code_size(),
            .encrypted_bytecode_offset = encrypted_offset,
        })) {
            result.reason = "protected ELF was written but profile sidecar could not be written atomically";
            return result;
        }
        result.success = true;
        result.reason = result.artifact.sibling_bytecode
                            ? "AArch64 function virtualized with two sibling VM profiles"
                            : "AArch64 function virtualized with embedded VM runtime";
    } catch (const std::exception& error) {
        result.reason = error.what();
    }
    return result;
}

} // namespace myvmp::protect
