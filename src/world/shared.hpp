/**
 * @file world/shared.hpp
 * @brief The shared world interface
 * @author 60Trees_ (github.com/60Trees)
 * @detail
 * This file has a bunch of shared types and functions
 * used by both the client and server side (e.g Chunk,
 * Tile, LodLevel, etc)
 */

#pragma once

#include <glm/ext/vector_int3.hpp>
#include <shared_mutex>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

#include <registry/registry.hpp>
#include <triolib.hpp>

/// @brief The width (in blocks) of the highest-detail (LOD 0) chunk.
extern const size_t* default_chunk_size;

/**
 * @brief Represents a level of detail (LOD) for chunks in the world.
 *
 * This type is used to specify the level of detail for chunks, where a lower value indicates higher detail.
 */
using LodLevel = unsigned int;

/// @brief Integer division rounded toward negative infinity. Chunk math must not use
/// truncating division, otherwise negative coordinates (west/south) belong to the wrong parent.
constexpr inline int floor_div(int value, int divisor) {
    const int quotient = value / divisor;
    const int remainder = value % divisor;
    return quotient - ((remainder != 0) && ((remainder < 0) != (divisor < 0)) ? 1 : 0);
}

struct LodDescriptor {
    /// @brief The width of a chunk **IN BLOCKS** at a specific level of detail (LOD).
    /// @note Must either be one or a power of two.
    size_t width;
    /// @brief The size *multiplier* of a chunk at a specific level of detail (LOD).
    /// @note Must either be one or a power of two.
    /// Does not change the amount of blocks, just the size.
    /// If the associated \ref LodLevel is `0` then it **MUST** return `1`.
    /// @detail For example, `1` = normal size, `2` = double size, `4` = quadruple size, etc.
    size_t scale;
    /// @brief Get how many units long a chunk is in the world at this level of detail (LOD).
    /// @detail This is the product of `width` and `scale`.
    inline size_t size() const noexcept { return width * scale; }
};

/// @brief Validates a LOD list: non-empty, LOD 0 unscaled, power-of-two sizes, never shrinking.
bool is_valid(std::span<const LodDescriptor> lods);

struct Camera {
    float pitch = 0.0f;
    float yaw = 0.0f;
    float roll = 0.0f;
    double x = 0, y = 0, z = 0;
};

struct ChunkPos {
    int x, y, z;
    inline auto operator<=>(const ChunkPos&) const = default;
    inline ChunkPos operator+(const ChunkPos& o) const { return {x + o.x, y + o.y, z + o.z}; }
    inline ChunkPos operator-(const ChunkPos& o) const { return {x - o.x, y - o.y, z - o.z}; }
    inline ChunkPos operator*(int scalar) const { return {x * scalar, y * scalar, z * scalar}; }
    /// @note Floor division, so negative chunk coordinates map to the chunk that contains them.
    inline ChunkPos operator/(int scalar) const {
        return {floor_div(x, scalar), floor_div(y, scalar), floor_div(z, scalar)};
    }
};

/// Contains pos and lod level
struct ChunkIndex {
    ChunkPos pos;
    LodLevel lod;

    inline auto operator<=>(const ChunkIndex&) const = default;
};

namespace std {
    // Chunk keys are used in unordered containers in the hot loading / meshing paths.
    template <>
    struct hash<ChunkPos> {
        size_t operator()(const ChunkPos& pos) const noexcept {
            size_t hash = static_cast<size_t>(static_cast<uint32_t>(pos.x));
            hash ^=
                static_cast<size_t>(static_cast<uint32_t>(pos.y)) + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
            hash ^=
                static_cast<size_t>(static_cast<uint32_t>(pos.z)) + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
            return hash;
        }
    };

    template <>
    struct hash<ChunkIndex> {
        size_t operator()(const ChunkIndex& index) const noexcept {
            size_t hash = std::hash<ChunkPos>{}(index.pos);
            hash ^= static_cast<size_t>(index.lod) + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
            return hash;
        }
    };
}  // namespace std

