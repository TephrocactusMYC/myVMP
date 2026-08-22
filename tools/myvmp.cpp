#include "myvmp/arm32/decoder.hpp"
#include "myvmp/arm64/decoder.hpp"
#include "myvmp/bytecode/bytecode.hpp"
#include "myvmp/elf/elf.hpp"
#include "myvmp/elf/patch_plan.hpp"
#include "myvmp/ir/ir.hpp"
#include "myvmp/patch/elf_writer.hpp"
#include "myvmp/protect/aarch64.hpp"
#include "myvmp/protect/arm32.hpp"
#include "myvmp/protect/riscv64.hpp"
#include "myvmp/riscv64/decoder.hpp"

#include <elf.h>

#include <exception>
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <limits>
#include <optional>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

#ifndef MYVMP_VERSION
#define MYVMP_VERSION "development"
#endif

namespace {

std::string g_log_dir;

std::uint32_t parse_word(const std::string& text);

std::uint64_t parse_u64(const std::string& text) {
    std::size_t consumed = 0;
    const auto value = std::stoull(text, &consumed, 0);
    if (consumed != text.size()) {
        throw std::runtime_error("invalid integer: " + text);
    }
    return value;
}

void write_debug_log(const std::string& name, const std::string& content) {
    std::string dir = g_log_dir;
    if (dir.empty()) {
        const char* env_dir = std::getenv("MYVMP_LOG_DIR");
        if (env_dir != nullptr) {
            dir = env_dir;
        }
    }
    if (dir.empty()) {
        return;
    }
    std::filesystem::create_directories(dir);
    std::ofstream out(std::filesystem::path(dir) / name);
    out << content;
}

bool env_flag_enabled(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && std::string(value) != "0" && std::string(value) != "false";
}

std::string hex_dump(const std::vector<std::uint8_t>& bytes) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i != 0) {
            out << (i % 16 == 0 ? '\n' : ' ');
        }
        out << std::setw(2) << static_cast<unsigned>(bytes[i]);
    }
    out << '\n';
    return out.str();
}

std::vector<std::uint8_t> read_file_bytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("failed to open file: " + path);
    }
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::size_t exposed_chunks(const std::vector<std::uint8_t>& before,
                           const std::vector<std::uint8_t>& after,
                           std::size_t width,
                           std::size_t stride = 0) {
    if (width == 0 || before.size() < width || after.size() < width) {
        return 0;
    }
    std::size_t exposed = 0;
    if (stride == 0) stride = width;
    for (std::size_t offset = 0; offset + width <= before.size(); offset += stride) {
        if (std::search(after.begin(), after.end(), before.begin() + static_cast<std::ptrdiff_t>(offset),
                        before.begin() + static_cast<std::ptrdiff_t>(offset + width)) != after.end()) {
            ++exposed;
        }
    }
    return exposed;
}

double shannon_entropy(const std::vector<std::uint8_t>& bytes) {
    if (bytes.empty()) {
        return 0.0;
    }
    std::array<std::size_t, 256> counts{};
    for (const auto byte : bytes) {
        ++counts[byte];
    }
    double entropy = 0.0;
    for (const auto count : counts) {
        if (count == 0) {
            continue;
        }
        const auto p = static_cast<double>(count) / static_cast<double>(bytes.size());
        entropy -= p * std::log2(p);
    }
    return entropy;
}

std::size_t same_position_bytes(const std::vector<std::uint8_t>& lhs, const std::vector<std::uint8_t>& rhs) {
    const auto limit = std::min(lhs.size(), rhs.size());
    std::size_t same = 0;
    for (std::size_t i = 0; i < limit; ++i) {
        if (lhs[i] == rhs[i]) {
            ++same;
        }
    }
    return same;
}

std::size_t longest_common_run(const std::vector<std::uint8_t>& lhs, const std::vector<std::uint8_t>& rhs) {
    const auto limit = std::min(lhs.size(), rhs.size());
    std::size_t current = 0;
    std::size_t best = 0;
    for (std::size_t i = 0; i < limit; ++i) {
        if (lhs[i] == rhs[i]) {
            ++current;
            best = std::max(best, current);
        } else {
            current = 0;
        }
    }
    return best;
}

