#include "world.hpp"
#include <base/poller.hpp>
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

Tile TileNull = {0, 0};

using namespace std;
using namespace triolib;

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

struct WorldImpl : World, Poller {
    tImplClass(World);
    WorldImpl() : Poller(5) {}

    void init() override { SubApp::init(); }
    void loop() override {
        SubApp::loop();
        Poller::trypoll(this);
    }
    void poll() override {
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
    void quit() override { SubApp::quit(); }

    bool chunk_in_fov(Vec3i pos, PosLodLevel lod, double aspect_ratio = 2.0) const override {
        const double scale = std::ldexp(1.0, static_cast<int>(lod));
        const double chunk_size = static_cast<double>(CHUNK_SIZE) * scale;
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

    /// Tune this. Leave a couple of cores for the main/render thread and the world loader.
    static size_t default_worker_count() {
        const size_t hw = std::max<size_t>(1, std::thread::hardware_concurrency());
        return hw > 3 ? std::min<size_t>(hw - 2, 12) : 1;
    }

    ChunkMesher* mesher = nullptr;
    std::atomic_bool done = false;
    std::vector<std::thread> workers;

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

    /// Pops the pending chunk that is closest to the camera, measured in chunk widths *of its own LOD*,
    /// so coarse fallback chunks are not starved by a flood of LOD 0 work.
    /// Thread-safe: the chosen chunk is removed from the pending set and marked in-flight atomically.
    std::optional<World::ChunkIndex> take_next() {
        Camera cam;
        {
            std::shared_lock camera_lock(world->camera_mutex);
            cam = world->camera;
        }

        auto& data = world->mesh_data;
        // Lock order: chunks_to_mesh_mutex -> in_flight_mutex (release() only takes in_flight_mutex).
        std::lock_guard pending_lock(data.chunks_to_mesh_mutex);
        std::lock_guard flight_lock(in_flight_mutex);

        std::optional<World::ChunkIndex> best;
        double best_score = 0.0;
        for (PosLodLevel lod = 0; lod < data.chunks_to_mesh.size(); ++lod) {
            const double size = static_cast<double>(CHUNK_SIZE) * std::ldexp(1.0, static_cast<int>(lod));
            for (const Vec3i& pos : data.chunks_to_mesh[lod]) {
                const double dx = ((pos.x + 0.5) * size - cam.x) / size;
                const double dy = ((pos.y + 0.5) * size - cam.y) / size;
                const double dz = ((pos.z + 0.5) * size - cam.z) / size;
                const double score = dx * dx + dy * dy + dz * dz;
                if (best && score >= best_score) continue;
                const World::ChunkIndex candidate{pos, lod};
                if (is_in_flight_locked(candidate)) continue;  // leave it queued for later
                best = candidate;
                best_score = score;
            }
        }
        if (best) {
            data.chunks_to_mesh[best->lod].erase(best->pos);
            in_flight.push_back(*best);
        }
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

        // 2) Mesh without holding any lock. NOTE: runs concurrently on several threads, so ChunkMesher::mesh
        //    must not use shared mutable state (use thread_local / local scratch buffers).
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
                run_limited(1) console.log << "Mesher error: " << e.what();
            }
            if (!worked) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    void init() override {
        World::Module::init();
        mesher = &world->getmod<ChunkMesher>();
        const size_t count = default_worker_count();
        workers.reserve(count);
        for (size_t i = 0; i < count; ++i) workers.emplace_back([this]() { worker_main(); });
    }
    void loop() override {}
    void quit() override {
        done = true;
        for (auto& t : workers)
            if (t.joinable()) t.join();
        workers.clear();
    }
} REGISTER_MOD(ChunkMeshLoop, World);
