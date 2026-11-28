/**
 * @file world/options.cpp
 * @brief The world options
 * @author 60Trees_ (github.com/60Trees)
 * @detail
 * This file has a bunch of options for the world.
 * It is designed to compile quickly and easy to edit.
 */

#include <cstddef>
#include <world/shared.hpp>

const size_t* default_chunk_size = new size_t(64);

namespace {
    inline bool is_power_of_two(size_t value) { return value > 0 && (value & (value - 1)) == 0; }

    /// @brief Number of child chunks per axis between two adjacent LODs.
    inline int lod_ratio(const LodDescriptor& parent, const LodDescriptor& child) {
        tassert(parent.size() >= child.size());
        return static_cast<int>(parent.size() / child.size());
    }

    /// @brief Every offset inside one parent chunk, expressed in child-chunk units.
    std::vector<ChunkPos> get_offsets(std::span<const LodDescriptor> lods, LodLevel child, LodLevel parent) {
        tassert(parent < lods.size());
        tassert(child < parent);

        const int ratio = lod_ratio(lods[parent], lods[child]);
        std::vector<ChunkPos> offsets;
        offsets.reserve(static_cast<size_t>(ratio) * ratio * ratio);

        for (int x = 0; x < ratio; x++)
            for (int y = 0; y < ratio; y++)
                for (int z = 0; z < ratio; z++) offsets.push_back({x, y, z});

        return offsets;
    }
}  // namespace

/// @brief Checks that a LOD list is coherent (power-of-two sizes, never shrinks).
bool is_valid(std::span<const LodDescriptor> lods) {
    if (lods.empty()) return false;
    if (lods[0].scale != 1) return false;

    size_t previous = 0;
    for (const auto& lod : lods) {
        if (!is_power_of_two(lod.width) || !is_power_of_two(lod.scale)) return false;
        if (lod.size() < previous) return false;
        previous = lod.size();
    }
    return true;
}

// is_valid must return true
ChunkPos Chunk::convert_chunk_pos(std::span<const LodDescriptor> lods, ChunkIndex current, LodLevel target) {
    tassert(target < lods.size());
    tassert(current.lod < lods.size());

    const size_t current_size = lods[current.lod].size();
    const size_t target_size = lods[target].size();
    if (current_size == target_size) return current.pos;

    if (target_size > current_size) {
        // Go to a coarser LOD: find the chunk that contains this one. `operator/` rounds
        // toward negative infinity, which is required for west/south coordinates.
        const int ratio = static_cast<int>(target_size / current_size);
        return current.pos / ratio;
    }

    // Go to a finer LOD: this chunk covers a block of smaller chunks; return the
    // smallest-coordinate one.
    const int ratio = static_cast<int>(current_size / target_size);
    return current.pos * ratio;
}

// is_valid must return true
std::vector<ChunkIndex> Chunk::get_child_chunks(std::span<const LodDescriptor> lods, ChunkIndex current) {
    tassert(current.lod < lods.size());

    std::vector<ChunkIndex> children;
    for (LodLevel child = 0; child < current.lod; ++child) {
        const auto direct = get_child_chunks_lod_specific(lods, current, child);
        children.insert(children.end(), direct.begin(), direct.end());
    }
    return children;
}

// is_valid must return true
std::vector<ChunkIndex> Chunk::get_child_chunks_lod_specific(
    std::span<const LodDescriptor> lods, ChunkIndex current, LodLevel child) {
    tassert(current.lod < lods.size());
    tassert(child < current.lod);

    const auto offsets = get_offsets(lods, child, current.lod);
    const ChunkPos base = convert_chunk_pos(lods, current, child);

    std::vector<ChunkIndex> children;
    children.reserve(offsets.size());
    for (const auto& offset : offsets) children.emplace_back(base + offset, child);
    return children;
}

// is_valid must return true
std::optional<ChunkIndex> Chunk::get_parent_chunk(std::span<const LodDescriptor> lods, ChunkIndex current) {
    tassert(current.lod < lods.size());
    if (current.lod + 1 >= lods.size()) return std::nullopt;
    return ChunkIndex{convert_chunk_pos(lods, current, current.lod + 1), current.lod + 1};
}
