/**
 * @file loop.cpp
 * @brief The meshing loop implementaiton
 * @author 60Trees_ (github.com/60Trees)
 * @detail
 * This file is responsible for actually scheduling
 * the meshing of chunks. It works on a multithreaded
 * basis
 */

#include "main.hpp"
#include "triolib.hpp"
#include <mutex>
#include <optional>
#include <world/shared.hpp>

using namespace std;

// Properly handles the thread pool for the meshing loop.
// Does not actually contian any meshing code, just calls it
struct WorldMeshHandlerImpl : WorldMeshHandler {
    tImplClass(WorldMeshHandler);
    WorldMesher* mesher = nullptr;
    void init() override {
        WorldClient::ThreadedModule::init();
        mesher = &parent->getmod<WorldMesher>();
    }

    bool tick_thread() override {
        shared_lock __(client->chunks_mutex);
        auto work = get_work();
        if (!work) return false;

        vector<shared_lock<shared_mutex>> locks;
        locks.reserve(7);
        locks.emplace_back(shared_lock(client->chunks.at(*work)->mutex));
        for (const auto offset : neighbor_offsets) {
            locks.emplace_back(shared_lock(client->chunks.at(ChunkIndex{work->pos + offset, work->lod})->mutex));
        }

        mesh_chunk(*work);

        return true;
    }

    constexpr static array<ChunkPos, 6> neighbor_offsets = {
        ChunkPos{0, 1, 0},   // up
        ChunkPos{0, -1, 0},  // down
        ChunkPos{0, 0, 1},   // north
        ChunkPos{0, 0, -1},  // south
        ChunkPos{1, 0, 0},   // east
        ChunkPos{-1, 0, 0},  // west
    };

    // Collects the chunk and its neighbors and meshes it, putting the result in \ref chunk_meshes
    //
    // locks: chunk_meshes_mutex (unique)
    void mesh_chunk(ChunkIndex work) {
        if (!client->chunks.contains(work)) return;

        array<shared_ptr<Chunk>, 6> neighbors;
        shared_ptr<Chunk> own_chunk = make_shared<Chunk>(client->chunks.at(work)->duplicate());

        size_t i = 0;
        for (const auto offset : neighbor_offsets) {
            ChunkIndex neighbor_index{work.pos + offset, work.lod};
            if (!client->chunks.contains(neighbor_index)) return;
            neighbors[i] = make_shared<Chunk>(client->chunks.at(neighbor_index)->duplicate());
            i++;
        }

        unique_lock __(chunk_meshes_mutex);
        chunk_meshes[work] = mesher->mesh_chunk(work, own_chunk, neighbors);
    }

    enum MeshStatus { CAN_MESH, CANNOT_MESH, DOES_NOT_EXIST };

    // Whether or not a certain chunk is possible to be meshed (Does it exist? Do its neighbors exist?)
    MeshStatus can_mesh(ChunkIndex work) {
        if (!client->chunks.contains(work)) return DOES_NOT_EXIST;

        for (const auto offset : neighbor_offsets) {
            if (!client->chunks.contains(ChunkIndex{work.pos + offset, work.lod})) return CANNOT_MESH;
        }
        return CAN_MESH;
    }

    // Returns a chunk that is (probably) ready to be meshed. It might have changed between the locks
    // from some other thread, so double check just in case
    // locks: chunks_to_mesh_mutex
    // calls: can_mesh
    optional<ChunkIndex> get_work() {
        lock_guard __(chunks_to_mesh_mutex);
        if (chunks_to_mesh.empty()) return nullopt;
        optional<ChunkIndex> retval = nullopt;
        set<ChunkIndex> to_remove;
        for (auto it = chunks_to_mesh.begin(); it != chunks_to_mesh.end(); it++) {
            const auto status = can_mesh(*it);
            if (status == DOES_NOT_EXIST) {
                to_remove.insert(*it);
            } else if (status == CAN_MESH) {
                retval = *it;
                break;
            }
        }
        chunks_to_mesh.remove_if([&](const ChunkIndex& i) { return to_remove.contains(i); });
        return retval;
    }
} REGISTER_MOD(WorldMeshHandlerImpl, WorldClient);
