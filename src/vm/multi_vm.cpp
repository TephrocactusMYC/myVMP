#include "myvmp/vm/multi_vm.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <tuple>
#include <unordered_map>

namespace myvmp::vm {

void VmBridge::validate() const {
    if (source_profile_id == destination_profile_id) {
        throw std::runtime_error("VM bridge profiles must differ");
    }
    if (maximum_depth == 0 || depth >= maximum_depth) {
        throw std::runtime_error("VM bridge nesting depth exceeded");
    }
    if (destination_block_id == continuation_block_id && depth != 0) {
        throw std::runtime_error("recursive VM bridge cycle rejected");
    }
}

SiblingVmPartition plan_sibling_partition(const bytecode::Image& image,
                                          std::uint32_t primary_profile_id,
                                          std::uint32_t secondary_profile_id,
                                          std::uint32_t seed) {
    if (primary_profile_id == secondary_profile_id) {
        throw std::runtime_error("sibling VM profile identifiers must differ");
    }
    SiblingVmPartition result;
    result.primary_profile_id = primary_profile_id;
    result.secondary_profile_id = secondary_profile_id;
    std::unordered_map<std::uint32_t, bool> secondary;
    for (std::size_t index = 0; index < image.blocks.size(); ++index) {
        const auto& block = image.blocks[index];
        const bool use_secondary = index != 0 && (((block.id * 0x9e3779b9u) ^ seed) & 1u) != 0;
        if (!secondary.emplace(block.id, use_secondary).second) {
            throw std::runtime_error("sibling VM partition requires unique block IDs");
        }
        (use_secondary ? result.secondary_blocks : result.primary_blocks).push_back(block.id);
    }
    // Ensure a useful two-VM experiment whenever the CFG has more than one block.
    if (image.blocks.size() > 1 && result.secondary_blocks.empty()) {
        const auto id = image.blocks.back().id;
        result.primary_blocks.pop_back();
        result.secondary_blocks.push_back(id);
        secondary[id] = true;
    }
    for (const auto& block : image.blocks) {
        for (const auto successor : block.successors) {
            const auto target = secondary.find(successor);
            if (target == secondary.end()) {
                throw std::runtime_error("sibling VM partition references an unknown successor");
            }
            if (secondary.at(block.id) == target->second) continue;
            VmBridge bridge;
            bridge.source_profile_id = secondary.at(block.id) ? secondary_profile_id : primary_profile_id;
            bridge.destination_profile_id = target->second ? secondary_profile_id : primary_profile_id;
            bridge.continuation_block_id = block.id;
            bridge.live_gpr_mask = (std::uint64_t{1} << 31) - 1u;
            bridge.live_fp_mask = 0xffffffffu;
            bridge.sp_policy = StateSyncPolicy::CopyAll;
            bridge.nzcv_policy = StateSyncPolicy::CopyAll;
            bridge.payload_key_epoch = seed;
            bridge.destination_block_id = successor;
            bridge.validate();
            result.bridges.push_back(bridge);
        }
    }
    return result;
}

void SiblingVmImage::validate() const {
    if (image.blocks.size() != block_profile_ids.size()) {
        throw std::runtime_error("sibling VM block ownership count mismatch");
    }
    if (partition.primary_profile_id == partition.secondary_profile_id) {
        throw std::runtime_error("sibling VM profiles must be distinct");
    }
    if (primary_profile.seed != partition.primary_profile_id ||
        secondary_profile.seed != partition.secondary_profile_id) {
        throw std::runtime_error("sibling VM profile metadata is inconsistent");
    }
    if (partition.primary_blocks.empty() || partition.secondary_blocks.empty()) {
        throw std::runtime_error("sibling VM image must assign at least one block to each profile");
    }
    std::size_t expected_offset = 0;
    std::set<std::uint32_t> primary_ids;
    std::set<std::uint32_t> secondary_ids;
    std::unordered_map<std::uint32_t, std::uint32_t> owners_by_id;
    for (std::size_t index = 0; index < image.blocks.size(); ++index) {
        const auto& block = image.blocks[index];
        if (block.payload_offset != expected_offset) {
            throw std::runtime_error("sibling VM physical blocks must be contiguous");
        }
        expected_offset += block.payload_size;
        const auto owner = block_profile_ids[index];
        if (owner != partition.primary_profile_id && owner != partition.secondary_profile_id) {
            throw std::runtime_error("sibling VM block has an unknown profile owner");
        }
        if (!owners_by_id.emplace(block.id, owner).second) {
            throw std::runtime_error("sibling VM image contains duplicate block IDs");
        }
        (owner == partition.primary_profile_id ? primary_ids : secondary_ids).insert(block.id);
    }
    if (expected_offset != bytecode::decode_payload(image).size()) {
        throw std::runtime_error("sibling VM blocks do not cover the bytecode payload");
    }
    if (image.blocks.empty() || block_profile_ids.front() != partition.primary_profile_id) {
        throw std::runtime_error("sibling VM entry block must belong to the primary profile");
    }
    if (primary_ids != std::set<std::uint32_t>(partition.primary_blocks.begin(),
                                               partition.primary_blocks.end()) ||
        secondary_ids != std::set<std::uint32_t>(partition.secondary_blocks.begin(),
                                                 partition.secondary_blocks.end())) {
        throw std::runtime_error("sibling VM partition lists disagree with block ownership");
    }
    std::set<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t>> bridges;
    for (const auto& bridge : partition.bridges) {
        bridge.validate();
        const auto source = owners_by_id.find(bridge.continuation_block_id);
        const auto destination = owners_by_id.find(bridge.destination_block_id);
        if (source == owners_by_id.end() || destination == owners_by_id.end() ||
            source->second != bridge.source_profile_id ||
            destination->second != bridge.destination_profile_id) {
            throw std::runtime_error("sibling VM bridge metadata disagrees with block ownership");
        }
        if (!bridges.emplace(bridge.continuation_block_id, bridge.destination_block_id,
                             bridge.source_profile_id, bridge.destination_profile_id).second) {
            throw std::runtime_error("sibling VM image contains a duplicate bridge");
        }
    }
    for (const auto& block : image.blocks) {
        for (const auto successor : block.successors) {
            const auto destination = owners_by_id.find(successor);
            if (destination == owners_by_id.end()) {
                throw std::runtime_error("sibling VM block references an unknown successor");
            }
            const auto source_owner = owners_by_id.at(block.id);
            if (source_owner != destination->second &&
                !bridges.contains({block.id, successor, source_owner, destination->second})) {
                throw std::runtime_error("sibling VM cross-profile edge has no bridge");
            }
        }
    }
}

SiblingVmImage encode_sibling(const ir::Program& program, SiblingEncodeOptions options) {
    if (options.primary_seed == 0 || options.secondary_seed == 0 ||
        options.primary_seed == options.secondary_seed) {
        throw std::invalid_argument("sibling VM seeds must be distinct and nonzero");
    }
    const auto primary = bytecode::profile_from_seed(options.primary_seed);
    const auto secondary = bytecode::profile_from_seed(options.secondary_seed);
    const bytecode::EncodeOptions primary_options{
        .key = options.key,
        .profile = primary,
        .diversify_immediates = true,
        .diversification_seed = options.partition_seed,
        .shuffle_blocks = true,
    };
    auto primary_image = bytecode::encode(program, primary_options);
    if (primary_image.blocks.size() < 2) {
        throw std::invalid_argument("sibling VM mode requires a function with at least two CFG blocks");
    }
    auto secondary_image = bytecode::encode(program, {
        .key = options.key,
        .profile = secondary,
        .diversify_immediates = true,
        .diversification_seed = options.partition_seed,
        .shuffle_blocks = true,
    });
    if (primary_image.blocks.size() != secondary_image.blocks.size()) {
        throw std::runtime_error("sibling VM encoders produced different block counts");
    }
    auto primary_payload = bytecode::decode_payload(primary_image);
    const auto secondary_payload = bytecode::decode_payload(secondary_image);
    if (primary_payload.size() != secondary_payload.size()) {
        throw std::runtime_error("sibling VM encoders produced incompatible layouts");
    }
    const auto partition = plan_sibling_partition(primary_image, options.primary_seed,
                                                   options.secondary_seed,
                                                   options.partition_seed);
    const auto is_secondary = [&](std::uint32_t id) {
        return std::find(partition.secondary_blocks.begin(), partition.secondary_blocks.end(), id) !=
               partition.secondary_blocks.end();
    };
    std::vector<std::uint32_t> owners;
    owners.reserve(primary_image.blocks.size());
    for (std::size_t index = 0; index < primary_image.blocks.size(); ++index) {
        const auto& block = primary_image.blocks[index];
        const auto& peer = secondary_image.blocks[index];
        if (block.id != peer.id || block.payload_offset != peer.payload_offset ||
            block.payload_size != peer.payload_size || block.successors != peer.successors) {
            throw std::runtime_error("sibling VM encoders produced incompatible CFG layouts");
        }
        const bool secondary_owner = is_secondary(block.id);
        owners.push_back(secondary_owner ? options.secondary_seed : options.primary_seed);
        if (secondary_owner) {
            std::copy_n(secondary_payload.begin() + static_cast<std::ptrdiff_t>(block.payload_offset),
                        block.payload_size,
                        primary_payload.begin() + static_cast<std::ptrdiff_t>(block.payload_offset));
        }
    }
    bytecode::crypt_payload(primary_payload, options.key);
    std::copy(primary_payload.begin(), primary_payload.end(), primary_image.bytes.begin() + 5);
    SiblingVmImage result{std::move(primary_image), primary, secondary, partition, std::move(owners)};
    result.validate();
    return result;
}

bytecode::Image normalize_sibling_image(const SiblingVmImage& sibling,
                                        const bytecode::VmProfile& transport) {
    sibling.validate();
    auto output = sibling.image;
    auto payload = bytecode::decode_payload(output);
    for (std::size_t index = 0; index < output.blocks.size(); ++index) {
        const auto& block = output.blocks[index];
        const auto& source = sibling.block_profile_ids[index] == sibling.partition.primary_profile_id
                                 ? sibling.primary_profile : sibling.secondary_profile;
        for (std::size_t pc = block.payload_offset; pc < block.payload_offset + block.payload_size;) {
            const auto logical = source.logical(payload[pc]);
            payload[pc] = transport.physical(logical);
            pc += 1 + bytecode::operand_size(logical);
        }
    }
    output.profile = transport;
    bytecode::crypt_payload(payload, output.key);
    std::copy(payload.begin(), payload.end(), output.bytes.begin() + 5);
    return output;
}

} // namespace myvmp::vm
