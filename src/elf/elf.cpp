#include "myvmp/elf/elf.hpp"

#include <elf.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace myvmp::elf {
namespace {

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("failed to open ELF file");
    }
    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    if (size < 0) {
        throw std::runtime_error("failed to determine ELF size");
    }
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!in && !bytes.empty()) {
        throw std::runtime_error("failed to read ELF file");
    }
    return bytes;
}

template <typename T>
T read_struct(const std::vector<std::uint8_t>& bytes, std::uint64_t offset) {
    if (offset > bytes.size() || sizeof(T) > bytes.size() - static_cast<std::size_t>(offset)) {
        throw std::runtime_error("ELF structure is out of bounds");
    }
    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

void validate_table(const std::vector<std::uint8_t>& bytes,
                    std::uint64_t offset,
                    std::uint64_t count,
                    std::uint64_t entry_size,
                    std::uint64_t minimum_entry_size,
                    const char* description) {
    if (count == 0) return;
    if (entry_size < minimum_entry_size) {
        throw std::runtime_error(std::string(description) + " entry is too small");
    }
    if (offset > bytes.size() || count > (bytes.size() - static_cast<std::size_t>(offset)) / entry_size) {
        throw std::runtime_error(std::string(description) + " is out of bounds");
    }
}

template <typename Phdr>
void validate_segment(const std::vector<std::uint8_t>& bytes, const Phdr& phdr) {
    if (phdr.p_filesz != 0 &&
        (phdr.p_offset > bytes.size() || phdr.p_filesz > bytes.size() - phdr.p_offset)) {
        throw std::runtime_error("ELF segment file range is out of bounds");
    }
    if (phdr.p_type == PT_LOAD && phdr.p_filesz > phdr.p_memsz) {
        throw std::runtime_error("ELF load segment file size exceeds memory size");
    }
    if (phdr.p_memsz > std::numeric_limits<decltype(phdr.p_vaddr)>::max() - phdr.p_vaddr) {
        throw std::runtime_error("ELF segment virtual range overflows");
    }
}

std::string read_cstr(const std::vector<std::uint8_t>& bytes, std::uint64_t base, std::uint32_t offset) {
    if (base > bytes.size() || offset > bytes.size() - static_cast<std::size_t>(base)) return {};
    const auto start = base + offset;
    if (start >= bytes.size()) {
        return {};
    }
    const auto* ptr = reinterpret_cast<const char*>(bytes.data() + start);
    const auto max_len = bytes.size() - static_cast<std::size_t>(start);
    const auto len = strnlen(ptr, max_len);
    return std::string(ptr, len);
}

Machine machine_from_elf(std::uint16_t machine) {
    switch (machine) {
    case EM_ARM:
        return Machine::Arm;
    case EM_AARCH64:
        return Machine::AArch64;
    case EM_X86_64:
        return Machine::X86_64;
    case EM_RISCV:
        return Machine::RiscV;
    default:
        return Machine::Unknown;
    }
}

} // namespace

