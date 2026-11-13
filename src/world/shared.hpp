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

#include <registry/registry.hpp>
#include <triolib.hpp>

/**
 * @brief Represents a level of detail (LOD) for chunks in the world.
 *
 * This type is used to specify the level of detail for chunks, where a lower value indicates higher detail.
 */
using LodLevel = unsigned int;

/*
*/

struct LodDescriptor {
    /// @brief The width of a chunk **IN BLOCKS** at a specific level of detail (LOD).
    /// @note Must either be one or a power of two.
    size_t width;
    /// @brief The size *multiplier* of a chunk at a specific level of detail (LOD).
    /// @note Must either be one or a power of two.
    /// Does not change the amount of blocks, just the size.
    /// If the associated \ref LodLevel is `0` then it **MUST** return `1`.
    /// @detail For example, `1` = normal size, `2` = double size, `4` = quadruple size, etc.
    size_t size;
};

/// @brief The default chunk size (width) in blocks for the highest level of detail (LOD 0).
extern const size_t* default_chunk_size;

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
};

struct ChunkIndex {
    ChunkPos pos;
    LodLevel lod;

    inline auto operator<=>(const ChunkIndex&) const = default;
};

struct Tile {
    RegistryUint id = RegistryNull;
    uint16_t data = 0;
    bool operator==(const Tile&) const;
    operator bool() const;
};

constexpr static Tile TileNull{RegistryNull, 0};

struct TileArr {
    inline TileArr(size_t tile_count) : tile_count(tile_count), arr(std::make_unique<Tile[]>(tile_count)) {}
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

    Tile operator[](size_t index) const noexcept {
        if (index >= tile_count) return TileNull;
        return arr[index];
    }

    inline Tile& at(size_t index) noexcept {
        static Tile TileNullRef = TileNull;
        if (index >= tile_count) {
            TileNullRef = TileNull;
            return TileNullRef;
        }
        return arr[index];
    }
    inline Tile at(size_t index) const noexcept {
        if (index >= tile_count) return TileNull;
        return arr[index];
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

    inline Tile& at(int x, int y, int z) noexcept { return tiles[index(x, y, z)]; }
    inline Tile& at(glm::ivec3 pos) noexcept { return tiles[index(pos)]; }
    inline Tile& at(size_t index) noexcept { return tiles[index]; }
    inline Tile at(int x, int y, int z) const noexcept { return tiles[index(x, y, z)]; }
    inline Tile at(glm::ivec3 pos) const noexcept { return tiles[index(pos)]; }
    inline Tile at(size_t index) const noexcept { return tiles[index]; }
    /// @brief Get the width of the chunk in blocks.
    inline size_t size() const noexcept { return chunk_width; }
    inline Tile& operator[](int x, int y, int z) noexcept { return at(x, y, z); }
    inline Tile& operator[](glm::ivec3 pos) noexcept { return at(pos); }
    inline Tile& operator[](size_t index) noexcept { return at(index); }
    inline Tile operator[](int x, int y, int z) const noexcept { return at(x, y, z); }
    inline Tile operator[](glm::ivec3 pos) const noexcept { return at(pos); }
    inline Tile operator[](size_t index) const noexcept { return at(index); }

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

    inline Iterator begin() { return {this, 0}; }
    inline Iterator end() { return {this, tiles.size()}; }
    // </AI>

    std::shared_mutex mutex;

    bool should_render = true;

    private:
    size_t chunk_width;
};