int inspect_elf(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: myvmp inspect-elf <path> [symbol]\n";
        return 2;
    }

    const auto elf = myvmp::elf::ElfFile::parse(argv[2]);
    std::ostringstream output;
    output << "machine=" << myvmp::elf::to_string(elf.machine()) << '\n';
    output << "class=" << myvmp::elf::to_string(elf.elf_class()) << '\n';
    output << "pie_or_shared=" << (elf.is_pie_or_shared() ? "true" : "false") << '\n';
    output << "entry=0x" << std::hex << elf.entry() << std::dec << '\n';
    output << "segments=" << elf.segments().size() << '\n';
    output << "sections=" << elf.sections().size() << '\n';
    output << "symbols=" << elf.symbols().size() << '\n';
    output << "relocations=" << elf.relocations().size() << '\n';
    std::size_t unwind_sections = 0;
    std::uint64_t unwind_bytes = 0;
    for (const auto& section : elf.sections()) {
        if (section.name == ".eh_frame" || section.name == ".eh_frame_hdr" ||
            section.name == ".gcc_except_table" || section.name.starts_with(".ARM.ex")) {
            ++unwind_sections;
            unwind_bytes += section.size;
        }
    }
    output << "unwind_sections=" << unwind_sections << '\n'
           << "unwind_metadata_bytes=" << unwind_bytes << '\n'
           << "unwind_metadata_protected=false\n";

    if (argc >= 4) {
        const auto symbol = elf.find_symbol(argv[3]);
        if (!symbol.has_value()) {
            std::cerr << "symbol not found: " << argv[3] << '\n';
            return 1;
        }
        const auto file_offset = elf.va_to_offset(symbol->value);
        output << "symbol=" << symbol->name << '\n';
        output << "symbol_va=0x" << std::hex << symbol->value << std::dec << '\n';
        output << "symbol_size=" << symbol->size << '\n';
        if (file_offset.has_value()) {
            output << "symbol_offset=0x" << std::hex << *file_offset << std::dec << '\n';
        }
        const auto relocations = elf.relocations_in_range(symbol->value, symbol->size);
        output << "symbol_relocations=" << relocations.size() << '\n';
        for (const auto& relocation : relocations) {
            output << "relocation_offset=0x" << std::hex << relocation.offset << std::dec
                   << " type=" << relocation.type
                   << " symbol=" << (relocation.symbol_name.empty() ? "<none>" : relocation.symbol_name)
                   << " addend=" << relocation.addend
                   << " target_section=" << relocation.target_section << '\n';
        }

        const myvmp::elf::PatchPlanner planner;
        const auto plan = planner.plan_function_vm_entry(elf, *symbol, 4096);
        output << "detour_kind=" << myvmp::elf::to_string(plan.detour_kind) << '\n';
        output << "payload_va=0x" << std::hex << plan.payload_va << std::dec << '\n';
        output << "plan_reason=" << plan.reason << '\n';
    }

    std::cout << output.str();
    write_debug_log("inspect-elf.log", output.str());
    return 0;
}

