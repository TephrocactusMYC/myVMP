#pragma once

#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace myvmp::elf {

enum class Machine {
    Unknown,
    Arm,
    AArch64,
    X86_64,
    RiscV,
};

enum class ElfClass {
    Elf32,
    Elf64,
};

struct Segment {
    std::uint32_t type{};
    std::uint32_t flags{};
    std::uint64_t offset{};
    std::uint64_t vaddr{};
    std::uint64_t filesz{};
    std::uint64_t memsz{};
    std::uint64_t align{};
};

struct Section {
    std::string name;
    std::uint32_t type{};
    std::uint64_t flags{};
    std::uint64_t addr{};
    std::uint64_t offset{};
    std::uint64_t size{};
};

struct Symbol {
    std::string name;
    std::uint8_t info{};
    std::uint16_t section_index{};
    std::uint64_t value{};
    std::uint64_t size{};
};

struct Relocation {
    std::uint64_t offset{};
    std::uint32_t type{};
    std::string symbol_name;
    std::int64_t addend{};
    std::string target_section;
    bool has_explicit_addend = false;
};

class ElfFile {
public:
    static ElfFile parse(const std::filesystem::path& path);

    [[nodiscard]] Machine machine() const noexcept;
    [[nodiscard]] ElfClass elf_class() const noexcept;
    [[nodiscard]] bool is_pie_or_shared() const noexcept;
    [[nodiscard]] std::uint64_t entry() const noexcept;
    [[nodiscard]] const std::vector<Segment>& segments() const noexcept;
    [[nodiscard]] const std::vector<Section>& sections() const noexcept;
    [[nodiscard]] const std::vector<Symbol>& symbols() const noexcept;
    [[nodiscard]] const std::vector<Relocation>& relocations() const noexcept;
    [[nodiscard]] std::vector<Relocation> relocations_in_range(std::uint64_t va,
                                                               std::uint64_t size) const;
    [[nodiscard]] std::optional<Symbol> find_symbol(const std::string& name) const;
    [[nodiscard]] std::optional<std::uint64_t> va_to_offset(std::uint64_t va) const;
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> read_va(std::uint64_t va,
                                                                   std::size_t size) const;
    [[nodiscard]] std::optional<std::uint64_t> read_value_va(std::uint64_t va,
                                                             std::uint8_t width) const;

private:
    Machine machine_ = Machine::Unknown;
    ElfClass elf_class_ = ElfClass::Elf64;
    bool pie_or_shared_ = false;
    std::uint64_t entry_ = 0;
    std::vector<Segment> segments_;
    std::vector<Section> sections_;
    std::vector<Symbol> symbols_;
    std::vector<Relocation> relocations_;
    std::vector<std::uint8_t> bytes_;
};

std::string to_string(Machine machine);
std::string to_string(ElfClass elf_class);

} // namespace myvmp::elf
