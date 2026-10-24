#pragma once

#include <array>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <vector>
#include <shared_mutex>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <numbers>
#include <glm/detail/qualifier.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/vec4.hpp>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <registry/registry.hpp>
#include <triolib.hpp>
#include <include/position.hpp>
#include <world/entities/entity.hpp>

struct Vec3i {
    int x, y, z;

    // <AI>
    bool operator==(const Vec3i&) const = default;
    auto operator<=>(const Vec3i&) const = default;

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

/// Neighbour order used everywhere (matches ChunkMesher::mesh): down, up, north, south, west, east.
/// `i ^ 1` is always the opposite direction.
inline constexpr std::array<Vec3i, 6> CHUNK_NEIGHBOR_OFFSETS{{
    {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0},
}};

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

struct BlockVertex {
    /**
     * @brief *RAW* Chunk-local coordinates.
     * @note To get a *normalized* chunk-local coordinate (float from 0 to 1), cast to float and divide by `256.0f`.
     * The `uint8_t` is just enough precision to have one sixteenth of a block precision in a chunk of 16^3 blocks.
     */
    uint16_t x, y, z;
    /// Ignored for now.
    uint16_t lighting;
    /// @brief The pixel coordinates of the top-left corner of the texture (`u0`, inclusive)
    /// and the bottom-right corner of the texture (`v1`, exclusive) in the atlas.
    uint16_t u, v;
    /**
     * @brief The normal vector of the face.
     * @note To normalize, cast to float and divide by `127.0f`.
     * However, this means that -128 is outside the range. It will be considered an error value.
     */
    int8_t normal_yaw, normal_pitch, normal_roll;
};

struct RawChunk {
    struct Data {
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

    std::unique_ptr<Data> array_chunk = nullptr;

    /// @brief If `raw_chunk` is null, then this tile is used for all blocks in the chunk.
    /// Useful to save memory for chunks that are all the same block (e.g. air).
    Tile single_tile = TileNull;

    RawChunk() = default;
    RawChunk(std::unique_ptr<Data> raw_chunk, Tile single_tile)
        : array_chunk(std::move(raw_chunk)), single_tile(single_tile) {}

    // TODO: Implement these constructors in world.cpp
    // When copying the Data, just construct a new Data and copy the array_chunk->data array
    RawChunk(const RawChunk& oth);
    RawChunk(RawChunk&&);
    RawChunk& operator=(RawChunk&& oth);
    RawChunk& operator=(const RawChunk& oth);

    inline void set_blocks(const std::array<Tile, CHUNK_BLOCK_COUNT>& tiles) noexcept {
        array_chunk = std::make_unique<Data>();
        array_chunk->data = std::array<Tile, CHUNK_BLOCK_COUNT>(tiles);
        try_compress();
    }
    inline void set_blocks(std::array<Tile, CHUNK_BLOCK_COUNT>&& tiles) noexcept {
        array_chunk = std::make_unique<Data>();
        array_chunk->data = std::move(tiles);
        try_compress();
    }
    inline void fill_with(Tile tile) noexcept {
        single_tile = tile;
        array_chunk = nullptr;
    }
    static inline size_t index(size_t x, size_t y, size_t z) noexcept {
        return x + y * CHUNK_SIZE + z * CHUNK_SIZE * CHUNK_SIZE;
    }
    inline Tile get(size_t x, size_t y, size_t z) const noexcept {
        if (x >= CHUNK_SIZE || y >= CHUNK_SIZE || z >= CHUNK_SIZE) return TileNull;
        if (!array_chunk) return single_tile;

        return array_chunk->data[index(x, y, z)];
    }
    inline void set(size_t x, size_t y, size_t z, const Tile& tile) noexcept {
        if (x >= CHUNK_SIZE || y >= CHUNK_SIZE || z >= CHUNK_SIZE) return;
        if (!array_chunk && tile == single_tile) return;
        if (!array_chunk) {
            array_chunk = std::make_unique<Data>();
            array_chunk->data.fill(single_tile);
        }

        array_chunk->data[index(x, y, z)] = tile;
    }
    void try_compress() {
        if (!array_chunk) return;

        const auto& data = array_chunk->data;
        const Tile first = data[0];

        for (uint i = 1; i < CHUNK_BLOCK_COUNT; ++i) {
            if (data[i] != first) return;
        }

        single_tile = first;
        array_chunk = nullptr;
    }
    inline bool is_single_block() noexcept {
        try_compress();
        return !array_chunk;
    }
};

struct Chunk : RawChunk {
    /// @note Shared lock = read, unique lock = write
    std::shared_mutex mutex;

    Chunk& operator=(RawChunk&& other) noexcept {
        RawChunk::operator=(std::move(other));
        return *this;
    }

    /// Written by the loader under `Level::mutex` (unique), read under `Level::mutex` (shared).
    /// `true` means "a mesh for this chunk should be published".
    bool rendered = false;