int quality_report(int argc, char** argv) {
    bool json = false;
    auto max_same_position = std::numeric_limits<double>::infinity();
    auto max_word32_exposure = std::numeric_limits<double>::infinity();
    auto max_word64_exposure = std::numeric_limits<double>::infinity();
    auto max_literal32_exposure = std::numeric_limits<double>::infinity();
    auto max_literal64_exposure = std::numeric_limits<double>::infinity();
    auto max_size_ratio = std::numeric_limits<double>::infinity();
    int path_index = 2;
    while (path_index < argc && std::string(argv[path_index]).starts_with("--")) {
        const std::string option = argv[path_index++];
        if (option == "--json") {
            json = true;
            continue;
        }
        if (path_index >= argc) throw std::invalid_argument("quality threshold value is missing");
        const auto value = std::stod(argv[path_index++]);
        if (value < 0.0) throw std::invalid_argument("quality thresholds must be non-negative");
        if (option == "--max-same-position") max_same_position = value;
        else if (option == "--max-word32-exposure") max_word32_exposure = value;
        else if (option == "--max-word64-exposure") max_word64_exposure = value;
        else if (option == "--max-literal32-exposure") max_literal32_exposure = value;
        else if (option == "--max-literal64-exposure") max_literal64_exposure = value;
        else if (option == "--max-size-ratio") max_size_ratio = value;
        else throw std::invalid_argument("unknown quality-report option: " + option);
    }
    if (argc != path_index + 2) {
        std::cerr << "usage: myvmp quality-report [--json] [--max-same-position N] "
                     "[--max-word32-exposure N] [--max-word64-exposure N] "
                     "[--max-literal32-exposure N] [--max-literal64-exposure N] "
                     "[--max-size-ratio N] <before-bytes> <after-bytes>\n";
        return 2;
    }

    const auto before = read_file_bytes(argv[path_index]);
    const auto after = read_file_bytes(argv[path_index + 1]);
    const auto limit = std::min(before.size(), after.size());
    const auto same = same_position_bytes(before, after);
    const auto longest_run = longest_common_run(before, after);
    const auto same_ratio = limit == 0 ? 0.0 : static_cast<double>(same) / static_cast<double>(limit);
    const auto size_ratio = before.empty() ? 0.0 : static_cast<double>(after.size()) / static_cast<double>(before.size());
    const auto exposed_words32 = exposed_chunks(before, after, 4);
    const auto exposed_words64 = exposed_chunks(before, after, 8);
    const auto exposed_literals32 = exposed_chunks(before, after, 4, 1);
    const auto exposed_literals64 = exposed_chunks(before, after, 8, 1);
    const auto source_words32 = before.size() / 4;
    const auto source_words64 = before.size() / 8;
    const auto exposure32 = source_words32 == 0 ? 0.0 : static_cast<double>(exposed_words32) / source_words32;
    const auto exposure64 = source_words64 == 0 ? 0.0 : static_cast<double>(exposed_words64) / source_words64;
    const auto source_literals32 = before.size() < 4 ? 0 : before.size() - 3;
    const auto source_literals64 = before.size() < 8 ? 0 : before.size() - 7;
    const auto literal_exposure32 = source_literals32 == 0 ? 0.0 : static_cast<double>(exposed_literals32) / source_literals32;
    const auto literal_exposure64 = source_literals64 == 0 ? 0.0 : static_cast<double>(exposed_literals64) / source_literals64;
    const auto passed = same_ratio <= max_same_position && exposure32 <= max_word32_exposure &&
                        exposure64 <= max_word64_exposure && literal_exposure32 <= max_literal32_exposure &&
                        literal_exposure64 <= max_literal64_exposure && size_ratio <= max_size_ratio;

    std::ostringstream output;
    output << std::fixed << std::setprecision(6);
    if (json) {
        output << "{\n"
               << "  \"before_size\": " << before.size() << ",\n"
               << "  \"after_size\": " << after.size() << ",\n"
               << "  \"size_ratio\": " << size_ratio << ",\n"
               << "  \"before_entropy\": " << shannon_entropy(before) << ",\n"
               << "  \"after_entropy\": " << shannon_entropy(after) << ",\n"
               << "  \"same_position_bytes\": " << same << ",\n"
               << "  \"same_position_ratio\": " << same_ratio << ",\n"
               << "  \"longest_same_position_run\": " << longest_run << ",\n"
               << "  \"exposed_aligned_words32\": " << exposed_words32 << ",\n"
               << "  \"exposed_aligned_words64\": " << exposed_words64 << ",\n"
               << "  \"aligned_word32_exposure_ratio\": " << exposure32 << ",\n"
               << "  \"aligned_word64_exposure_ratio\": " << exposure64 << ",\n"
               << "  \"source_literal32_exposure_ratio\": " << literal_exposure32 << ",\n"
               << "  \"source_literal64_exposure_ratio\": " << literal_exposure64 << ",\n"
               << "  \"thresholds_passed\": " << (passed ? "true" : "false") << "\n"
               << "}\n";
    } else {
        output << "before_size=" << before.size() << '\n'
               << "after_size=" << after.size() << '\n'
               << "size_ratio=" << size_ratio << '\n'
               << "before_entropy=" << shannon_entropy(before) << '\n'
               << "after_entropy=" << shannon_entropy(after) << '\n'
               << "same_position_bytes=" << same << '\n'
               << "same_position_ratio=" << same_ratio << '\n'
               << "longest_same_position_run=" << longest_run << '\n'
               << "exposed_aligned_words32=" << exposed_words32 << '\n'
               << "exposed_aligned_words64=" << exposed_words64 << '\n'
               << "aligned_word32_exposure_ratio=" << exposure32 << '\n'
               << "aligned_word64_exposure_ratio=" << exposure64 << '\n';
        output << "source_literal32_exposure_ratio=" << literal_exposure32 << '\n'
               << "source_literal64_exposure_ratio=" << literal_exposure64 << '\n';
        output << "thresholds_passed=" << (passed ? "true" : "false") << '\n';
    }
    std::cout << output.str();
    write_debug_log("quality-report.log", output.str());
    return passed ? 0 : 1;
}

