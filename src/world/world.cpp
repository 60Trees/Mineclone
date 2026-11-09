#include "world.hpp"
#include <base/fps_handler.hpp>
#include <glm/common.hpp>
#include "triolib.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <mutex>
#include <numbers>
#include <optional>
#include <shared_mutex>
#include <thread>
#include <vector>
#include <threads/task.hpp>

Tile TileNull = {0, 0};

using namespace std;
using namespace triolib;

uint64_t Vec3i::Vec3iHash::mix(uint64_t x) noexcept {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
size_t Vec3i::Vec3iHash::operator()(const Vec3i& v) const noexcept {
    uint64_t h = mix(static_cast<uint32_t>(v.x));
    h = mix(h ^ static_cast<uint32_t>(v.y));
    h = mix(h ^ static_cast<uint32_t>(v.z));
    return static_cast<size_t>(h);
}
bool Tile::operator==(const Tile& other) const { return id == other.id && data == other.data; }
Tile::operator bool() const { return id != RegistryNull; }

World::World() {
    chunk_size_function = [](PosLodLevel lod) {
        return (size_t)(max((size_t)CHUNK_SIZE / (size_t)(lod + 1), (size_t)1));
    };
}

void RawChunk::Data::set_blocks(const array<Tile, CHUNK_BLOCK_COUNT>& tiles) noexcept { data = tiles; }
void RawChunk::Data::set_blocks(array<Tile, CHUNK_BLOCK_COUNT>&& tiles) noexcept { data = std::move(tiles); }
size_t RawChunk::Data::index(size_t x, size_t y, size_t z) noexcept {
    return x + y * CHUNK_SIZE + z * CHUNK_SIZE * CHUNK_SIZE;
}
Tile RawChunk::Data::get(size_t x, size_t y, size_t z) const noexcept {
    return x >= CHUNK_SIZE || y >= CHUNK_SIZE || z >= CHUNK_SIZE ? TileNull : data[index(x, y, z)];
}
void RawChunk::Data::set(size_t x, size_t y, size_t z, const Tile& tile) noexcept {
    if (x < CHUNK_SIZE && y < CHUNK_SIZE && z < CHUNK_SIZE) data[index(x, y, z)] = tile;
}

RawChunk::RawChunk(unique_ptr<Data> raw_chunk, Tile tile) : array_chunk(std::move(raw_chunk)), single_tile(tile) {}
void RawChunk::set_blocks(const array<Tile, CHUNK_BLOCK_COUNT>& tiles) noexcept {
    array_chunk = make_unique<Data>();
    array_chunk->data = tiles;
    try_compress();
}
void RawChunk::set_blocks(array<Tile, CHUNK_BLOCK_COUNT>&& tiles) noexcept {
    array_chunk = make_unique<Data>();
    array_chunk->data = std::move(tiles);
    try_compress();
}
void RawChunk::fill_with(Tile tile) noexcept { single_tile = tile, array_chunk = nullptr; }
size_t RawChunk::index(size_t x, size_t y, size_t z) noexcept { return Data::index(x, y, z); }
Tile RawChunk::get(size_t x, size_t y, size_t z) const noexcept {
    if (x >= CHUNK_SIZE || y >= CHUNK_SIZE || z >= CHUNK_SIZE) return TileNull;
    return array_chunk ? array_chunk->data[index(x, y, z)] : single_tile;
}
void RawChunk::set(size_t x, size_t y, size_t z, const Tile& tile) noexcept {
    if (x >= CHUNK_SIZE || y >= CHUNK_SIZE || z >= CHUNK_SIZE || (!array_chunk && tile == single_tile)) return;
    if (!array_chunk) {
        array_chunk = make_unique<Data>();
        array_chunk->data.fill(single_tile);
    }
    array_chunk->data[index(x, y, z)] = tile;
}
void RawChunk::try_compress() {
    if (!array_chunk) return;
    const Tile first = array_chunk->data[0];
    for (uint i = 1; i < CHUNK_BLOCK_COUNT; ++i)
        if (array_chunk->data[i] != first) return;
    single_tile = first;
    array_chunk = nullptr;
}
bool RawChunk::is_single_block() noexcept {
    try_compress();
    return !array_chunk;
}

Chunk& Chunk::operator=(RawChunk&& other) noexcept {
    RawChunk::operator=(std::move(other));
    return *this;
}

Level::Level(Level&& other) {
    unique_lock lock(other.mutex);
    chunks = std::move(other.chunks);
}
Level& Level::operator=(Level&& other) {
    if (this != &other) {
        scoped_lock lock(mutex, other.mutex);
        chunks = std::move(other.chunks);
    }
    return *this;
}
Chunk& Level::operator[](const Vec3i& pos, bool lock) {
    if (!lock) return chunks[pos];
    shared_lock lockguard(mutex);
    return chunks[pos];
}
const Chunk& Level::operator[](const Vec3i& pos) const { return chunks.at(pos); }
bool Level::contains(const Vec3i& pos) const { return chunks.contains(pos); }
bool Level::contains(const Vec3i& pos, bool lock) {
    if (!lock) return chunks.contains(pos);
    shared_lock lockguard(mutex);
    return chunks.contains(pos);
}
void Level::erase(const Vec3i& pos, bool lock) {
    if (!lock) {
        chunks.erase(pos);
        return;
    }
    unique_lock lockguard(mutex);
    chunks.erase(pos);
}

void World::MeshData::ensure_mesh(Vec3i pos, PosLodLevel lod) {
    lock_guard lock(chunks_to_mesh_mutex);
    if (chunks_to_mesh.size() <= lod) chunks_to_mesh.resize(lod + 1);
    chunks_to_mesh[lod].insert(pos);
}
void World::MeshData::drop_mesh(Vec3i pos, PosLodLevel lod) {
    {
        lock_guard lock(chunks_to_mesh_mutex);
        if (lod < chunks_to_mesh.size()) chunks_to_mesh[lod].erase(pos);
    }
    unique_lock lock(meshes_mutex);
    if (lod < meshes.size() && meshes[lod].erase(pos) > 0) ++version;
}
void World::MeshData::publish_mesh(Vec3i pos, PosLodLevel lod, Mesh mesh) {
    unique_lock lock(meshes_mutex);
    if (meshes.size() <= lod) meshes.resize(lod + 1);
    meshes[lod][pos] = std::move(mesh);
    ++version;
}

void World::set_chunk_size_function(function<size_t(PosLodLevel)> function) {
    if (!function) throw invalid_argument("World chunk size function cannot be empty");
    unique_lock lock(chunk_size_mutex);
    chunk_size_function = std::move(function);
}
size_t World::chunk_size(PosLodLevel lod) const {
    shared_lock lock(chunk_size_mutex);
    return clamp<size_t>(chunk_size_function(lod), 1, size_t{1} << 20);
}
double World::chunk_extent(PosLodLevel lod) const { return static_cast<double>(CHUNK_SIZE) * chunk_size(lod); }
Vec3i World::camchunk(PosLodLevel lod) const {
    const double scalar = 1.0 / chunk_extent(lod);
    shared_lock lock(camera_mutex);
    return {static_cast<int>(floor(camera.x * scalar)), static_cast<int>(floor(camera.y * scalar)),
        static_cast<int>(floor(camera.z * scalar))};
}

void World::Module::init() {
    tassertmsgerr(parent, "When initializing " + vname() + ", `parent` is null", logic_error);
    world = dynamic_cast<World*>(parent);
    tassertmsgerr(world, vname() + " must be a module belonging to World, not " + string(typeid(*parent).name()), logic_error);
}

void World::SnapHandler::init() {
    World::Module::init();
    world->snap_back_callback.erase(typeid(*this).hash_code());
    world->snap_back_callback[typeid(*this).hash_code()] = [this](const glm::vec<3, double>& difference) {
        on_resize(difference);
    };
}
void World::SnapHandler::quit() { world->snap_back_callback.erase(typeid(*this).hash_code()); }

void ChunkMesher::init() { World::Module::init(); }
void ChunkMesher::loop() {}
void ChunkMesher::quit() {}

RawChunk::RawChunk(const RawChunk& other) : single_tile(other.single_tile) {
    if (other.array_chunk) {
        array_chunk = std::make_unique<Data>();
        array_chunk->data = other.array_chunk->data;
    }
}

RawChunk::RawChunk(RawChunk&&) = default;

RawChunk& RawChunk::operator=(RawChunk&& other) {
    if (this == &other) return *this;
    array_chunk = std::move(other.array_chunk);
    single_tile = other.single_tile;
    return *this;
}

RawChunk& RawChunk::operator=(const RawChunk& other) {
    if (this == &other) return *this;
    RawChunk copy(other);
    *this = std::move(copy);
    return *this;
}

struct WorldImpl : World {
    tImplClass(World);

    WorldImpl() = default;

    void init() override { SubApp::init(); }
    void loop() override {
        SubApp::loop();
        run_limited(1) try_snapback();
    }

    void try_snapback() {
        glm::vec<3, double> distance;
        if (camera.x > snap_back_threshold) distance.x -= snap_back_threshold;
        if (camera.x < -snap_back_threshold) distance.x += snap_back_threshold;
        if (camera.y > snap_back_threshold) distance.y -= snap_back_threshold;
        if (camera.y < -snap_back_threshold) distance.y += snap_back_threshold;
        if (camera.z > snap_back_threshold) distance.z -= snap_back_threshold;
        if (camera.z < -snap_back_threshold) distance.z += snap_back_threshold;

        camera.x += distance.x;
        camera.y += distance.y;
        camera.z += distance.z;

        for (auto& [_, callback] : snap_back_callback) callback(distance);
    }

    void quit() override {
        if (auto* pool = application->get_subapp_ptr<ThreadPool>()) pool->quit();
        SubApp::quit();
    }

    bool chunk_in_fov(Vec3i pos, PosLodLevel lod, double aspect_ratio = 2.0) const override {
        const double chunk_size = chunk_extent(lod);
        // yes if the camera is inside the chunk
        if (camera.x >= pos.x * chunk_size && camera.x <= (pos.x + 1) * chunk_size && camera.y >= pos.y * chunk_size &&
            camera.y <= (pos.y + 1) * chunk_size && camera.z >= pos.z * chunk_size &&
            camera.z <= (pos.z + 1) * chunk_size)
            return true;
        // yes if the camera is next to the chunk (within one chunk width in any direction)
        if (camera.x >= (pos.x - 1) * chunk_size && camera.x <= (pos.x + 2) * chunk_size &&
            camera.y >= (pos.y - 1) * chunk_size && camera.y <= (pos.y + 2) * chunk_size &&
            camera.z >= (pos.z - 1) * chunk_size && camera.z <= (pos.z + 2) * chunk_size)
            return true;
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
} REGISTER_SUBAPP(WorldImpl);

struct ChunkMeshLoop : World::Module {
    tClass;

    ChunkMesher* mesher = nullptr;
    std::atomic_bool done = false;
    std::unique_ptr<WorkAdder> work_adder;

    /// Chunks currently being meshed by some worker. A chunk that is re-queued while it is in flight stays in the
    /// pending set and is only picked up once the running job finishes, so two workers never mesh the same chunk
    /// concurrently and an older mesh can never be published after a newer one.
    std::mutex in_flight_mutex;
    std::vector<World::ChunkIndex> in_flight;

    static bool same_chunk(const World::ChunkIndex& a, const World::ChunkIndex& b) {
        return a.lod == b.lod && a.pos.x == b.pos.x && a.pos.y == b.pos.y && a.pos.z == b.pos.z;
    }

    /// Caller must hold in_flight_mutex.
    bool is_in_flight_locked(const World::ChunkIndex& idx) const {
        return std::any_of(
            in_flight.begin(), in_flight.end(), [&](const World::ChunkIndex& other) { return same_chunk(idx, other); });
    }

    void release(const World::ChunkIndex& idx) {
        std::lock_guard lock(in_flight_mutex);
        const auto it = std::find_if(
            in_flight.begin(), in_flight.end(), [&](const World::ChunkIndex& other) { return same_chunk(idx, other); });
        if (it != in_flight.end()) {
            *it = in_flight.back();
            in_flight.pop_back();
        }
    }

    /// Releases the in-flight claim on scope exit, even if meshing throws.
    struct InFlightGuard {
        ChunkMeshLoop& self;
        World::ChunkIndex idx;
        ~InFlightGuard() { self.release(idx); }
    };

    /// Pops the pending chunk that is closest to the camera in world space.
    /// This keeps newly visible chunks ahead of distant work regardless of set ordering.
    /// Thread-safe: the chosen chunk is removed from the pending set and marked in-flight atomically.
    std::optional<World::ChunkIndex> take_next() {
        auto& data = world->mesh_data;
        // Lock order: chunks_to_mesh_mutex -> in_flight_mutex (release() only takes in_flight_mutex).
        std::lock_guard pending_lock(data.chunks_to_mesh_mutex);
        std::lock_guard flight_lock(in_flight_mutex);

        std::optional<World::ChunkIndex> best;
        double best_distance = 0.0;
        for (PosLodLevel lod = 0; lod < data.chunks_to_mesh.size(); ++lod) {
            for (const Vec3i& pos : data.chunks_to_mesh[lod]) {
                const World::ChunkIndex candidate{pos, lod};
                if (is_in_flight_locked(candidate)) continue;  // leave it queued for later

                const Vec3i camera_chunk = world->camchunk(lod);
                const double scale = static_cast<double>(world->chunk_size(lod));
                const double dx = (pos.x - camera_chunk.x) * scale;
                const double dy = (pos.y - camera_chunk.y) * scale;
                const double dz = (pos.z - camera_chunk.z) * scale;
                const double distance = dx * dx + dy * dy + dz * dz;
                if (!best || distance < best_distance || (distance == best_distance && lod < best->lod)) {
                    best = candidate;
                    best_distance = distance;
                }
            }
        }

        if (!best) return std::nullopt;
        data.chunks_to_mesh[best->lod].erase(best->pos);
        in_flight.push_back(*best);
        return best;
    }

    void mesh_chunk(const World::ChunkIndex index) {
        const Vec3i pos = index.pos;
        const PosLodLevel lod = index.lod;

        std::optional<RawChunk> chunk;
        std::array<std::optional<RawChunk>, 6> neighbors;

        // 1) Snapshot the chunk and its six neighbours under ONE level lock, so "which neighbours exist" is
        //    consistent with the loader's insertions (it needs the unique lock to insert).
        {
            std::shared_lock levels_lock(world->levels_mutex);
            if (lod >= world->levels.size()) return;
            Level& level = world->levels[lod];
            std::shared_lock level_lock(level.mutex);

            const auto it = level.chunks.find(pos);
            // Unloaded or no longer visible: the loader already dropped the mesh, nothing to do.
            if (it == level.chunks.end() || !it->second.rendered) return;

            const auto snapshot = [](Chunk& c) {
                std::shared_lock chunk_lock(c.mutex);
                return RawChunk(static_cast<const RawChunk&>(c));
            };

            chunk.emplace(snapshot(it->second));
            uint8_t missing = 0;
            for (size_t i = 0; i < neighbors.size(); ++i) {
                const Vec3i off = CHUNK_NEIGHBOR_OFFSETS[i];
                const Vec3i npos{pos.x + off.x, pos.y + off.y, pos.z + off.z};
                const auto nit = level.chunks.find(npos);
                if (nit == level.chunks.end()) {
                    missing |= static_cast<uint8_t>(1u << i);
                    continue;
                }
                neighbors[i].emplace(snapshot(nit->second));
            }
            // Missing neighbours are meshed as air so the chunk shows up immediately; the loader re-queues this
            // chunk when one of them is generated (see WorldLoaderImpl::loop_thread).
            it->second.mesh_missing_neighbors.store(missing);
        }

        // 2) Mesh without holding any lock.
        // NOTE: runs concurrently on several threads, so ChunkMesher::mesh
        // must not use shared mutable state (use thread_local / local scratch buffers).
        const auto ptr = [&](size_t i) -> const RawChunk* { return neighbors[i] ? &*neighbors[i] : nullptr; };
        auto vertices = mesher->mesh(index, *chunk, ptr(0), ptr(1), ptr(2), ptr(3), ptr(4), ptr(5));
        vertices.shrink_to_fit();
        auto mesh = std::make_shared<const std::vector<BlockVertex>>(std::move(vertices));

        // 3) Publish only if the chunk is *still* visible. Checked under the same lock the loader uses to clear
        //    `rendered` + drop the mesh, so a stale mesh can never be published after a drop.
        {
            std::shared_lock levels_lock(world->levels_mutex);
            if (lod >= world->levels.size()) return;
            Level& level = world->levels[lod];
            std::shared_lock level_lock(level.mutex);
            const auto it = level.chunks.find(pos);
            if (it == level.chunks.end() || !it->second.rendered) return;
            world->mesh_data.publish_mesh(pos, lod, std::move(mesh));
        }
    }

    /// @return true If a chunk was processed
    /// @return false If there was no work
    bool meshloop() {
        const auto next = take_next();
        if (!next) return false;
        InFlightGuard guard{*this, *next};
        mesh_chunk(*next);
        return true;
    }

    void worker_main() {
        while (!done) {
            bool worked = false;
            try {
                worked = meshloop();
            } catch (const std::exception& e) {
                console.err << "Mesher error: " << e.what();
            }
            if (!worked) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    void init() override {
        World::Module::init();
        mesher = &world->getmod<ChunkMesher>();
        auto& pool = parent->application->load_subapp<ThreadPool>();
        work_adder = std::make_unique<WorkAdder>(pool.main_list, [this](WorkList&) { return meshloop(); }, "worldmesher", 1);
    }
    void loop() override {}
    void quit() override {
        done = true;
        parent->application->get_subapp<ThreadPool>().shutdown();
        work_adder.reset();
    }
} REGISTER_MOD(ChunkMeshLoop, World);
