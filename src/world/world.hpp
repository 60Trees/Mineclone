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
        static uint64_t mix(uint64_t x) noexcept;
        size_t operator()(const Vec3i& v) const noexcept;
    };
    // </AI>
};

/// Neighbour order used everywhere (matches ChunkMesher::mesh): down, up, north, south, west, east.
/// `i ^ 1` is always the opposite direction.
inline constexpr std::array<Vec3i, 6> CHUNK_NEIGHBOR_OFFSETS{{
    {0, -1, 0},
    {0, 1, 0},
    {0, 0, -1},
    {0, 0, 1},
    {-1, 0, 0},
    {1, 0, 0},
}};

struct Tile {
    RegistryUint id = RegistryNull;
    uint16_t data;

    bool operator==(const Tile& other) const;
    operator bool() const;
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
    /// Top-left corner of the tile in the atlas, in pixels (inclusive). Same for every vertex of a face.
    uint16_t u, v;
    /// Size of the tile in the atlas, in pixels. Same for every vertex of a face.
    uint16_t u_size, v_size;
    /**
     * @brief Tile-space texture coordinate of THIS vertex: how many times the tile has repeated
     * by the time you reach this corner. Typically 0 at the near edge of the face and N at the far edge.
     * Interpolated across the face, the shader wraps it with fract() inside the tile's atlas rectangle.
     *
     * Far LODs: N = 2^lod, so the texture keeps its native world size.
     * Greedy meshing (future): N = merged_width_in_blocks * 2^lod.
     * @note Max 255. Widen to uint16_t if greedy meshing at higher LODs needs more.
     */
    uint8_t repeat_u, repeat_v;
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

        void set_blocks(const std::array<Tile, CHUNK_BLOCK_COUNT>& tiles) noexcept;
        void set_blocks(std::array<Tile, CHUNK_BLOCK_COUNT>&& tiles) noexcept;
        static size_t index(size_t x, size_t y, size_t z) noexcept;
        Tile get(size_t x, size_t y, size_t z) const noexcept;
        void set(size_t x, size_t y, size_t z, const Tile& tile) noexcept;
    };

    std::unique_ptr<Data> array_chunk = nullptr;

    /// @brief If `raw_chunk` is null, then this tile is used for all blocks in the chunk.
    /// Useful to save memory for chunks that are all the same block (e.g. air).
    Tile single_tile = TileNull;

    RawChunk() = default;
    RawChunk(std::unique_ptr<Data> raw_chunk, Tile single_tile);

    // TODO: Implement these constructors in world.cpp
    // When copying the Data, just construct a new Data and copy the array_chunk->data array
    RawChunk(const RawChunk& oth);
    RawChunk(RawChunk&&);
    RawChunk& operator=(RawChunk&& oth);
    RawChunk& operator=(const RawChunk& oth);

    void set_blocks(const std::array<Tile, CHUNK_BLOCK_COUNT>& tiles) noexcept;
    void set_blocks(std::array<Tile, CHUNK_BLOCK_COUNT>&& tiles) noexcept;
    void fill_with(Tile tile) noexcept;
    static size_t index(size_t x, size_t y, size_t z) noexcept;
    Tile get(size_t x, size_t y, size_t z) const noexcept;
    void set(size_t x, size_t y, size_t z, const Tile& tile) noexcept;
    void try_compress();
    bool is_single_block() noexcept;
};

struct Chunk : RawChunk {
    /// @note Shared lock = read, unique lock = write
    std::shared_mutex mutex;

    Chunk& operator=(RawChunk&& other) noexcept;

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
    Level(Level&& other);
    Level& operator=(Level&& other);
    Chunk& operator[](const Vec3i& pos, bool lock = false);
    const Chunk& operator[](const Vec3i& pos) const;
    bool contains(const Vec3i& pos) const;
    bool contains(const Vec3i& pos, bool lock = false);
    void erase(const Vec3i& pos, bool lock = false);
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
    World();
    /**
     * @brief The chunks in the world, indexed by PosLodLevel.
     *
     * @detail The way these chunks are physically arranged is that
     * their physical size is `chunk_size(lod)` times the size of a normal chunk
     * AND that the south west bottom corner of the chunk coord `0,0,0` (for all lods)
     * is the world origin (`+x` is east, `+y` is up, `+z` is north)
     *
     * @note
     * The first level (0) is highest detail, with the coords being the plain chunk coordinates.
     * The second level (1) is half detail by default, with the coords being the chunk coordinates divided by 2, rounded down to 0.
     *
     * When adjacent LOD sizes have an integral ratio, a parent covers that many child chunks per axis. For example,
     * with the default ratio of 2, Lvl1 chunk coord `1,2,3` covers Lvl0 chunk coords `2,4,6` to `3,5,7` (inclusive).
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
        void ensure_mesh(Vec3i pos, PosLodLevel lod);

        /// Forget a chunk completely: pending request AND published mesh.
        void drop_mesh(Vec3i pos, PosLodLevel lod);

        void publish_mesh(Vec3i pos, PosLodLevel lod, Mesh mesh);

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
    mutable std::shared_mutex camera_mutex;

    /// Returns how many world blocks are represented by one block in a chunk at this LOD.
    /// Values are clamped to a practical range so coordinate calculations remain safe.
    std::function<size_t(PosLodLevel)> chunk_size_function;
    mutable std::shared_mutex chunk_size_mutex;

    void set_chunk_size_function(std::function<size_t(PosLodLevel)> function);
    size_t chunk_size(PosLodLevel lod) const;
    double chunk_extent(PosLodLevel lod) const;
    Vec3i camchunk(PosLodLevel lod) const;

    virtual bool chunk_in_fov(Vec3i pos, PosLodLevel lod, double aspect_ratio = 2.0) const = 0;

    struct Module : triolib::Module {
        tClass;

        World* world = nullptr;
        void init() override;
    };

    /**
     * @brief A module for `World` that is called
     * every time the center gets snapped backwards.
     */
    struct SnapHandler : World::Module {
        void init() override;
        void quit() override;
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

    void init() override;
    void loop() override;
    void quit() override;
};