struct Tile {
    RegistryUint id = RegistryNull;
    uint16_t data = 0;
    bool operator==(const Tile&) const;
    operator bool() const;
};

inline bool Tile::operator==(const Tile& other) const { return id == other.id && data == other.data; }

inline Tile::operator bool() const { return id != RegistryNull && id != 0; }

constexpr static Tile TileNull{RegistryNull, 0};

struct TileArr {
    inline TileArr(size_t tile_count) : tile_count(tile_count), arr(nullptr) {}
    inline TileArr(const TileArr& other)
        : tile_count(other.tile_count), arr(std::make_unique<Tile[]>(other.tile_count)) {
        std::copy_n(other.arr.get(), tile_count, arr.get());
    }
    inline TileArr(TileArr&& other) noexcept
        : tile_count(std::exchange(other.tile_count, 0)), arr(std::move(other.arr)) {}
    inline TileArr duplicate() { return TileArr{*this}; }
    inline TileArr& operator=(const TileArr& other) {
        if (this == &other) return *this;

        auto new_arr = std::make_unique<Tile[]>(other.tile_count);
        std::copy_n(other.arr.get(), other.tile_count, new_arr.get());

        arr = std::move(new_arr);
        tile_count = other.tile_count;

        return *this;
    }
    inline TileArr& operator=(TileArr&& other) noexcept {
        if (this == &other) return *this;

        arr = std::move(other.arr);
        tile_count = std::exchange(other.tile_count, 0);

        return *this;
    }
    ~TileArr() = default;

    /// @brief Get the number of tiles in the array.
    inline size_t size() const noexcept { return tile_count; }

    inline Tile& operator[](size_t index) noexcept {
        static Tile TileNullRef = TileNull;
        if (index >= tile_count) {
            TileNullRef = TileNull;
            return TileNullRef;
        }
        return arr[index];
    }

    // Tile operator[](size_t index) const noexcept {
    //     if (index >= tile_count) return TileNull;
    //     return arr[index];
    // }
    // inline Tile& at(size_t index) noexcept {
    //     static Tile TileNullRef = TileNull;
    //     if (index >= tile_count) {
    //         TileNullRef = TileNull;
    //         return TileNullRef;
    //     }
    //     return arr[index];
    // }

    inline Tile at(size_t index) const noexcept {
        if (index >= tile_count) return TileNull;
        return arr[index];
    }

    inline void write(size_t index, Tile tile) noexcept {
        if (index >= tile_count) return;
        if (!arr) {
            if (tile == single_tile) return;
            try_decompress();
        }
        arr[index] = tile;
    }

    /// If \ref arr is null, then the entire chunk will be treated as if
    /// if was filled with this tile.
    Tile single_tile = TileNull;

    bool can_compress() const {
        if (!arr) return false;
        Tile first_tile = arr[0];
        for (size_t i = 1; i < tile_count; ++i) {
            if (arr[i] != first_tile) {
                return false;
            }
        }
        return true;
    }

    bool try_compress() {
        if (!arr) return false;
        Tile first_tile = arr[0];
        for (size_t i = 1; i < tile_count; ++i) {
            if (arr[i] != first_tile) {
                return false;
            }
        }
        arr = nullptr;
        single_tile = first_tile;
        return true;
    }

    void try_decompress() {
        if (arr) return;
        arr = std::make_unique<Tile[]>(tile_count);
        for (size_t i = 0; i < tile_count; ++i) {
            arr[i] = single_tile;
        }
    }

    private:
    std::unique_ptr<Tile[]> arr;
    size_t tile_count;
};
;

/**
 * @brief A chunk of the world, containing a 3D array of tiles.
 * @note Contains an `std::shared_mutex`
 */
struct Chunk {
    inline Chunk(size_t chunk_width, bool should_render = true)
        : chunk_width(chunk_width), tiles(chunk_width * chunk_width * chunk_width), should_render(should_render) {}
    inline Chunk(const Chunk& other)
        : chunk_width(other.chunk_width), tiles(other.tiles), should_render(other.should_render) {}
    inline Chunk(Chunk&& other) noexcept
        : chunk_width(other.chunk_width), tiles(std::move(other.tiles)), should_render(other.should_render) {}
    TileArr tiles;