int profile_manifest(int argc, char** argv) {
    std::uint32_t seed = 0;
    if (argc >= 3) {
        seed = parse_word(argv[2]);
    }
    const auto profile = seed == 0 ? myvmp::bytecode::fixed_profile() : myvmp::bytecode::profile_from_seed(seed);
    const auto output = profile.manifest() + "\n";
    std::cout << output;
    write_debug_log("profile-manifest.json", output);
    return 0;
}

std::uint32_t parse_word(const std::string& text) {
    std::size_t consumed = 0;
    const auto value = std::stoull(text, &consumed, 0);
    if (consumed != text.size() || value > 0xffffffffull) {
        throw std::runtime_error("invalid ARM64 instruction word: " + text);
    }
    return static_cast<std::uint32_t>(value);
}

std::string shift_to_string(myvmp::ir::ShiftKind shift) {
    switch (shift) {
    case myvmp::ir::ShiftKind::Lsl:
        return "lsl";
    case myvmp::ir::ShiftKind::Lsr:
        return "lsr";
    case myvmp::ir::ShiftKind::Asr:
        return "asr";
    case myvmp::ir::ShiftKind::Ror:
        return "ror";
    }
    return "unknown";
}

std::string condition_to_string(myvmp::ir::Condition condition) {
    return myvmp::ir::to_string(condition);
}

std::string ir_to_text(const myvmp::ir::Program& program) {
    std::ostringstream output;
    for (const auto& inst : program.instructions()) {
        output << myvmp::ir::to_string(inst.opcode)
               << " dst=" << static_cast<unsigned>(inst.dst)
               << " src=" << static_cast<unsigned>(inst.src)
               << " rhs=" << static_cast<unsigned>(inst.rhs)
               << " imm=" << inst.imm
               << " shift=" << shift_to_string(inst.shift_kind)
               << ":" << static_cast<unsigned>(inst.shift_amount)
               << " cond=" << condition_to_string(inst.condition)
               << " target=" << inst.target
               << '\n';
    }
    return output.str();
}

int lift_arm64(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: myvmp lift-arm64 [--seed N] [--base-va VA] <hex-word>...\n";
        return 2;
    }

    std::uint32_t seed = 0;
    std::uint64_t base_va = 0;
    int word_index = 2;
    while (word_index + 1 < argc) {
        const std::string option = argv[word_index];
        if (option == "--seed") {
            seed = parse_word(argv[word_index + 1]);
            word_index += 2;
        } else if (option == "--base-va") {
            base_va = parse_u64(argv[word_index + 1]);
            word_index += 2;
        } else {
            break;
        }
    }
    if (word_index >= argc) {
        std::cerr << "usage: myvmp lift-arm64 [--seed N] [--base-va VA] <hex-word>...\n";
        return 2;
    }

    std::vector<std::uint32_t> words;
    for (int i = word_index; i < argc; ++i) {
        words.push_back(parse_word(argv[i]));
    }

    const myvmp::arm64::Lifter lifter;
    const auto program = lifter.lift(words, base_va);
    const auto output = ir_to_text(program);
    if (env_flag_enabled("MYVMP_DUMP_IR")) {
        write_debug_log("lift-arm64.ir.log", output);
    }
    if (env_flag_enabled("MYVMP_DUMP_BYTECODE")) {
        const auto profile = seed == 0 ? myvmp::bytecode::fixed_profile() : myvmp::bytecode::profile_from_seed(seed);
        const auto key = seed == 0 ? 0xA5A5A5A5u : (seed ^ 0xA5A5A5A5u);
        const auto image = myvmp::bytecode::encode(program, {
            .key = key,
            .profile = profile,
            .diversify_immediates = seed != 0,
            .diversification_seed = seed,
            .shuffle_blocks = seed != 0,
        });
        write_debug_log("lift-arm64.bytecode.decoded.hex", hex_dump(myvmp::bytecode::decode_payload(image)));
        write_debug_log("lift-arm64.bytecode.encoded.hex", hex_dump(image.bytes));
        write_debug_log("lift-arm64.profile.json", profile.manifest() + "\n");
    }
    std::cout << output;
    write_debug_log("lift-arm64.log", output);
    return 0;
}

