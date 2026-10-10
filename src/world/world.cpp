#include "world.hpp"
#include <base/poller.hpp>
#include <base/fps_handler.hpp>
#include <glm/common.hpp>
#include "triolib.hpp"
#include <atomic>
#include <thread>
#include <mutex>

using namespace std;
using namespace triolib;

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
        std::vector<UnlockedChunk> chunks_to_mesh;
        std::vector<Vec3i> positions_to_mesh;

        {
            std::shared_lock lock(world->chunks_mutex);
            for (auto& [pos, chunk] : world->chunks) {
                std::shared_lock chunk_lock(chunk.mutex);
                if (chunk.needs_remesh) {
                    auto raw_chunk = chunk.raw_chunk
                        ? std::make_unique<RawChunk>(chunk.raw_chunk->data)
                        : nullptr;
                    chunks_to_mesh.emplace_back(std::move(raw_chunk), chunk.single_tile);
                    positions_to_mesh.push_back(pos);
                }
            }
        }

        if (chunks_to_mesh.empty()) return false;
        for (size_t i = 0; i < chunks_to_mesh.size(); ++i) {
            mesher->mesh(positions_to_mesh[i], chunks_to_mesh[i]);
        }

        std::shared_lock lock(world->chunks_mutex);
        for (auto& [pos, chunk] : world->chunks) {
            std::unique_lock chunk_lock(chunk.mutex);
            for (size_t i = 0; i < positions_to_mesh.size(); ++i) {
                if (positions_to_mesh[i] == pos) {
                    chunk.mesh = std::move(chunks_to_mesh[i].mesh);
                    chunk.needs_remesh = false;
                }
            }
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
