#include "world.hpp"
#include <base/poller.hpp>
#include <base/fps_handler.hpp>
#include <glm/common.hpp>
#include "triolib.hpp"
#include <atomic>
#include <optional>
#include <thread>
#include <mutex>

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
} REGISTER_SUBAPP(WorldImpl);

struct ChunkMeshLoop : World::Module {
    tClass;

    ChunkMesher* mesher = nullptr;
    std::atomic_bool done = false;
    std::thread mesher_thread;

    /// Pops the pending chunk that is closest to the camera, measured in chunk widths *of its own LOD*,
    /// so coarse fallback chunks are not starved by a flood of LOD 0 work.
    std::optional<World::ChunkIndex> take_next() {
        Camera cam;
        {
            std::shared_lock camera_lock(world->camera_mutex);
            cam = world->camera;
        }

        auto& data = world->mesh_data;
        std::lock_guard pending_lock(data.chunks_to_mesh_mutex);

        std::optional<World::ChunkIndex> best;
        double best_score = 0.0;
        for (PosLodLevel lod = 0; lod < data.chunks_to_mesh.size(); ++lod) {
            const double size = static_cast<double>(CHUNK_SIZE) * std::ldexp(1.0, static_cast<int>(lod));
            for (const Vec3i& pos : data.chunks_to_mesh[lod]) {
                const double dx = ((pos.x + 0.5) * size - cam.x) / size;
                const double dy = ((pos.y + 0.5) * size - cam.y) / size;
                const double dz = ((pos.z + 0.5) * size - cam.z) / size;
                const double score = dx * dx + dy * dy + dz * dz;
                if (!best || score < best_score) {
                    best = World::ChunkIndex{pos, lod};
                    best_score = score;
                }
            }
        }
        if (best) data.chunks_to_mesh[best->lod].erase(best->pos);
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
        mesh_chunk(*next);
        return true;
    }

    void init() override {
        World::Module::init();
        mesher = &world->getmod<ChunkMesher>();
        mesher_thread = std::thread{[this]() {
            while (!done) {
                bool worked = false;
                try {
                    worked = meshloop();
                } catch (const std::exception& e) {
                    run_limited(1) console.log << "Mesher error: " << e.what();
                }
                if (!worked) std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }};
    }
    void loop() override {}
    void quit() override {
        done = true;
        if (mesher_thread.joinable()) mesher_thread.join();
    }
} REGISTER_MOD(ChunkMeshLoop, World);