int lift_arm32(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: myvmp lift-arm32 <hex-a32-word>...\n";
        return 2;
    }
    std::vector<std::uint32_t> words;
    for (int i = 2; i < argc; ++i) words.push_back(parse_word(argv[i]));
    const auto program = myvmp::arm32::Lifter{}.lift(words);
    const auto output = ir_to_text(program);
    std::cout << output;
    write_debug_log("lift-arm32.ir.log", output);
    return 0;
}

int lift_riscv64(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: myvmp lift-rv64 <hex-rv64-word>...\n";
        return 2;
    }
    std::vector<std::uint32_t> words;
    for (int index = 2; index < argc; ++index) words.push_back(parse_word(argv[index]));
    const auto program = myvmp::riscv64::Lifter{}.lift(words);
    const auto output = ir_to_text(program);
    std::cout << output;
    write_debug_log("lift-rv64.ir.log", output);
    return 0;
}

int lift_elf_symbol(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "usage: myvmp lift-elf-symbol <aarch64-elf> <symbol>\n";
        return 2;
    }
    const auto elf = myvmp::elf::ElfFile::parse(argv[2]);
    const auto symbol = elf.find_symbol(argv[3]);
    if (!symbol.has_value()) {
        throw std::runtime_error("symbol not found: " + std::string(argv[3]));
    }
    const auto alignment = elf.machine() == myvmp::elf::Machine::RiscV ? 2u : 4u;
    if (symbol->size == 0 || (symbol->size % alignment) != 0) {
        throw std::runtime_error("function symbol size has invalid instruction alignment");
    }
    const auto bytes = elf.read_va(symbol->value, static_cast<std::size_t>(symbol->size));
    if (!bytes.has_value()) {
        throw std::runtime_error("function bytes are outside file-backed PT_LOAD segments");
    }
    myvmp::ir::Program program;
    if (elf.machine() == myvmp::elf::Machine::AArch64) {
        const myvmp::arm64::LiftContext context{
            .region_va = symbol->value,
            .read_memory = [&elf](std::uint64_t address, std::uint8_t width) { return elf.read_value_va(address, width); },
        };
        program = myvmp::arm64::Lifter{}.lift(*bytes, static_cast<std::size_t>(symbol->size / 4u), context);
    } else if (elf.machine() == myvmp::elf::Machine::RiscV) {
        program = myvmp::riscv64::Lifter{}.lift_bytes(
            *bytes, {.region_va = symbol->value, .instruction_offsets = {}});
    } else throw std::runtime_error("lift-elf-symbol requires AArch64 or RV64 ELF");
    const auto output = ir_to_text(program);
    std::cout << output;
    write_debug_log("lift-elf-symbol.ir.log", output);
    return 0;
}

int protect_dry_run(int argc, char** argv) {
    if (argc < 4 || argc > 7) {
        std::cerr << "usage: myvmp protect-dry-run <aarch64-elf> <symbol> [seed] [--multi-vm secondary-seed]\n";
        return 2;
    }
    const auto elf = myvmp::elf::ElfFile::parse(argv[2]);
    const auto symbol = elf.find_symbol(argv[3]);
    if (!symbol.has_value()) {
        throw std::runtime_error("symbol not found: " + std::string(argv[3]));
    }
    const auto seed = argc >= 5 ? parse_word(argv[4]) : 0u;
    std::optional<std::uint32_t> secondary_seed;
    if (argc > 5) {
        if (argc != 7 || std::string(argv[5]) != "--multi-vm") {
            throw std::runtime_error("expected --multi-vm <secondary-seed>");
        }
        secondary_seed = parse_word(argv[6]);
    }
    const auto artifact = elf.machine() == myvmp::elf::Machine::RiscV
        ? myvmp::protect::Riscv64Protector{}.dry_run(elf, *symbol, seed, secondary_seed)
        : myvmp::protect::Aarch64Protector{}.dry_run(elf, *symbol, seed, secondary_seed);
    std::ostringstream output;
    output << "symbol=" << symbol->name << '\n'
           << "liftable=" << (artifact.liftable ? "true" : "false") << '\n'
           << "reason=" << artifact.reason << '\n';
    if (artifact.liftable) {
        output << "native_size=" << artifact.original_bytes.size() << '\n'
               << "ir_instructions=" << artifact.program.instructions().size() << '\n'
               << "bytecode_size=" << artifact.bytecode.bytes.size() << '\n'
               << "profile_seed=" << artifact.bytecode.profile.seed << '\n'
               << "vm_mode=" << (artifact.sibling_bytecode ? "sibling" : "single") << '\n'
               << "detour_kind=" << myvmp::elf::to_string(artifact.patch_plan.detour_kind) << '\n'
               << "payload_va=0x" << std::hex << artifact.patch_plan.payload_va << std::dec << '\n'
               << "planned_entry_size=" << artifact.planned_entry_bytes.size() << '\n';
        write_debug_log("protect-dry-run.ir.log", ir_to_text(artifact.program));
        write_debug_log("protect-dry-run.bytecode.hex", hex_dump(artifact.bytecode.bytes));
        write_debug_log("protect-dry-run.profile.json", artifact.bytecode.profile.manifest() + "\n");
        write_debug_log("protect-dry-run.entry.hex", hex_dump(artifact.planned_entry_bytes));
    }
    std::cout << output.str();
    write_debug_log("protect-dry-run.log", output.str());
    return artifact.liftable ? 0 : 1;
}

