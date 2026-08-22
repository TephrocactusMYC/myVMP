#pragma once

#include "myvmp/bytecode/bytecode.hpp"

#include <cstdint>
#include <vector>

namespace myvmp::vm {

enum class StateSyncPolicy : std::uint8_t { Preserve, CopyLive, CopyAll };
enum class MemoryOrderBoundary : std::uint8_t { None, Acquire, Release, AcquireRelease, Sequential };

struct VmBridge {
    std::uint32_t source_profile_id{};
    std::uint32_t destination_profile_id{};
    std::uint32_t continuation_block_id{};
    std::uint64_t live_gpr_mask{};
    std::uint32_t live_fp_mask{};
    StateSyncPolicy sp_policy{StateSyncPolicy::CopyLive};
    StateSyncPolicy nzcv_policy{StateSyncPolicy::CopyLive};
    std::uint32_t payload_key_epoch{};
    std::uint32_t destination_block_id{};
    MemoryOrderBoundary memory_order{MemoryOrderBoundary::None};
    std::uint8_t depth{};
    std::uint8_t maximum_depth{2};

    void validate() const;
};

struct SiblingVmPartition {
    std::uint32_t primary_profile_id{};
    std::uint32_t secondary_profile_id{};
    std::vector<std::uint32_t> primary_blocks;
    std::vector<std::uint32_t> secondary_blocks;
    std::vector<VmBridge> bridges;
};

struct SiblingVmImage {
    bytecode::Image image;
    bytecode::VmProfile primary_profile;
    bytecode::VmProfile secondary_profile;
    SiblingVmPartition partition;
    // One profile ID per physical block in image.blocks.
    std::vector<std::uint32_t> block_profile_ids;

    void validate() const;
};

struct SiblingEncodeOptions {
    std::uint32_t primary_seed{};
    std::uint32_t secondary_seed{};
    std::uint32_t partition_seed{};
    std::uint32_t key{0xC0DEC0DEu};
};

SiblingVmImage encode_sibling(const ir::Program& program, SiblingEncodeOptions options);

// Converts profile-local physical opcodes into a chosen transport profile. Operands and
// explicit byte offsets remain unchanged, making this useful for host semantic execution.
bytecode::Image normalize_sibling_image(const SiblingVmImage& image,
                                        const bytecode::VmProfile& transport);

// Deterministically assigns non-entry blocks to two independently seeded sibling profiles.
// The entry remains in the primary VM; every cross-profile CFG edge gets an explicit bridge.
SiblingVmPartition plan_sibling_partition(const bytecode::Image& image,
                                          std::uint32_t primary_profile_id,
                                          std::uint32_t secondary_profile_id,
                                          std::uint32_t seed);

} // namespace myvmp::vm
