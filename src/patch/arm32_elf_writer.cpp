#include "myvmp/patch/elf_writer.hpp"

#include "myvmp/patch/arm32_trampoline.hpp"

#include <elf.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <system_error>

namespace myvmp::patch {
namespace {

template <typename T>
T* arm_object_at(std::vector<std::uint8_t>& bytes, std::uint64_t offset) {
    if (offset > bytes.size() || sizeof(T) > bytes.size() - static_cast<std::size_t>(offset)) return nullptr;
    return reinterpret_cast<T*>(bytes.data() + offset);
}

std::uint32_t arm_align_up(std::uint32_t value, std::uint32_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

std::vector<std::uint8_t> arm_read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

} // namespace

ElfPatchResult Arm32ElfWriter::write(const std::filesystem::path& input,
                                     const std::filesystem::path& output,
                                     const elf::Symbol& function,
                                     const std::vector<std::uint8_t>& payload,
                                     bool erase_original_body) const {
    ElfPatchResult result;
    if (input == output || payload.empty() || function.size < 4 || (function.value & 3u) != 0) {
        result.reason = "ARM32 input/output, payload, or ARM-state function is invalid";
        return result;
    }
    auto bytes = arm_read_file(input);
    auto* ehdr = arm_object_at<Elf32_Ehdr>(bytes, 0);
    if (!ehdr || std::memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0 ||
        ehdr->e_ident[EI_CLASS] != ELFCLASS32 || ehdr->e_ident[EI_DATA] != ELFDATA2LSB ||
        ehdr->e_machine != EM_ARM) {
        result.reason = "input is not a little-endian ARM ELF32 image";
        return result;
    }
    Elf32_Phdr* executable = nullptr;
    std::uint32_t next_offset = static_cast<std::uint32_t>(bytes.size());
    std::uint32_t next_va = UINT32_MAX;
    std::uint32_t alignment = 0x1000;
    for (std::uint16_t i = 0; i < ehdr->e_phnum; ++i) {
        auto* phdr = arm_object_at<Elf32_Phdr>(bytes, ehdr->e_phoff + i * ehdr->e_phentsize);
        if (!phdr) { result.reason = "ARM32 program header table is out of bounds"; return result; }
        if (phdr->p_type == PT_LOAD && (phdr->p_flags & PF_X) != 0 &&
            function.value >= phdr->p_vaddr && function.value < phdr->p_vaddr + phdr->p_memsz) executable = phdr;
        if (phdr->p_type == PT_LOAD) alignment = std::max(alignment, phdr->p_align);
    }
    if (!executable) { result.reason = "ARM32 function is not in an executable PT_LOAD"; return result; }
    const auto executable_end = executable->p_offset + executable->p_filesz;
    for (std::uint16_t i = 0; i < ehdr->e_phnum; ++i) {
        auto* phdr = arm_object_at<Elf32_Phdr>(bytes, ehdr->e_phoff + i * ehdr->e_phentsize);
        if (phdr && phdr != executable && phdr->p_type == PT_LOAD && phdr->p_offset >= executable_end && phdr->p_offset < next_offset) {
            next_offset = phdr->p_offset; next_va = phdr->p_vaddr;
        }
    }
    for (std::uint16_t i = 0; i < ehdr->e_shnum; ++i) {
        auto* shdr = arm_object_at<Elf32_Shdr>(bytes, ehdr->e_shoff + i * ehdr->e_shentsize);
        if (!shdr) { result.reason = "ARM32 section header table is out of bounds"; return result; }
        if (shdr->sh_type != SHT_NOBITS && shdr->sh_offset >= executable_end &&
            shdr->sh_offset < next_offset) next_offset = shdr->sh_offset;
    }
    result.payload_offset = arm_align_up(executable_end, 16);
    result.payload_va = executable->p_vaddr + (result.payload_offset - executable->p_offset);
    const auto payload_end = static_cast<std::uint32_t>(result.payload_offset + payload.size());
    const auto payload_va_end = static_cast<std::uint32_t>(result.payload_va + payload.size());
    if (payload_va_end > next_va) { result.reason = "ARM32 executable segment has no safe payload VA gap"; return result; }
    const auto branch = encode_arm32_b(function.value, result.payload_va);
    if (!branch) { result.reason = "ARM32 payload is outside direct branch range"; return result; }
    const auto function_offset = executable->p_offset + static_cast<std::uint32_t>(function.value - executable->p_vaddr);
    if (function_offset > bytes.size() || function.size > bytes.size() - function_offset) {
        result.reason = "ARM32 function bytes are out of bounds"; return result;
    }
    if (payload_end > next_offset) {
        const auto insertion = arm_align_up(payload_end - next_offset, alignment);
        const auto old_shoff = ehdr->e_shoff;
        const auto shnum = ehdr->e_shnum;
        const auto shentsize = ehdr->e_shentsize;
        bytes.insert(bytes.begin() + next_offset, insertion, 0);
        ehdr = arm_object_at<Elf32_Ehdr>(bytes, 0);
        if (old_shoff >= next_offset) ehdr->e_shoff = old_shoff + insertion;
        for (std::uint16_t i = 0; i < ehdr->e_phnum; ++i) {
            auto* phdr = arm_object_at<Elf32_Phdr>(bytes, ehdr->e_phoff + i * ehdr->e_phentsize);
            if (phdr->p_offset >= next_offset) phdr->p_offset += insertion;
        }
        for (std::uint16_t i = 0; i < shnum; ++i) {
            auto* shdr = arm_object_at<Elf32_Shdr>(bytes, ehdr->e_shoff + i * shentsize);
            if (!shdr) { result.reason = "ARM32 section headers are invalid after insertion"; return result; }
            if (shdr->sh_offset >= next_offset && shdr->sh_type != SHT_NOBITS) shdr->sh_offset += insertion;
        }
        executable = nullptr;
        for (std::uint16_t i = 0; i < ehdr->e_phnum; ++i) {
            auto* phdr = arm_object_at<Elf32_Phdr>(bytes, ehdr->e_phoff + i * ehdr->e_phentsize);
            if (phdr->p_type == PT_LOAD && (phdr->p_flags & PF_X) != 0 && function.value >= phdr->p_vaddr && function.value < phdr->p_vaddr + phdr->p_memsz) { executable = phdr; break; }
        }
    }
    if (payload_end > bytes.size()) bytes.resize(payload_end, 0);
    ehdr = arm_object_at<Elf32_Ehdr>(bytes, 0);
    executable = nullptr;
    for (std::uint16_t i = 0; i < ehdr->e_phnum; ++i) {
        auto* phdr = arm_object_at<Elf32_Phdr>(bytes, ehdr->e_phoff + i * ehdr->e_phentsize);
        if (phdr->p_type == PT_LOAD && (phdr->p_flags & PF_X) != 0 && function.value >= phdr->p_vaddr && function.value < phdr->p_vaddr + phdr->p_memsz) { executable = phdr; break; }
    }
    if (!executable) { result.reason = "ARM32 executable segment disappeared"; return result; }
    std::copy(payload.begin(), payload.end(), bytes.begin() + result.payload_offset);
    std::memcpy(bytes.data() + function_offset, &*branch, 4);
    if (erase_original_body) {
        for (std::uint64_t offset = 4; offset < function.size; offset += 4) {
            const std::uint32_t udf = 0xe7f000f0u;
            std::memcpy(bytes.data() + function_offset + offset, &udf, 4);
        }
    }
    executable->p_filesz = payload_end - executable->p_offset;
    executable->p_memsz = std::max(executable->p_memsz, payload_va_end - executable->p_vaddr);
    const auto temporary = output.string() + ".tmp";
    { std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
      if (!stream || !stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size())) {
          result.reason = "failed to write ARM32 temporary ELF"; return result;
      } }
    std::error_code error;
    std::filesystem::permissions(temporary, std::filesystem::status(input, error).permissions(), error);
    std::filesystem::rename(temporary, output, error);
    if (error) { std::filesystem::remove(temporary); result.reason = "failed to publish ARM32 ELF: " + error.message(); return result; }
    result.success = true;
    result.reason = "patched ARM32 VM payload into executable PT_LOAD";
    return result;
}

} // namespace myvmp::patch
