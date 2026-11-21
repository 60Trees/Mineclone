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
    uint64_t mesh_revision = 0;

    void init() override {
        WorldClient::ThreadedModule::init();
        mesher = &parent->getmod<WorldMesher>();
    }

    bool tick_thread() override {
        unique_lock meshing_lock(meshing_mutex, try_to_lock);
        if (!meshing_lock.owns_lock()) return false;

        // Grab everything we need while holding the chunk map lock, then release it so
        // the loader is not blocked for the whole meshing operation. The shared_ptrs
        // keep the chunks alive.
        optional<ChunkIndex> work;
        shared_ptr<Chunk> own_chunk;
        array<shared_ptr<Chunk>, 6> neighbors;
        {
            shared_lock lock(client->chunks_mutex);
            work = get_work();
            if (!work) return false;

            const auto it = client->chunks.find(*work);
            if (it == client->chunks.end() || !it->second) return false;
            own_chunk = it->second;

            size_t i = 0;
            for (const auto offset : neighbor_offsets) {
                const auto neighbor = client->chunks.find(ChunkIndex{work->pos + offset, work->lod});
                if (neighbor != client->chunks.end() && neighbor->second) neighbors[i] = neighbor->second;
                i++;
            }
        }

        // Lock the chunks we are reading. Missing neighbours are allowed: they are
        // treated as empty space, and this chunk is re-queued when the neighbour appears.
        vector<shared_lock<shared_mutex>> locks;
        locks.reserve(7);
        locks.emplace_back(own_chunk->mutex);
        for (const auto& neighbor : neighbors)
            if (neighbor) locks.emplace_back(neighbor->mutex);

        mesh_chunk(*work, own_chunk, neighbors);

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

    // Meshes the chunk and puts the result in \ref chunk_meshes.
    // locks: chunk_meshes_mutex (unique)
    void mesh_chunk(
        ChunkIndex work, const shared_ptr<Chunk>& own_chunk, const array<shared_ptr<Chunk>, 6>& neighbors) {
        ChunkMesh result = mesher->mesh_chunk(work, own_chunk, neighbors);

        unique_lock lock(chunk_meshes_mutex);
        result.revision = ++mesh_revision;
        chunk_meshes[work] = std::move(result);
    }

    // Pops the next chunk that still exists off the queue.
    // locks: chunks_to_mesh_mutex
    // caller must hold a shared lock on chunks_mutex
    optional<ChunkIndex> get_work() {
        lock_guard lock(chunks_to_mesh_mutex);
        while (!chunks_to_mesh.empty()) {
            const ChunkIndex candidate = chunks_to_mesh.front();
            chunks_to_mesh.pop_front();
            chunks_to_mesh_pending.erase(candidate);

            const auto it = client->chunks.find(candidate);
            if (it != client->chunks.end() && it->second) return candidate;
        }
        return nullopt;
    }
} REGISTER_MOD(WorldMeshHandlerImpl, WorldClient);