ElfFile ElfFile::parse(const std::filesystem::path& path) {
    auto bytes = read_file(path);
    if (bytes.size() < EI_NIDENT) {
        throw std::runtime_error("file is too small for ELF identification");
    }

    if (bytes[EI_MAG0] != ELFMAG0 || bytes[EI_MAG1] != ELFMAG1 ||
        bytes[EI_MAG2] != ELFMAG2 || bytes[EI_MAG3] != ELFMAG3) {
        throw std::runtime_error("file is not an ELF object");
    }
    if (bytes[EI_DATA] != ELFDATA2LSB) {
        throw std::runtime_error("only little-endian ELF is supported");
    }
    if (bytes[EI_CLASS] == ELFCLASS32) {
        if (bytes.size() < sizeof(Elf32_Ehdr)) {
            throw std::runtime_error("file is too small for ELF32 header");
        }
        const auto header = read_struct<Elf32_Ehdr>(bytes, 0);
        ElfFile out;
        out.elf_class_ = ElfClass::Elf32;
        out.machine_ = machine_from_elf(header.e_machine);
        out.pie_or_shared_ = header.e_type == ET_DYN;
        out.entry_ = header.e_entry;
        validate_table(bytes, header.e_phoff, header.e_phnum, header.e_phentsize,
                       sizeof(Elf32_Phdr), "ELF32 program header table");
        validate_table(bytes, header.e_shoff, header.e_shnum, header.e_shentsize,
                       sizeof(Elf32_Shdr), "ELF32 section header table");
        for (std::uint16_t i = 0; i < header.e_phnum; ++i) {
            const auto phdr = read_struct<Elf32_Phdr>(bytes, header.e_phoff + static_cast<std::uint64_t>(i) * header.e_phentsize);
            validate_segment(bytes, phdr);
            out.segments_.push_back({phdr.p_type, phdr.p_flags, phdr.p_offset, phdr.p_vaddr,
                                     phdr.p_filesz, phdr.p_memsz, phdr.p_align});
        }
        std::uint64_t shstr_base = 0;
        if (header.e_shstrndx != SHN_UNDEF && header.e_shstrndx < header.e_shnum) {
            const auto shstr = read_struct<Elf32_Shdr>(bytes, header.e_shoff + static_cast<std::uint64_t>(header.e_shstrndx) * header.e_shentsize);
            shstr_base = shstr.sh_offset;
        }
        std::vector<Elf32_Shdr> raw_sections;
        raw_sections.reserve(header.e_shnum);
        for (std::uint16_t i = 0; i < header.e_shnum; ++i) {
            const auto shdr = read_struct<Elf32_Shdr>(bytes, header.e_shoff + static_cast<std::uint64_t>(i) * header.e_shentsize);
            raw_sections.push_back(shdr);
            out.sections_.push_back({read_cstr(bytes, shstr_base, shdr.sh_name), shdr.sh_type,
                                     shdr.sh_flags, shdr.sh_addr, shdr.sh_offset, shdr.sh_size});
        }
        std::vector<std::vector<std::string>> symbol_names(raw_sections.size());
        for (std::size_t section_index = 0; section_index < raw_sections.size(); ++section_index) {
            const auto& shdr = raw_sections[section_index];
            if ((shdr.sh_type != SHT_SYMTAB && shdr.sh_type != SHT_DYNSYM) ||
                shdr.sh_entsize == 0 || shdr.sh_link >= raw_sections.size()) continue;
            const auto& strtab = raw_sections[shdr.sh_link];
            const auto count = shdr.sh_size / shdr.sh_entsize;
            validate_table(bytes, shdr.sh_offset, count, shdr.sh_entsize,
                           sizeof(Elf32_Sym), "ELF32 symbol table");
            symbol_names[section_index].resize(count);
            for (std::uint64_t i = 0; i < count; ++i) {
                const auto& sym = read_struct<Elf32_Sym>(bytes, shdr.sh_offset + i * shdr.sh_entsize);
                const auto name = read_cstr(bytes, strtab.sh_offset, sym.st_name);
                symbol_names[section_index][i] = name;
                if (!name.empty()) out.symbols_.push_back({name, sym.st_info, sym.st_shndx, sym.st_value, sym.st_size});
            }
        }
        for (const auto& shdr : raw_sections) {
            if ((shdr.sh_type != SHT_RELA && shdr.sh_type != SHT_REL) || shdr.sh_entsize == 0 ||
                shdr.sh_link >= symbol_names.size()) continue;
            const auto target_section = shdr.sh_info < out.sections_.size() ? out.sections_[shdr.sh_info].name : std::string{};
            const auto count = shdr.sh_size / shdr.sh_entsize;
            validate_table(bytes, shdr.sh_offset, count, shdr.sh_entsize,
                           shdr.sh_type == SHT_RELA ? sizeof(Elf32_Rela) : sizeof(Elf32_Rel),
                           "ELF32 relocation table");
            for (std::uint64_t i = 0; i < count; ++i) {
                const auto entry_offset = shdr.sh_offset + i * shdr.sh_entsize;
                std::uint32_t offset = 0, info = 0;
                std::int64_t addend = 0;
                bool explicit_addend = false;
                if (shdr.sh_type == SHT_RELA) {
                    const auto& rela = read_struct<Elf32_Rela>(bytes, entry_offset);
                    offset = rela.r_offset; info = rela.r_info; addend = rela.r_addend; explicit_addend = true;
                } else {
                    const auto& rel = read_struct<Elf32_Rel>(bytes, entry_offset);
                    offset = rel.r_offset; info = rel.r_info;
                }
                if (header.e_type == ET_REL && shdr.sh_info < raw_sections.size()) offset += raw_sections[shdr.sh_info].sh_addr;
                const auto symbol_index = static_cast<std::size_t>(ELF32_R_SYM(info));
                const auto& names = symbol_names[shdr.sh_link];
                out.relocations_.push_back({offset, static_cast<std::uint32_t>(ELF32_R_TYPE(info)),
                    symbol_index < names.size() ? names[symbol_index] : std::string{}, addend,
                    target_section, explicit_addend});
            }
        }
        out.bytes_ = std::move(bytes);
        return out;
    }
    if (bytes[EI_CLASS] != ELFCLASS64) {
        throw std::runtime_error("unsupported ELF class");
    }
    if (bytes.size() < sizeof(Elf64_Ehdr)) {
        throw std::runtime_error("file is too small for ELF64 header");
    }
    const auto ehdr = read_struct<Elf64_Ehdr>(bytes, 0);

    ElfFile out;
    out.elf_class_ = ElfClass::Elf64;
    out.machine_ = machine_from_elf(ehdr.e_machine);
    out.pie_or_shared_ = ehdr.e_type == ET_DYN;
    out.entry_ = ehdr.e_entry;

    validate_table(bytes, ehdr.e_phoff, ehdr.e_phnum, ehdr.e_phentsize,
                   sizeof(Elf64_Phdr), "ELF64 program header table");
    validate_table(bytes, ehdr.e_shoff, ehdr.e_shnum, ehdr.e_shentsize,
                   sizeof(Elf64_Shdr), "ELF64 section header table");
    for (std::uint16_t i = 0; i < ehdr.e_phnum; ++i) {
        const auto offset = ehdr.e_phoff + static_cast<std::uint64_t>(i) * ehdr.e_phentsize;
        const auto phdr = read_struct<Elf64_Phdr>(bytes, offset);
        validate_segment(bytes, phdr);
        out.segments_.push_back({
            phdr.p_type,
            phdr.p_flags,
            phdr.p_offset,
            phdr.p_vaddr,
            phdr.p_filesz,
            phdr.p_memsz,
            phdr.p_align,
        });
    }

    std::uint64_t shstr_base = 0;
    if (ehdr.e_shstrndx != SHN_UNDEF && ehdr.e_shstrndx < ehdr.e_shnum) {
        const auto offset = ehdr.e_shoff + static_cast<std::uint64_t>(ehdr.e_shstrndx) * ehdr.e_shentsize;
        const auto shstr = read_struct<Elf64_Shdr>(bytes, offset);
        shstr_base = shstr.sh_offset;
    }

    std::vector<Elf64_Shdr> raw_sections;
    raw_sections.reserve(ehdr.e_shnum);
    for (std::uint16_t i = 0; i < ehdr.e_shnum; ++i) {
        const auto offset = ehdr.e_shoff + static_cast<std::uint64_t>(i) * ehdr.e_shentsize;
        const auto shdr = read_struct<Elf64_Shdr>(bytes, offset);
        raw_sections.push_back(shdr);
        out.sections_.push_back({
            read_cstr(bytes, shstr_base, shdr.sh_name),
            shdr.sh_type,
            shdr.sh_flags,
            shdr.sh_addr,
            shdr.sh_offset,
            shdr.sh_size,
        });
    }

    std::vector<std::vector<std::string>> symbol_names(raw_sections.size());
    for (std::size_t section_index = 0; section_index < raw_sections.size(); ++section_index) {
        const auto& shdr = raw_sections[section_index];
        if (shdr.sh_type != SHT_SYMTAB && shdr.sh_type != SHT_DYNSYM) {
            continue;
        }
        if (shdr.sh_entsize == 0 || shdr.sh_link >= raw_sections.size()) {
            continue;
        }
        const auto& strtab = raw_sections[shdr.sh_link];
        const auto count = shdr.sh_size / shdr.sh_entsize;
        validate_table(bytes, shdr.sh_offset, count, shdr.sh_entsize,
                       sizeof(Elf64_Sym), "ELF64 symbol table");
        symbol_names[section_index].resize(static_cast<std::size_t>(count));
        for (std::uint64_t i = 0; i < count; ++i) {
            const auto offset = shdr.sh_offset + i * shdr.sh_entsize;
            const auto& sym = read_struct<Elf64_Sym>(bytes, offset);
            const auto name = read_cstr(bytes, strtab.sh_offset, sym.st_name);
            symbol_names[section_index][static_cast<std::size_t>(i)] = name;
            if (name.empty()) {
                continue;
            }
            out.symbols_.push_back({name, sym.st_info, sym.st_shndx, sym.st_value, sym.st_size});
        }
    }

    for (const auto& shdr : raw_sections) {
        if ((shdr.sh_type != SHT_RELA && shdr.sh_type != SHT_REL) || shdr.sh_entsize == 0 ||
            shdr.sh_link >= symbol_names.size()) {
            continue;
        }
        const auto count = shdr.sh_size / shdr.sh_entsize;
        validate_table(bytes, shdr.sh_offset, count, shdr.sh_entsize,
                       shdr.sh_type == SHT_RELA ? sizeof(Elf64_Rela) : sizeof(Elf64_Rel),
                       "ELF64 relocation table");
        const auto target_section = shdr.sh_info < out.sections_.size()
                                        ? out.sections_[shdr.sh_info].name
                                        : std::string{};
        for (std::uint64_t i = 0; i < count; ++i) {
            const auto entry_offset = shdr.sh_offset + i * shdr.sh_entsize;
            std::uint64_t offset = 0;
            std::uint64_t info = 0;
            std::int64_t addend = 0;
            bool explicit_addend = false;
            if (shdr.sh_type == SHT_RELA) {
                const auto& rela = read_struct<Elf64_Rela>(bytes, entry_offset);
                offset = rela.r_offset;
                info = rela.r_info;
                addend = rela.r_addend;
                explicit_addend = true;
            } else {
                const auto& rel = read_struct<Elf64_Rel>(bytes, entry_offset);
                offset = rel.r_offset;
                info = rel.r_info;
            }
            if (ehdr.e_type == ET_REL && shdr.sh_info < raw_sections.size()) {
                offset += raw_sections[shdr.sh_info].sh_addr;
            }
            const auto symbol_index = static_cast<std::size_t>(ELF64_R_SYM(info));
            const auto& names = symbol_names[shdr.sh_link];
            const auto symbol_name = symbol_index < names.size() ? names[symbol_index] : std::string{};
            out.relocations_.push_back({offset,
                                        static_cast<std::uint32_t>(ELF64_R_TYPE(info)),
                                        symbol_name,
                                        addend,
                                        target_section,
                                        explicit_addend});
        }
    }

    out.bytes_ = std::move(bytes);
    return out;
}