int patch_aarch64_stub(int argc, char** argv) {
    if (argc < 6) {
        std::cerr << "usage: myvmp patch-aarch64-stub <input> <symbol> <output> <hex-word>...\n";
        return 2;
    }
    const auto elf = myvmp::elf::ElfFile::parse(argv[2]);
    const auto symbol = elf.find_symbol(argv[3]);
    if (!symbol.has_value()) {
        throw std::runtime_error("symbol not found: " + std::string(argv[3]));
    }
    std::vector<std::uint8_t> payload;
    for (int i = 5; i < argc; ++i) {
        const auto word = parse_word(argv[i]);
        payload.push_back(static_cast<std::uint8_t>(word));
        payload.push_back(static_cast<std::uint8_t>(word >> 8u));
        payload.push_back(static_cast<std::uint8_t>(word >> 16u));
        payload.push_back(static_cast<std::uint8_t>(word >> 24u));
    }
    const myvmp::patch::Aarch64ElfWriter writer;
    const auto result = writer.write(argv[2], argv[4], *symbol, payload);
    std::cout << "patched=" << (result.success ? "true" : "false") << '\n'
              << "reason=" << result.reason << '\n'
              << "payload_va=0x" << std::hex << result.payload_va << std::dec << '\n'
              << "payload_offset=0x" << std::hex << result.payload_offset << std::dec << '\n';
    return result.success ? 0 : 1;
}

myvmp::protect::ProtectionResult protect_one(const std::filesystem::path& input,
                                             const std::filesystem::path& output,
                                             const std::string& symbol,
                                             std::uint32_t seed,
                                             std::optional<std::uint32_t> secondary_seed);

int protect_function(int argc, char** argv) {
    if (argc < 5 || argc > 8) {
        std::cerr << "usage: myvmp protect <input> <symbol> <output> [seed] [--multi-vm secondary-seed]\n";
        return 2;
    }
    const auto seed = argc >= 6 ? parse_word(argv[5]) : 0u;
    std::optional<std::uint32_t> secondary_seed;
    if (argc > 6) {
        if (argc != 8 || std::string(argv[6]) != "--multi-vm") {
            throw std::runtime_error("expected --multi-vm <secondary-seed>");
        }
        secondary_seed = parse_word(argv[7]);
    }
    const auto result = protect_one(argv[2], argv[4], argv[3], seed, secondary_seed);
    std::cout << "protected=" << (result.success ? "true" : "false") << '\n'
              << "reason=" << result.reason << '\n';
    if (result.success) {
        std::cout << "native_size=" << result.artifact.original_bytes.size() << '\n'
                  << "bytecode_size=" << result.artifact.bytecode.bytes.size() << '\n'
                  << "bytecode_blocks=" << result.artifact.bytecode.blocks.size() << '\n'
                  << "vm_mode=" << (result.artifact.sibling_bytecode ? "sibling" : "single") << '\n'
                  << "profile_sidecar=" << argv[4] << ".myvmp.json\n"
                  << "payload_va=0x" << std::hex << result.patch.payload_va << std::dec << '\n'
                  << "payload_offset=0x" << std::hex << result.patch.payload_offset << std::dec << '\n';
    }
    return result.success ? 0 : 1;
}

