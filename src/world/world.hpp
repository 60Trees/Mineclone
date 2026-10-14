#pragma once

#include <shared_mutex>
#include <cstddef>
#include <cstdint>
#include <glm/detail/qualifier.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/vec4.hpp>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <registry/registry.hpp>
#include <triolib.hpp>
#include <include/position.hpp>
#include <world/entities/entity.hpp>

struct Tile {
    RegistryUint id = RegistryNull;
    uint16_t data;

    inline bool operator==(const Tile& other) const { return id == other.id && data == other.data; }
    inline operator bool() const { return id != RegistryNull; }
};

extern Tile TileNull;

static constexpr size_t CHUNK_SIZE = 16;
static constexpr size_t CHUNK_BLOCK_COUNT = CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE;
static_assert(CHUNK_SIZE > 0 && (CHUNK_SIZE & (CHUNK_SIZE - 1)) == 0, "CHUNK_SIZE must be a power of 2");

struct RawChunk {
    std::array<Tile, CHUNK_BLOCK_COUNT> data;

    inline void set_blocks(const std::array<Tile, CHUNK_BLOCK_COUNT>& tiles) noexcept {
        data = std::array<Tile, CHUNK_BLOCK_COUNT>(tiles);
    }
    inline void set_blocks(std::array<Tile, CHUNK_BLOCK_COUNT>&& tiles) noexcept { data = std::move(tiles); }

    static inline size_t index(size_t x, size_t y, size_t z) noexcept {
        return x + y * CHUNK_SIZE + z * CHUNK_SIZE * CHUNK_SIZE;
    }

    inline Tile get(size_t x, size_t y, size_t z) const noexcept {
        if (x >= CHUNK_SIZE || y >= CHUNK_SIZE || z >= CHUNK_SIZE) return TileNull;

        return data[index(x, y, z)];
    }

    inline void set(size_t x, size_t y, size_t z, const Tile& tile) noexcept {
        if (x >= CHUNK_SIZE || y >= CHUNK_SIZE || z >= CHUNK_SIZE) return;
        data[index(x, y, z)] = tile;
    }
};

struct BlockVertex {
    uint8_t x, y, z;
    uint16_t lighting;
    uint16_t u0, v1;
    uint8_t normal_yaw, normal_pitch, normal_roll;
};

struct UnlockedChunk {
    std::unique_ptr<RawChunk> raw_chunk = nullptr;

    /// @brief If `raw_chunk` is null, then this tile is used for all blocks in the chunk.
    /// Useful to save memory for chunks that are all the same block (e.g. air).
    Tile single_tile = TileNull;

    UnlockedChunk() = default;
    UnlockedChunk(std::unique_ptr<RawChunk> raw_chunk, Tile single_tile)
        : raw_chunk(std::move(raw_chunk)), single_tile(single_tile) {}

    inline void set_blocks(const std::array<Tile, CHUNK_BLOCK_COUNT>& tiles) noexcept {
        raw_chunk = std::make_unique<RawChunk>();
        raw_chunk->data = std::array<Tile, CHUNK_BLOCK_COUNT>(tiles);
        try_compress();
        needs_remesh = true;
    }
    inline void set_blocks(std::array<Tile, CHUNK_BLOCK_COUNT>&& tiles) noexcept {
        raw_chunk = std::make_unique<RawChunk>();
        raw_chunk->data = std::move(tiles);
        try_compress();
        needs_remesh = true;
    }
    inline void fill_with(Tile tile) noexcept {
        single_tile = tile;
        raw_chunk = nullptr;
        needs_remesh = true;
    }
    static inline size_t index(size_t x, size_t y, size_t z) noexcept {
        return x + y * CHUNK_SIZE + z * CHUNK_SIZE * CHUNK_SIZE;
    }
    inline Tile get(size_t x, size_t y, size_t z) const noexcept {
        if (x >= CHUNK_SIZE || y >= CHUNK_SIZE || z >= CHUNK_SIZE) return TileNull;
        if (!raw_chunk) return single_tile;

        return raw_chunk->data[index(x, y, z)];
    }
    inline void set(size_t x, size_t y, size_t z, const Tile& tile) noexcept {
        if (x >= CHUNK_SIZE || y >= CHUNK_SIZE || z >= CHUNK_SIZE) return;
        if (!raw_chunk && tile == single_tile) return;
        if (!raw_chunk) {
            raw_chunk = std::make_unique<RawChunk>();
            raw_chunk->data.fill(single_tile);
        }

        raw_chunk->data[index(x, y, z)] = tile;
        needs_remesh = true;
    }
    void try_compress() {
        if (!raw_chunk) return;

        const auto& data = raw_chunk->data;
        const Tile first = data[0];

        for (uint i = 1; i < CHUNK_BLOCK_COUNT; ++i) {
            if (data[i] != first) return;
        }

        single_tile = first;
        raw_chunk = nullptr;
        needs_remesh = true;
    }
    inline bool is_single_block() noexcept {
        try_compress();
        return !raw_chunk;
    }