    /// Bit `i` set => the neighbour at CHUNK_NEIGHBOR_OFFSETS[i] did not exist when this chunk was last meshed.
    /// Set by the mesher while it holds the level lock; read by the loader when a chunk is inserted, so that
    /// chunks that were meshed against missing neighbours get re-meshed exactly once when the neighbour shows up.
    std::atomic<uint8_t> mesh_missing_neighbors{0};
};

/// @brief A group of chunks at the same LOD level.
struct Level {
    /// @note Shared lock = iterate / index, unique lock = add/remove
    std::shared_mutex mutex;
    std::unordered_map<Vec3i, Chunk, Vec3i::Vec3iHash> chunks;

    Level() = default;
    Level(const Level&) = delete;
    Level& operator=(const Level&) = delete;
    Level(Level&& other) {
        std::unique_lock lock(other.mutex);
        chunks = std::move(other.chunks);
    }
    Level& operator=(Level&& other) {
        if (this == &other) return *this;
        std::scoped_lock lock(mutex, other.mutex);
        chunks = std::move(other.chunks);
        return *this;
    }

    inline Chunk& operator[](const Vec3i& pos, bool lock = false) {
        if (!lock) return chunks[pos];
        std::shared_lock lockguard(mutex);
        return chunks[pos];
    }
    inline const Chunk& operator[](const Vec3i& pos) const { return chunks.at(pos); }
    inline bool contains(const Vec3i& pos) const { return chunks.contains(pos); }
    inline bool contains(const Vec3i& pos, bool lock = false) {
        if (!lock) return chunks.contains(pos);
        std::shared_lock lockguard(mutex);
        return chunks.contains(pos);
    }
    inline void erase(const Vec3i& pos, bool lock = false) {
        if (!lock) {
            chunks.erase(pos);
            return;
        }
        std::unique_lock lockguard(mutex);
        chunks.erase(pos);
    }
};

/**
 * The chunk loading status of a chunk. It is like LodLevel but with negative values for unloaded chunks.
 * `<-1` = unloaded
 * `-1` = not rendered and doesn't need to be loaded or unloaded (lazy chunk)
 * `0` = not rendered but loaded
 * `1` = full detail
 * `2` = half detail
 * `3` = quarter detail
 * etc.
 */
using ChunkStatus = int;

/**
 * `0` = not rendered
 * `1` = full detail
 * `2` = half detail
 * `3` = quarter detail
 * etc.
 */
using LodLevel = unsigned int;

/**
 * `0` = full detail
 * `1` = half detail
 * `2` = quarter detail
 * etc.
 */
using PosLodLevel = unsigned int;

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
    /**
     * @brief The chunks in the world, indexed by PosLodLevel.
     *
     * @detail The way these chunks are physically arranged is that
     * their physical size is `2^index` times the size of a normal chunk
     * AND that the south west bottom corner of the chunk coord `0,0,0` (for all lods)
     * is the world origin (`+x` is east, `+y` is up, `+z` is north)
     *
     * @note
     * The first level (0) is highest detail, with the coords being the plain chunk coordinates.
     * The second level (1) is half detail, with the coords being the chunk coordinates divided by 2, rounded down to 0.
     *
     * Regarding the the first level (`levels[0]`) as Lvl0 and the second level (`levels[1]`) as Lvl1, the Lvl1 chunk
     * coord `x,y,z` will cover the Lvl0 chunk coords For example: Lvl1 chunk coord `1,2,3` will cover Lvl0 chunk
     * cooords `2,4,6` to `3,5,7` (inclusive) and the Lvl1 chunk coord `0,0,0` will cover Lvl0 chunk coords `0,0,0` to
     * `1,1,1` (inclusive) and the Lvl1 chunk coord `-1,-1,-1` will cover Lvl0 chunk coords `-2,-2,-2` to `-1,-1,-1`
     * (inclusive).
     *
     * etc.
     */
    std::vector<Level> levels;
    /// @note Shared lock = iterate / index, unique lock = add/remove
    std::shared_mutex levels_mutex;

    struct ChunkIndex {
        Vec3i pos;
        PosLodLevel lod;
    };

    std::map<RawRegistryEntry, RegistryUint> needed_registry_ids{};

    /**
     * @brief Pending mesh requests + published meshes.
     *
     * Lock order (acquire left to right only):
     *     levels_mutex -> Level::mutex -> chunks_to_mesh_mutex -> meshes_mutex
     * The renderer only ever takes meshes_mutex (shared) and never anything from the left.
     *
     * Invariant: a mesh is published for (lod,pos) only while that chunk exists AND `rendered` is true.
     * The loader upholds it by calling drop_mesh() under the level lock whenever it clears `rendered`
     * or erases the chunk; the mesher upholds it by re-checking under the level lock before publishing.
     */
    struct MeshData {
        /// Immutable once published, so the renderer can hold a reference without holding a lock.
        using Mesh = std::shared_ptr<const std::vector<BlockVertex>>;
        using MeshMap = std::unordered_map<Vec3i, Mesh, Vec3i::Vec3iHash>;