myvmp::protect::ProtectionResult protect_one(const std::filesystem::path& input,
                                             const std::filesystem::path& output,
                                             const std::string& symbol,
                                             std::uint32_t seed,
                                             std::optional<std::uint32_t> secondary_seed) {
    const auto image = myvmp::elf::ElfFile::parse(input);
    switch (image.machine()) {
    case myvmp::elf::Machine::Arm:
        if (secondary_seed) throw std::runtime_error("sibling VM mode currently requires a 64-bit target");
        return myvmp::protect::Arm32Protector{}.protect_file(input, output, symbol, seed);
    case myvmp::elf::Machine::RiscV:
        return myvmp::protect::Riscv64Protector{}.protect_file(input, output, symbol, seed, secondary_seed);
    case myvmp::elf::Machine::AArch64:
        return myvmp::protect::Aarch64Protector{}.protect_file(input, output, symbol, seed, secondary_seed);
    case myvmp::elf::Machine::X86_64:
        throw std::runtime_error("x86-64 protection backend is not implemented yet");
    case myvmp::elf::Machine::Unknown:
        throw std::runtime_error("unsupported ELF machine");
    }
    throw std::runtime_error("unsupported ELF machine");
}

std::uint32_t batch_seed(std::uint32_t master, std::size_t index) {
    auto value = master ^ static_cast<std::uint32_t>(index * 0x9e3779b9u);
    value ^= value >> 16u; value *= 0x7feb352du;
    value ^= value >> 15u; value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value == 0 ? 0x6d79564du : value;
}

