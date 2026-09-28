#pragma once

#include "myvmp/bytecode/bytecode.hpp"
#include "myvmp/ir/analysis.hpp"
#include "myvmp/vm/state_encoding.hpp"

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
    std::uint32_t live_vector_mask{};
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
    // Destination live-ins indexed by stable CFG block ID; used for dynamic call returns.
    std::vector<ir::BlockLiveness> block_liveness;
    // Host-interpreter models for distinct per-profile state layouts. Native runtimes
    // do not consume these descriptors yet.
    StateRepresentation primary_state_representation;
    StateRepresentation secondary_state_representation;

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

// Legacy deterministic two-way hash partition retained for format compatibility and tests.
SiblingVmPartition plan_sibling_partition(const bytecode::Image& image,
                                          std::uint32_t primary_profile_id,
                                          std::uint32_t secondary_profile_id,
                                          std::uint32_t seed);

// CFG/liveness-aware partition used by protection. It keeps loop backedges and expensive
// live-state boundaries together when useful, while seeking balanced ownership and useful
// cold-edge diversity. The entry remains primary and every cross-profile edge has a bridge.
SiblingVmPartition plan_sibling_partition(const bytecode::Image& image,
                                          const ir::CFGAnalysis& analysis,
                                          std::uint32_t primary_profile_id,
                                          std::uint32_t secondary_profile_id,
                                          std::uint32_t seed);

} // namespace myvmp::vm
