#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>
#include <registry/registry.hpp>
#include <triolib.hpp>
#include <include/position.hpp>

struct Tile {
    RegistryUint id = RegistryNull;
    uint16_t data;

    inline bool operator==(const Tile& other) const { return id == other.id && data == other.data; }
    inline operator bool() const { return id != RegistryNull; }
};

extern Tile TileNull;

#define inconsequential [[gnu::const]] constexpr inline

struct Chunk {
    static constexpr size_t CHUNK_SIZE = 16;
    static constexpr size_t BLOCK_COUNT = CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE;

    static_assert(CHUNK_SIZE > 0 && (CHUNK_SIZE & (CHUNK_SIZE - 1)) == 0, "CHUNK_SIZE must be a power of 2");

    std::vector<Tile> palette;

    std::vector<uint8_t> data;

    uint8_t bucket = 0;

    inconsequential static uint8_t get_bucket_size(size_t palette_size) noexcept {
        if (palette_size <= 1) return 0;
        uint8_t bits = 0;
        while ((size_t(1) << bits) < palette_size) ++bits;
        return bits;
    }

    private:
    inconsequential static size_t bytes_for(size_t bit_count) noexcept { return (bit_count + 7) / 8; }
    inconsequential static size_t index(size_t x, size_t y, size_t z) { return x + y * CHUNK_SIZE + z * CHUNK_SIZE * CHUNK_SIZE; }

    uint get_index(size_t block_index) const noexcept;
    void set_index(size_t block_index, uint value) noexcept;

    public:
    void resize_palette() noexcept;

    void set_blocks(const std::array<Tile, BLOCK_COUNT>& tiles);
    Tile get(size_t x, size_t y, size_t z) const noexcept;
    void set(size_t x, size_t y, size_t z, const Tile& tile) noexcept;
};

/**
 * Contains a collection of chunks
 */
struct World : triolib::AppModule {
    std::map<std::pair<BigInt, BigInt>, Chunk> chunks;
    std::map<RawRegistryEntry, RegistryUint> needed_registry_ids{};
    inline Chunk& get_chunk(BigInt chunkx, BigInt chunky, BigInt chunkz) { return chunks[{chunkx, chunky}]; }
};
