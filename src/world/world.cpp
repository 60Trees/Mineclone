#include "world.hpp"
#include <base/poller.hpp>
#include <base/fps_handler.hpp>
#include <glm/common.hpp>
#include "triolib.hpp"
#include <atomic>
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

    /// @return true If the mesher loop found work
    /// @return false If the mesher loop found no work
    bool meshloop() {
        std::vector<std::map<Vec3i, std::vector<BlockVertex>>> meshes;

        std::vector<std::map<Vec3i, RawChunk>> chunks_to_mesh;
        {
            auto& data = world->mesh_data;
            std::lock_guard __(data.chunks_to_mesh_mutex);
            if (data.chunks_to_mesh.empty()) return false;
            for (PosLodLevel lod = 0; lod < data.chunks_to_mesh.size(); ++lod) {
                std::map<Vec3i, RawChunk> lod_chunks_to_mesh;
                for (const auto& [pos, chunk] : data.chunks_to_mesh[lod]) {
                    lod_chunks_to_mesh[pos] = std::move(chunk);
                }
                chunks_to_mesh.push_back(std::move(lod_chunks_to_mesh));
            }
            data.chunks_to_mesh.clear();
            if (chunks_to_mesh.empty()) return false;
        }

        auto& mesher = parent->getmod<ChunkMesher>();

        // mesher.mesh({pos, lod}, RawChunk&) -> std::vector<BlockVertex>
        for (PosLodLevel lod = 0; lod < chunks_to_mesh.size(); ++lod) {
            meshes.push_back({});
            for (const auto& [pos, chunk] : chunks_to_mesh[lod]) {
                const auto sample = [&](int x, int y, int z) {
                    Vec3i neighbor_pos = pos;
                    while (x < 0) {
                        x += static_cast<int>(CHUNK_SIZE);
                        --neighbor_pos.x;
                    }
                    while (x >= static_cast<int>(CHUNK_SIZE)) {
                        x -= static_cast<int>(CHUNK_SIZE);
                        ++neighbor_pos.x;
                    }
                    while (y < 0) {
                        y += static_cast<int>(CHUNK_SIZE);
                        --neighbor_pos.y;
                    }
                    while (y >= static_cast<int>(CHUNK_SIZE)) {
                        y -= static_cast<int>(CHUNK_SIZE);
                        ++neighbor_pos.y;
                    }
                    while (z < 0) {
                        z += static_cast<int>(CHUNK_SIZE);
                        --neighbor_pos.z;
                    }
                    while (z >= static_cast<int>(CHUNK_SIZE)) {
                        z -= static_cast<int>(CHUNK_SIZE);
                        ++neighbor_pos.z;
                    }
                    if (neighbor_pos == pos) return chunk.get(x, y, z);
                    const auto neighbor = chunks_to_mesh[lod].find(neighbor_pos);
                    return neighbor == chunks_to_mesh[lod].end() ? TileNull : neighbor->second.get(x, y, z);
                };
                meshes[lod][pos] = mesher.mesh({pos, lod}, chunk, sample);
            }
        }

        {
            auto& data = world->mesh_data;
            std::unique_lock __(data.meshes_mutex);
            if (data.meshes.size() < meshes.size()) data.meshes.resize(meshes.size());
            for (PosLodLevel lod = 0; lod < meshes.size(); ++lod)
                for (auto& [pos, mesh] : meshes[lod]) data.meshes[lod][pos] = std::move(mesh);
        }

        return true;
    }

    void init() override {
        World::Module::init();
        mesher = &world->getmod<ChunkMesher>();
        mesher_thread = std::thread{[this]() {
            while (!done)
                if (!meshloop()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }};
    }
    std::atomic_bool done = false;
    std::thread mesher_thread;
    void loop() override {}
    void quit() override {
        done = true;
        if (mesher_thread.joinable()) mesher_thread.join();
    }
} REGISTER_MOD(ChunkMeshLoop, World);
