#include "myvmp/protect/sidecar.hpp"

#include <fstream>
#include <system_error>

namespace myvmp::protect {

bool publish_profile_sidecar(const std::filesystem::path& protected_output,
                             const bytecode::Image& image,
                             const std::optional<vm::SiblingVmImage>& sibling,
                             const ProtectionManifestOptions& options) {
    if (sibling) {
        try {
            sibling->validate();
        } catch (...) {
            return false;
        }
        if (sibling->image.blocks.size() != image.blocks.size() ||
            sibling->block_profile_ids.size() != image.blocks.size()) {
            return false;
        }
    }
    const auto path = std::filesystem::path(protected_output.string() + ".myvmp.json");
    const auto temporary = std::filesystem::path(path.string() + ".tmp");
    bool complete = false;
    {
        std::ofstream out(temporary, std::ios::trunc);
        if (!out) return false;
        out << "{\n"
            << "  \"architecture\": \"" << options.architecture << "\",\n"
            << "  \"mode\": \"" << (sibling ? "sibling-vm" : "single-vm") << "\",\n"
            << "  \"opcode_codec\": \"" << options.opcode_codec << "\",\n"
            << "  \"operand_codec\": \"" << options.operand_codec << "\",\n"
            << "  \"opcode_whitening\": \"" << options.opcode_whitening << "\",\n"
            << "  \"payload_encrypted\": true,\n"
            << "  \"runtime_code_size\": " << options.runtime_code_size << ",\n"
            << "  \"runtime_dispatch\": \"" << options.runtime_dispatch << "\",\n"
            << "  \"encrypted_bytecode_offset\": " << options.encrypted_bytecode_offset << ",\n"
            << "  \"branch_targets_are_offsets\": "
            << (image.branch_targets_are_offsets ? "true" : "false") << ",\n"
            << "  \"profile\": " << image.profile.manifest();
        if (sibling)
            out << ",\n  \"secondary_profile\": " << sibling->secondary_profile.manifest();
        out << ",\n  \"bridge_count\": "
            << (sibling ? sibling->partition.bridges.size() : 0) << ",\n"
            << "  \"blocks\": [";
        for (std::size_t index = 0; index < image.blocks.size(); ++index) {
            const auto& block = image.blocks[index];
            if (index != 0) out << ',';
            out << "\n    {\"id\": " << block.id
                << ", \"payload_offset\": " << block.payload_offset
                << ", \"payload_size\": " << block.payload_size
                << ", \"profile_id\": "
                << (sibling ? sibling->block_profile_ids[index] : image.profile.seed)
                << ", \"successors\": [";
            for (std::size_t successor = 0; successor < block.successors.size(); ++successor) {
                if (successor != 0) out << ',';
                out << block.successors[successor];
            }
            out << "]}";
        }
        out << "\n  ],\n  \"bridges\": [";
        if (sibling) {
            for (std::size_t index = 0; index < sibling->partition.bridges.size(); ++index) {
                const auto& bridge = sibling->partition.bridges[index];
                if (index != 0) out << ',';
                out << "\n    {\"source_profile_id\": " << bridge.source_profile_id
                    << ", \"destination_profile_id\": " << bridge.destination_profile_id
                    << ", \"source_block_id\": " << bridge.continuation_block_id
                    << ", \"continuation_block_id\": " << bridge.continuation_block_id
                    << ", \"destination_block_id\": " << bridge.destination_block_id
                    << ", \"maximum_depth\": " << static_cast<unsigned>(bridge.maximum_depth)
                    << "}";
            }
        }
        out << "\n  ]\n}\n";
        complete = static_cast<bool>(out);
    }
    std::error_code error;
    if (complete) std::filesystem::rename(temporary, path, error);
    if (!complete || error) std::filesystem::remove(temporary, error);
    return complete && !error;
}

} // namespace myvmp::protect
