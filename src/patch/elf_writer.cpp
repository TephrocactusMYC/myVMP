#include "myvmp/patch/elf_writer.hpp"

#include "myvmp/patch/aarch64_trampoline.hpp"
#include "myvmp/patch/riscv64_trampoline.hpp"

#include <elf.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <system_error>

namespace myvmp::patch {
namespace {

std::uint64_t align_up(std::uint64_t value, std::uint64_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

template <typename T>
T* object_at(std::vector<std::uint8_t>& bytes, std::uint64_t offset) {
    if (offset > bytes.size() || sizeof(T) > bytes.size() - static_cast<std::size_t>(offset)) {
        return nullptr;
    }
    return reinterpret_cast<T*>(bytes.data() + offset);
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

} // namespace

enum class Elf64Target { AArch64, Riscv64 };

template <Elf64Target Target>
ElfPatchResult write_elf64_payload(const std::filesystem::path& input,
                                          const std::filesystem::path& output,
                                          const elf::Symbol& function,
                                          const std::vector<std::uint8_t>& executable_payload,
                                          bool erase_original_body) {
    constexpr bool riscv64 = Target == Elf64Target::Riscv64;
    ElfPatchResult result;
    if (input == output) {
        result.reason = "input and output paths must differ";
        return result;
    }
    if (executable_payload.empty() || function.size < 4) {
        result.reason = "payload must be nonempty and function must hold a branch instruction";
        return result;
    }
    auto bytes = read_file(input);
    auto* ehdr = object_at<Elf64_Ehdr>(bytes, 0);
    if (ehdr == nullptr || std::memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0 ||
        ehdr->e_ident[EI_CLASS] != ELFCLASS64 || ehdr->e_ident[EI_DATA] != ELFDATA2LSB ||
        ehdr->e_machine != (riscv64 ? EM_RISCV : EM_AARCH64)) {
        result.reason = riscv64 ? "input is not a little-endian RV64 ELF image"
                                : "input is not a little-endian AArch64 ELF64 image";
        return result;
    }

    Elf64_Phdr* executable = nullptr;
    std::uint64_t next_file_offset = bytes.size();
    std::uint64_t next_va = ~0ull;
    std::uint64_t load_alignment = 0x1000;
    std::uint64_t maximum_load_end = 0;
    std::optional<std::uint16_t> riscv_attribute_phdr;
    std::optional<std::uint16_t> reusable_runtime_phdr;
    for (std::uint16_t i = 0; i < ehdr->e_phnum; ++i) {
        auto* phdr = object_at<Elf64_Phdr>(bytes, ehdr->e_phoff + static_cast<std::uint64_t>(i) * ehdr->e_phentsize);
        if (phdr == nullptr) {
            result.reason = "program header table is out of bounds";
            return result;
        }
        if (phdr->p_type == PT_LOAD && (phdr->p_flags & PF_X) != 0 &&
            function.value >= phdr->p_vaddr && function.value < phdr->p_vaddr + phdr->p_memsz) {
            executable = phdr;
        }
        if (phdr->p_type == PT_LOAD && phdr->p_align > load_alignment) {
            load_alignment = phdr->p_align;
        }
        if (phdr->p_type == PT_LOAD)
            maximum_load_end = std::max(maximum_load_end, phdr->p_vaddr + phdr->p_memsz);
        if (riscv64 && phdr->p_type == 0x70000003u) riscv_attribute_phdr = i;
    }
    if (executable == nullptr) {
        result.reason = "function is not inside an executable PT_LOAD segment";
        return result;
    }
    const auto executable_end = executable->p_offset + executable->p_filesz;
    for (std::uint16_t i = 0; i < ehdr->e_phnum; ++i) {
        auto* phdr = object_at<Elf64_Phdr>(bytes, ehdr->e_phoff + static_cast<std::uint64_t>(i) * ehdr->e_phentsize);
        if (phdr != nullptr && phdr->p_type == PT_LOAD && phdr->p_offset >= executable_end &&
            phdr->p_offset < next_file_offset && phdr != executable) {
            next_file_offset = phdr->p_offset;
            next_va = phdr->p_vaddr;
        }
    }

    if (riscv64) {
        for (std::uint16_t i = 0; i < ehdr->e_phnum; ++i) {
            const auto* phdr = object_at<Elf64_Phdr>(bytes,
                ehdr->e_phoff + static_cast<std::uint64_t>(i) * ehdr->e_phentsize);
            if (phdr != executable && phdr != nullptr && phdr->p_type == PT_LOAD &&
                (phdr->p_flags & (PF_R | PF_X | PF_W)) == (PF_R | PF_X) &&
                phdr->p_offset + phdr->p_filesz == bytes.size()) reusable_runtime_phdr = i;
        }
    }
    bool dedicated_segment = false;
    bool extending_runtime_segment = false;
    std::optional<std::uint16_t> dedicated_phdr;
    result.payload_offset = align_up(executable_end, 16);
    result.payload_va = executable->p_vaddr + (result.payload_offset - executable->p_offset);
    auto payload_end = result.payload_offset + executable_payload.size();
    auto payload_va_end = result.payload_va + executable_payload.size();
    if (riscv64 && next_va != ~0ull && payload_va_end > (next_va & ~(load_alignment - 1u)) &&
        (reusable_runtime_phdr || ehdr->e_type == ET_DYN)) {
        if (reusable_runtime_phdr) {
            const auto* segment = object_at<Elf64_Phdr>(bytes,
                ehdr->e_phoff + static_cast<std::uint64_t>(*reusable_runtime_phdr) * ehdr->e_phentsize);
            dedicated_segment = true;
            extending_runtime_segment = true;
            dedicated_phdr = reusable_runtime_phdr;
            result.payload_offset = align_up(segment->p_offset + segment->p_filesz, 16);
            result.payload_va = segment->p_vaddr + (result.payload_offset - segment->p_offset);
            payload_end = result.payload_offset + executable_payload.size();
            payload_va_end = result.payload_va + executable_payload.size();
        } else if (!riscv_attribute_phdr) {
            result.reason = "RV64 shared object needs a dedicated executable PT_LOAD but has no reusable attributes PHDR";
            return result;
        } else {
            dedicated_segment = true;
            dedicated_phdr = riscv_attribute_phdr;
            result.payload_offset = align_up(bytes.size(), load_alignment);
            result.payload_va = align_up(maximum_load_end, load_alignment);
            payload_end = result.payload_offset + executable_payload.size();
            payload_va_end = result.payload_va + executable_payload.size();
        }
    }
    if (!dedicated_segment && payload_va_end > next_va) {
        result.reason = "executable PT_LOAD has no safe virtual-address gap for payload";
        return result;
    }
    const auto function_offset = executable->p_offset + (function.value - executable->p_vaddr);
    if (function.size < 4 || function_offset > bytes.size() ||
        function.size > bytes.size() - function_offset) {
        result.reason = "function range is outside the executable segment file bytes";
        return result;
    }
    const auto branch = riscv64 ? encode_riscv64_jal(function.value, result.payload_va, 5)
                                : encode_aarch64_b(function.value, result.payload_va);
    const auto riscv_far_jump = riscv64 && !branch && function.size >= 8
        ? encode_riscv64_far_jump(function.value, result.payload_va, 5)
        : std::optional<std::array<std::uint32_t, 2>>{};
    const auto aarch64_page_jump = !riscv64 && !branch && function.size >= 12
        ? encode_aarch64_page_jump(function.value, result.payload_va, 16)
        : std::optional<std::array<std::uint32_t, 3>>{};
    const auto use_absolute_jump = !branch.has_value();
    if (use_absolute_jump && ((riscv64 && !riscv_far_jump) ||
                             (!riscv64 && !aarch64_page_jump &&
                              (ehdr->e_type == ET_DYN || function.size < 16)))) {
        result.reason = riscv64 ? "RV64 payload is outside AUIPC/JALR range or function has fewer than 8 entry bytes"
                        : ehdr->e_type == ET_DYN
                            ? "far AArch64 PIE payload requires a nearby position-independent veneer"
                            : "far AArch64 payload requires at least 16 entry bytes";
        return result;
    }
    if (!dedicated_segment && payload_end > next_file_offset) {
        const auto insertion_size = align_up(payload_end - next_file_offset, load_alignment);
        const auto original_shoff = ehdr->e_shoff;
        const auto original_shnum = ehdr->e_shnum;
        const auto original_shentsize = ehdr->e_shentsize;
        bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(next_file_offset),
                     static_cast<std::size_t>(insertion_size), 0);
        ehdr = object_at<Elf64_Ehdr>(bytes, 0);
        if (ehdr == nullptr) {
            result.reason = "ELF header disappeared while inserting payload space";
            return result;
        }
        if (original_shoff >= next_file_offset) {
            ehdr->e_shoff = original_shoff + insertion_size;
        }
        for (std::uint16_t i = 0; i < ehdr->e_phnum; ++i) {
            auto* phdr = object_at<Elf64_Phdr>(bytes,
                                              ehdr->e_phoff + static_cast<std::uint64_t>(i) * ehdr->e_phentsize);
            if (phdr == nullptr) {
                result.reason = "program header table is invalid after payload-space insertion";
                return result;
            }
            if (phdr->p_offset >= next_file_offset) {
                phdr->p_offset += insertion_size;
            }
        }
        for (std::uint16_t i = 0; i < original_shnum; ++i) {
            auto* shdr = object_at<Elf64_Shdr>(bytes,
                                              ehdr->e_shoff + static_cast<std::uint64_t>(i) * original_shentsize);
            if (shdr == nullptr) {
                result.reason = "section header table is invalid after payload-space insertion";
                return result;
            }
            if (shdr->sh_offset >= next_file_offset) {
                shdr->sh_offset += insertion_size;
            }
        }
        executable = nullptr;
        for (std::uint16_t i = 0; i < ehdr->e_phnum; ++i) {
            auto* phdr = object_at<Elf64_Phdr>(bytes,
                                              ehdr->e_phoff + static_cast<std::uint64_t>(i) * ehdr->e_phentsize);
            if (phdr != nullptr && phdr->p_type == PT_LOAD && (phdr->p_flags & PF_X) != 0 &&
                function.value >= phdr->p_vaddr && function.value < phdr->p_vaddr + phdr->p_memsz) {
                executable = phdr;
                break;
            }
        }
        if (executable == nullptr) {
            result.reason = "failed to restore executable segment after payload-space insertion";
            return result;
        }
    }
    if (payload_end > bytes.size()) {
        bytes.resize(static_cast<std::size_t>(payload_end), 0);
        ehdr = object_at<Elf64_Ehdr>(bytes, 0);
        executable = nullptr;
        for (std::uint16_t i = 0; i < ehdr->e_phnum; ++i) {
            auto* phdr = object_at<Elf64_Phdr>(bytes, ehdr->e_phoff + static_cast<std::uint64_t>(i) * ehdr->e_phentsize);
            if (phdr != nullptr && phdr->p_type == PT_LOAD && (phdr->p_flags & PF_X) != 0 &&
                function.value >= phdr->p_vaddr && function.value < phdr->p_vaddr + phdr->p_memsz) {
                executable = phdr;
                break;
            }
        }
        if (executable == nullptr) {
            result.reason = "failed to restore executable program header after resize";
            return result;
        }
    }
    std::copy(executable_payload.begin(), executable_payload.end(), bytes.begin() + result.payload_offset);
    std::size_t detour_size = 4;
    if (branch.has_value()) {
        std::memcpy(bytes.data() + function_offset, &*branch, sizeof(*branch));
    } else if (riscv64) {
        std::memcpy(bytes.data() + function_offset, riscv_far_jump->data(), 8);
        detour_size = 8;
    } else if (aarch64_page_jump) {
        std::memcpy(bytes.data() + function_offset, aarch64_page_jump->data(), 12);
        detour_size = 12;
    } else {
        const auto jump = encode_aarch64_absolute_jump(result.payload_va);
        std::copy(jump.begin(), jump.end(), bytes.begin() + static_cast<std::ptrdiff_t>(function_offset));
        detour_size = jump.size();
    }
    // Destroy the residual native implementation. Each remaining instruction becomes
    // a seed-dependent BRK so bypassing the entry detour fails closed and different VM
    // payloads do not leave an identical native tail behind.
    if (erase_original_body) {
        std::uint32_t fill_state = 0x9e3779b9u;
        for (const auto byte : executable_payload) {
            fill_state = (fill_state ^ byte) * 16777619u;
        }
        for (std::uint64_t offset = detour_size; offset < function.size; offset += 4) {
            fill_state = fill_state * 1664525u + 1013904223u;
            const auto trap = riscv64 ? 0x00100073u
                                      : 0xd4200000u | ((fill_state & 0xffffu) << 5u);
            if (riscv64 && function.size - offset < 4) {
                const std::uint16_t compressed_break = 0x9002u;
                std::memcpy(bytes.data() + function_offset + offset, &compressed_break,
                            sizeof(compressed_break));
            } else {
                std::memcpy(bytes.data() + function_offset + offset, &trap, sizeof(trap));
            }
        }
    }
    if (dedicated_segment) {
        auto* payload_phdr = object_at<Elf64_Phdr>(bytes,
            ehdr->e_phoff + static_cast<std::uint64_t>(*dedicated_phdr) * ehdr->e_phentsize);
        if (!payload_phdr) { result.reason = "RV64 reusable program header disappeared"; return result; }
        if (extending_runtime_segment) {
            payload_phdr->p_filesz = payload_end - payload_phdr->p_offset;
            payload_phdr->p_memsz = payload_phdr->p_filesz;
        } else {
            payload_phdr->p_type = PT_LOAD;
            payload_phdr->p_flags = PF_R | PF_X;
            payload_phdr->p_offset = result.payload_offset;
            payload_phdr->p_vaddr = result.payload_va;
            payload_phdr->p_paddr = result.payload_va;
            payload_phdr->p_filesz = executable_payload.size();
            payload_phdr->p_memsz = executable_payload.size();
            payload_phdr->p_align = load_alignment;
        }
        std::vector<Elf64_Phdr> headers;
        headers.reserve(ehdr->e_phnum);
        for (std::uint16_t index = 0; index < ehdr->e_phnum; ++index) {
            const auto* header = object_at<Elf64_Phdr>(bytes,
                ehdr->e_phoff + static_cast<std::uint64_t>(index) * ehdr->e_phentsize);
            if (!header) { result.reason = "RV64 program headers are invalid during LOAD sorting"; return result; }
            headers.push_back(*header);
        }
        std::vector<Elf64_Phdr> loads;
        for (const auto& header : headers)
            if (header.p_type == PT_LOAD) loads.push_back(header);
        std::sort(loads.begin(), loads.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.p_vaddr < rhs.p_vaddr;
        });
        std::size_t load_index = 0;
        for (auto& header : headers)
            if (header.p_type == PT_LOAD) header = loads[load_index++];
        for (std::uint16_t index = 0; index < ehdr->e_phnum; ++index) {
            auto* header = object_at<Elf64_Phdr>(bytes,
                ehdr->e_phoff + static_cast<std::uint64_t>(index) * ehdr->e_phentsize);
            *header = headers[index];
        }
    } else {
        executable->p_filesz = payload_end - executable->p_offset;
        executable->p_memsz = std::max(executable->p_memsz, payload_va_end - executable->p_vaddr);
    }