        /// Queue (or re-queue) a chunk for meshing.
        inline void ensure_mesh(Vec3i pos, PosLodLevel lod) {
            std::lock_guard lock(chunks_to_mesh_mutex);
            if (chunks_to_mesh.size() <= lod) chunks_to_mesh.resize(lod + 1);
            chunks_to_mesh[lod].insert(pos);
        }

        /// Forget a chunk completely: pending request AND published mesh.
        inline void drop_mesh(Vec3i pos, PosLodLevel lod) {
            {
                std::lock_guard lock(chunks_to_mesh_mutex);
                if (lod < chunks_to_mesh.size()) chunks_to_mesh[lod].erase(pos);
            }
            std::unique_lock lock(meshes_mutex);
            if (lod < meshes.size() && meshes[lod].erase(pos) > 0) ++version;
        }

        inline void publish_mesh(Vec3i pos, PosLodLevel lod, Mesh mesh) {
            std::unique_lock lock(meshes_mutex);
            if (meshes.size() <= lod) meshes.resize(lod + 1);
            meshes[lod][pos] = std::move(mesh);
            ++version;
        }

        std::vector<std::set<Vec3i>> chunks_to_mesh;
        std::mutex chunks_to_mesh_mutex;
        std::vector<MeshMap> meshes;
        std::shared_mutex meshes_mutex;
        /// Bumped (under meshes_mutex) whenever the set of published meshes changes.
        std::atomic<uint64_t> version{0};
    } mesh_data;

    BigPos offset;

    std::vector<ChunkIndex> chunks_to_mesh;
    std::shared_mutex chunks_to_mesh_mutex;

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
    Camera render_camera;
    std::shared_mutex camera_mutex;

    inline Vec3i camchunk(PosLodLevel lod) {
        const double scalar = 1.0 / static_cast<double>(CHUNK_SIZE) / std::pow(2, lod);
        std::shared_lock __(camera_mutex);
        return Vec3i{
            static_cast<int>(std::floor(camera.x * scalar)),
            static_cast<int>(std::floor(camera.y * scalar)),
            static_cast<int>(std::floor(camera.z * scalar)),
        };
    }

    inline bool chunk_in_fov(Vec3i pos, PosLodLevel lod, double aspect_ratio = 2.0) const {
        const double scale = std::ldexp(1.0, static_cast<int>(lod));
        const double chunk_size = static_cast<double>(CHUNK_SIZE) * scale;
        const double half_extent = chunk_size * 0.5;
        const double radius = std::sqrt(3.0) * half_extent;
        const double yaw = camera.yaw * std::numbers::pi / 180.0;
        const double pitch = camera.pitch * std::numbers::pi / 180.0;
        const double cos_pitch = std::cos(pitch);

        const std::array<double, 3> forward{
            cos_pitch * std::cos(yaw),
            std::sin(pitch),
            cos_pitch * std::sin(yaw),
        };
        const std::array<double, 3> right{std::sin(yaw), 0.0, -std::cos(yaw)};
        const std::array<double, 3> up{
            right[1] * forward[2] - right[2] * forward[1],
            right[2] * forward[0] - right[0] * forward[2],
            right[0] * forward[1] - right[1] * forward[0],
        };
        const std::array<double, 3> center{
            (static_cast<double>(pos.x) + 0.5) * chunk_size - camera.x,
            (static_cast<double>(pos.y) + 0.5) * chunk_size - camera.y,
            (static_cast<double>(pos.z) + 0.5) * chunk_size - camera.z,
        };
        const auto dot = [](const std::array<double, 3>& a, const std::array<double, 3>& b) {
            return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
        };
        const double depth = dot(center, forward);
        const double tan_vertical_fov = std::tan(35.0 * std::numbers::pi / 180.0);
        const double tan_horizontal_fov = tan_vertical_fov * aspect_ratio;

        return depth + radius >= 0.0 && std::abs(dot(center, right)) <= depth * tan_horizontal_fov + radius &&
               std::abs(dot(center, up)) <= depth * tan_vertical_fov + radius;
    }

    struct Module : triolib::Module {
        tClass;

        World* world = nullptr;
        void init() override {
            tassertmsgerr(parent, "When initializing " + vname() + ", `parent` is null", logic_error);

            world = dynamic_cast<World*>(parent);

            tassertmsgerr(world,
                vname() + " must be a module belonging to World, not " + std::string(typeid(*parent).name()),
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

    virtual std::vector<BlockVertex> mesh(World::ChunkIndex i, const RawChunk& chunk, const RawChunk* down,
        const RawChunk* up, const RawChunk* north, const RawChunk* south, const RawChunk* west,
        const RawChunk* east) = 0;

    void init() override {}
    void loop() override {}
    void quit() override {}
};