int protect_many(int argc, char** argv) {
    if (argc < 6) {
        std::cerr << "usage: myvmp protect-many <input> <output> <master-seed> "
                     "[--multi-vm secondary-master-seed] <symbol>...\n";
        return 2;
    }
    const std::filesystem::path input = argv[2];
    const std::filesystem::path output = argv[3];
    if (input == output) throw std::runtime_error("protect-many input and output paths must differ");
    if (std::filesystem::exists(output) ||
        std::filesystem::exists(output.string() + ".myvmp.json") ||
        std::filesystem::exists(output.string() + ".myvmp.batch.json"))
        throw std::runtime_error("protect-many refuses to overwrite existing output or manifests");
    const auto master_seed = parse_word(argv[4]);
    std::optional<std::uint32_t> secondary_master;
    int symbol_index = 5;
    if (std::string(argv[symbol_index]) == "--multi-vm") {
        if (argc < 8) throw std::runtime_error("protect-many --multi-vm needs a seed and symbols");
        secondary_master = parse_word(argv[symbol_index + 1]);
        symbol_index += 2;
    }
    if (symbol_index >= argc) throw std::runtime_error("protect-many needs at least one symbol");

    std::vector<std::filesystem::path> stages;
    std::vector<std::string> manifests;
    const auto final_profile = std::filesystem::path(output.string() + ".myvmp.json");
    const auto final_report = std::filesystem::path(output.string() + ".myvmp.batch.json");
    const auto temporary_report = std::filesystem::path(final_report.string() + ".tmp");
    bool metadata_published = false;
    auto current = input;
    try {
        for (int argument = symbol_index; argument < argc; ++argument) {
            const auto ordinal = static_cast<std::size_t>(argument - symbol_index);
            const auto stage = std::filesystem::path(output.string() + ".batch-" +
                                                     std::to_string(ordinal) + ".tmp");
            stages.push_back(stage);
            const auto primary = batch_seed(master_seed, ordinal);
            const auto secondary = secondary_master
                ? std::optional<std::uint32_t>(batch_seed(*secondary_master, ordinal))
                : std::nullopt;
            const auto result = protect_one(current, stage, argv[argument], primary, secondary);
            if (!result.success)
                throw std::runtime_error("symbol " + std::string(argv[argument]) + ": " + result.reason);
            std::ifstream profile(stage.string() + ".myvmp.json");
            if (!profile) throw std::runtime_error("cannot read generated profile sidecar");
            manifests.emplace_back(std::istreambuf_iterator<char>(profile),
                                   std::istreambuf_iterator<char>());
            current = stage;
            std::cout << "protected_symbol=" << argv[argument] << " seed=" << primary
                      << " bytecode_size=" << result.artifact.bytecode.bytes.size() << '\n';
        }
        {
            std::ofstream report(temporary_report, std::ios::trunc);
            report << "{\n  \"function_count\": " << manifests.size() << ",\n  \"profiles\": [\n";
            for (std::size_t index = 0; index < manifests.size(); ++index)
                report << manifests[index] << (index + 1 == manifests.size() ? "\n" : ",\n");
            report << "  ]\n}\n";
            if (!report) throw std::runtime_error("cannot prepare batch manifest");
        }
        std::error_code error;
        std::filesystem::rename(current.string() + ".myvmp.json", final_profile, error);
        if (error) throw std::runtime_error("cannot publish final profile sidecar: " + error.message());
        metadata_published = true;
        std::filesystem::rename(temporary_report, final_report, error);
        if (error) throw std::runtime_error("cannot publish batch manifest: " + error.message());
        std::filesystem::rename(current, output, error);
        if (error) throw std::runtime_error("cannot publish batch output: " + error.message());
        for (const auto& stage : stages) {
            if (stage != current) std::filesystem::remove(stage);
            std::filesystem::remove(stage.string() + ".myvmp.json");
        }
        std::cout << "protected=true\nfunction_count=" << manifests.size()
                  << "\nbatch_manifest=" << output.string() << ".myvmp.batch.json\n";
        return 0;
    } catch (...) {
        std::filesystem::remove(temporary_report);
        if (metadata_published) {
            std::filesystem::remove(final_profile);
            std::filesystem::remove(final_report);
        }
        for (const auto& stage : stages) {
            std::filesystem::remove(stage);
            std::filesystem::remove(stage.string() + ".myvmp.json");
        }
        throw;
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--version") {
            std::cout << "myVMP " << MYVMP_VERSION << '\n';
            return 0;
        }
        int command_index = 1;
        if (argc >= 4 && std::string(argv[1]) == "--log-dir") {
            g_log_dir = argv[2];
            command_index = 3;
        }

        std::vector<char*> command_argv;
        command_argv.push_back(argv[0]);
        for (int i = command_index; i < argc; ++i) {
            command_argv.push_back(argv[i]);
        }
        const auto command_argc = static_cast<int>(command_argv.size());

        if (command_argc >= 2 && std::string(command_argv[1]) == "inspect-elf") {
            return inspect_elf(command_argc, command_argv.data());
        }
        if (command_argc >= 2 && std::string(command_argv[1]) == "lift-arm64") {
            return lift_arm64(command_argc, command_argv.data());
        }
        if (command_argc >= 2 && std::string(command_argv[1]) == "lift-arm32") {
            return lift_arm32(command_argc, command_argv.data());
        }
        if (command_argc >= 2 && std::string(command_argv[1]) == "lift-rv64") {
            return lift_riscv64(command_argc, command_argv.data());
        }
        if (command_argc >= 2 && std::string(command_argv[1]) == "lift-elf-symbol") {
            return lift_elf_symbol(command_argc, command_argv.data());
        }
        if (command_argc >= 2 && std::string(command_argv[1]) == "protect-dry-run") {
            return protect_dry_run(command_argc, command_argv.data());
        }
        if (command_argc >= 2 && std::string(command_argv[1]) == "patch-aarch64-stub") {
            return patch_aarch64_stub(command_argc, command_argv.data());
        }
        if (command_argc >= 2 && std::string(command_argv[1]) == "protect") {
            return protect_function(command_argc, command_argv.data());
        }
        if (command_argc >= 2 && std::string(command_argv[1]) == "protect-many") {
            return protect_many(command_argc, command_argv.data());
        }
        if (command_argc >= 2 && std::string(command_argv[1]) == "quality-report") {
            return quality_report(command_argc, command_argv.data());
        }
        if (command_argc >= 2 && std::string(command_argv[1]) == "profile-manifest") {
            return profile_manifest(command_argc, command_argv.data());
        }
        std::cerr << "usage: myvmp [--log-dir <dir>] <inspect-elf|lift-arm32|lift-arm64|lift-rv64|lift-elf-symbol|protect-dry-run|protect|protect-many|patch-aarch64-stub|quality-report|profile-manifest> ...\n";
        return 2;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << '\n';
        return 1;
    }
}