Machine ElfFile::machine() const noexcept {
    return machine_;
}

ElfClass ElfFile::elf_class() const noexcept {
    return elf_class_;
}

bool ElfFile::is_pie_or_shared() const noexcept {
    return pie_or_shared_;
}

std::uint64_t ElfFile::entry() const noexcept {
    return entry_;
}

const std::vector<Segment>& ElfFile::segments() const noexcept {
    return segments_;
}

const std::vector<Section>& ElfFile::sections() const noexcept {
    return sections_;
}

const std::vector<Symbol>& ElfFile::symbols() const noexcept {
    return symbols_;
}

const std::vector<Relocation>& ElfFile::relocations() const noexcept {
    return relocations_;
}

std::vector<Relocation> ElfFile::relocations_in_range(std::uint64_t va, std::uint64_t size) const {
    std::vector<Relocation> result;
    if (size == 0) {
        return result;
    }
    for (const auto& relocation : relocations_) {
        if (relocation.offset >= va && relocation.offset - va < size) {
            result.push_back(relocation);
        }
    }
    return result;
}

std::optional<Symbol> ElfFile::find_symbol(const std::string& name) const {
    const auto it = std::find_if(symbols_.begin(), symbols_.end(), [&](const Symbol& symbol) {
        return symbol.name == name;
    });
    if (it == symbols_.end()) {
        return std::nullopt;
    }
    return *it;
}