    const auto temporary = output.string() + ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream || !stream.write(reinterpret_cast<const char*>(bytes.data()),
                                     static_cast<std::streamsize>(bytes.size()))) {
            result.reason = "failed to write temporary patched ELF";
            return result;
        }
    }
    std::error_code error;
    std::filesystem::permissions(temporary, std::filesystem::status(input, error).permissions(), error);
    std::filesystem::rename(temporary, output, error);
    if (error) {
        std::filesystem::remove(temporary);
        result.reason = "failed to publish patched ELF: " + error.message();
        return result;
    }
    result.success = true;
    result.reason = "patched executable payload into PT_LOAD gap";
    return result;
}

ElfPatchResult Aarch64ElfWriter::write(const std::filesystem::path& input,
                                       const std::filesystem::path& output,
                                       const elf::Symbol& function,
                                       const std::vector<std::uint8_t>& executable_payload,
                                       bool erase_original_body) const {
    return write_elf64_payload<Elf64Target::AArch64>(
        input, output, function, executable_payload, erase_original_body);
}

ElfPatchResult Riscv64ElfWriter::write(const std::filesystem::path& input,
                                       const std::filesystem::path& output,
                                       const elf::Symbol& function,
                                       const std::vector<std::uint8_t>& executable_payload,
                                       bool erase_original_body) const {
    return write_elf64_payload<Elf64Target::Riscv64>(
        input, output, function, executable_payload, erase_original_body);
}

} // namespace myvmp::patch