    inline Chunk duplicate() { return Chunk{*this}; }

    inline Tile at(int x, int y, int z) const noexcept { return tiles.at(index(x, y, z)); }
    inline Tile at(glm::ivec3 pos) const noexcept { return tiles.at(index(pos)); }
    inline Tile at(size_t index) const noexcept { return tiles.at(index); }
    /// @brief Get the width of the chunk in blocks.
    inline size_t size() const noexcept { return chunk_width; }
    inline Tile& operator[](int x, int y, int z) noexcept { return tiles[index(x, y, z)]; }
    inline Tile& operator[](glm::ivec3 pos) noexcept { return tiles[index(pos)]; }
    inline Tile& operator[](size_t index) noexcept { return tiles[index]; }
    inline void write(int x, int y, int z, Tile tile) noexcept { tiles.write(index(x, y, z), tile); }
    inline void write(glm::ivec3 pos, Tile tile) noexcept { tiles.write(index(pos), tile); }
    inline void write(size_t index, Tile tile) noexcept { tiles.write(index, tile); }

    inline const size_t index(int x, int y, int z) const noexcept {
        return x + y * chunk_width + z * chunk_width * chunk_width;
    }
    inline const size_t index(glm::ivec3 pos) const noexcept { return index(pos.x, pos.y, pos.z); }
    inline const glm::ivec3 pos(size_t index) const noexcept {
        int x = index % chunk_width;
        int y = (index / chunk_width) % chunk_width;
        int z = index / (chunk_width * chunk_width);
        return {x, y, z};
    }

    // <AI>
    struct Entry {
        glm::ivec3 pos;
        Tile& tile;
    };

    struct Iterator {
        Chunk* chunk;
        size_t i;

        using iterator_category = std::forward_iterator_tag;
        using value_type = Entry;
        using difference_type = std::ptrdiff_t;
        using reference = Entry;

        inline Entry operator*() const { return {chunk->pos(i), chunk->tiles[i]}; }

        inline Iterator& operator++() {
            i++;
            return *this;
        }

        inline bool operator==(const Iterator& other) const { return chunk == other.chunk && i == other.i; }
        inline bool operator!=(const Iterator& other) const { return !(*this == other); }
    };

    inline Iterator begin() {
        tiles.try_decompress();
        return {this, 0};
    }
    inline Iterator end() {
        tiles.try_decompress();
        return {this, tiles.size()};
    }
    // </AI>

    std::shared_mutex mutex;

    bool should_render = true;

    /**
     * @brief Converts a chunk position from one level of detail (LOD) to another.
     * @param current The current chunk position and LOD level.
     * @param target_lod The target LOD level to convert to.
     * If the target LOD is higher (less detailed, bigger), it must either be the same size or
     * bigger, so it will simply return whatever chunk contains the \ref current chunk.
     * If it is lower (more detailed, smaller), it must either be the same size or smaller.
     * This means that this chunk is the "parent" of multiple chunks, so it will return the
     * chunk with the smallest coordinates.
     */
    static ChunkPos convert_chunk_pos(std::span<const LodDescriptor> lods, ChunkIndex current, LodLevel target);

    /**
     * @brief Gets all descendants of a given chunk, down to LOD 0.
     */
    static std::vector<ChunkIndex> get_child_chunks(std::span<const LodDescriptor> lods, ChunkIndex current);

    /**
     * @brief Gets every direct (or deeper) child of \p current at exactly \p child.
     * @note For adjacent LODs this is the 2x2x2 block of chunks that the parent covers.
     */
    static std::vector<ChunkIndex> get_child_chunks_lod_specific(
        std::span<const LodDescriptor> lods, ChunkIndex current, LodLevel child);

    /**
     * @brief Gets the chunk directly above \p current (one LOD coarser), or nullopt if
     * \p current is already the biggest LOD.
     */
    static std::optional<ChunkIndex> get_parent_chunk(std::span<const LodDescriptor> lods, ChunkIndex current);

    private:
    size_t chunk_width;
};