    bool needs_remesh = true;
    std::vector<BlockVertex> mesh;
};

struct Chunk : UnlockedChunk {
    std::shared_mutex mutex;
};

struct Vec3i {
    int x, y, z;

    // <AI>
    bool operator==(const Vec3i&) const = default;

    struct Vec3iHash {
        static uint64_t mix(uint64_t x) noexcept {
            x += 0x9e3779b97f4a7c15ULL;
            x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
            x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
            return x ^ (x >> 31);
        }

        size_t operator()(const Vec3i& v) const noexcept {
            uint64_t h = mix(static_cast<uint32_t>(v.x));
            h = mix(h ^ static_cast<uint32_t>(v.y));
            h = mix(h ^ static_cast<uint32_t>(v.z));
            return static_cast<size_t>(h);
        }
    };
    // </AI>
};

struct Camera {
    float pitch = 0.0f;
    float yaw = 0.0f;
    float roll = 0.0f;
    double x = 0, y = 0, z = 0;
};

struct BigPos {
    BigInt x = 0, y = 0, z = 0;
};

/**
 * @brief A bubble of the world, which contains chunks and a
 * BigInt offset (BigInts are effectively infinite and cannot wrap).
 *
 * @detail In singleplayer, this is the entire world.
 * In multiplayer, some tweaks would have to be made to make it work
 * (i.e multiple bubbles, some players can join bubbles, etc) and the
 * only thing being sent to the client is this bubble.
 */
struct World : triolib::SubApp {
    tClass;
    /// shared lock = iterate over chunks, unique lock = add/remove chunks
    std::shared_mutex chunks_mutex;
    std::unordered_map<Vec3i, Chunk, Vec3i::Vec3iHash> chunks;
    std::map<RawRegistryEntry, RegistryUint> needed_registry_ids{};
    inline UnlockedChunk& get_chunk(int x, int y, int z) { return chunks[{x, y, z}]; }

    BigPos offset;

    double snap_back_threshold = 10000000;
    /// If any value in the center ever goes above
    /// the `snap_back_threshold`, then everything gets
    /// snapped backwards that amount, the offset
    /// gets added by that amount and all World::SnapHandlers
    /// get the callback.
    ///
    /// For example: Center goes above y=1000, everything moves 1000 units
    /// down, offset.y += 1000
    Camera camera;

    struct Module : triolib::Module {
        tClass;

        World* world = nullptr;
        void init() override {
            tassertmsgerr(parent, "When initializing " + vname() + ", `parent` is null", logic_error);

            world = dynamic_cast<World*>(parent);

            tassertmsgerr(world,
                vname() + " must be a module belonging to WorldBubble, not " + std::string(typeid(*parent).name()),
                logic_error);
        }
    };

    /**
     * @brief A module for `World` that is called
     * every time the center gets snapped backwards.
     */
    struct SnapHandler : World::Module {
        void init() override {
            World::Module::init();

            world->snap_back_callback.erase(typeid(*this).hash_code());
            world->snap_back_callback[typeid(*this).hash_code()] = [this](const glm::vec<3, double>& difference) {
                this->on_resize(difference);
            };
        }
        void quit() override { world->snap_back_callback.erase(typeid(*this).hash_code()); }
        virtual void on_resize(const glm::vec<3, double>& difference) = 0;
    };

    protected:
    std::map<size_t, std::function<void(const glm::vec<3, double>& difference)>> snap_back_callback{};
    friend struct SnapHandler;
};

struct ChunkMesher : World::Module {
    tClass;

    virtual void mesh(Vec3i pos, UnlockedChunk& chunk) = 0;

    void init() override {}
    void loop() override {}
    void quit() override {}
};