std::optional<std::uint64_t> ElfFile::va_to_offset(std::uint64_t va) const {
    for (const auto& segment : segments_) {
        if (segment.type != PT_LOAD) {
            continue;
        }
        if (va >= segment.vaddr && va < segment.vaddr + segment.filesz) {
            return segment.offset + (va - segment.vaddr);
        }
    }
    return std::nullopt;
}

std::optional<std::vector<std::uint8_t>> ElfFile::read_va(std::uint64_t va, std::size_t size) const {
    const auto offset = va_to_offset(va);
    if (!offset.has_value() || *offset > bytes_.size() || size > bytes_.size() - *offset) {
        return std::nullopt;
    }
    return std::vector<std::uint8_t>(bytes_.begin() + static_cast<std::ptrdiff_t>(*offset),
                                     bytes_.begin() + static_cast<std::ptrdiff_t>(*offset + size));
}

std::optional<std::uint64_t> ElfFile::read_value_va(std::uint64_t va, std::uint8_t width) const {
    if (width != 1 && width != 2 && width != 4 && width != 8) {
        return std::nullopt;
    }
    const auto bytes = read_va(va, width);
    if (!bytes.has_value()) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < bytes->size(); ++i) {
        value |= static_cast<std::uint64_t>((*bytes)[i]) << (i * 8u);
    }
    return value;
}

std::string to_string(Machine machine) {
    switch (machine) {
    case Machine::Arm:
        return "ARM";
    case Machine::AArch64:
        return "AArch64";
    case Machine::X86_64:
        return "x86_64";
    case Machine::RiscV:
        return "RISC-V";
    case Machine::Unknown:
        return "unknown";
    }
    return "unknown";
}

std::string to_string(ElfClass elf_class) {
    return elf_class == ElfClass::Elf32 ? "ELF32" : "ELF64";
}

} // namespace myvmp::elf
