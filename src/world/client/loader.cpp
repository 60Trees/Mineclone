/**
 * @file world/client/loader.cpp
 * @brief The world loader
 * @author 60Trees_ (github.com/60Trees)
 * @detail
 * This file is responsible for deciding
 * which chunks to load / unload. Works
 * multithreaded
 */

#include <algorithm>
#include <mutex>
#include <world/client/main.hpp>
#include <world/server/main.hpp>

#include <utils/weighted_set.hpp>
#include "world/client/mesh/main.hpp"

using namespace std;

/// The radius of each lod distance.
extern size_t (*default_get_view_distance)(LodLevel lod);
/// The priority of each lod level, used for determining which lod is more important
/// (e.g 0 lod is closest to the player = most important). Ran in a sequential manner,
/// lod incrementing until the priority is <= 0.0
extern double (*default_get_lod_priority)(LodLevel lod);
/// Very limit of how many lod levels there can be
extern size_t* max_lod_count;

struct WorldLoader : WorldClient::ThreadedModule {
    tClass;
    utils::weighted_set<LodLevel> lod_priorities;
    double (*get_lod_priority)(LodLevel lod) = default_get_lod_priority;
    size_t (*get_view_distance)(LodLevel lod) = default_get_view_distance;
    shared_mutex loader_options_mutex;

    WorldMeshHandler* mesh_handler = nullptr;

    bool try_load_chunk(LodLevel lod, const set<ChunkPos>& primary_chunks, const set<ChunkPos>& secondary_chunks) {
        auto chunk_exists = [&](const ChunkPos& pos, bool needed_render_state) {
            const ChunkIndex key{pos, lod};
            shared_lock __(client->chunks_mutex);
            if (!client->chunks.contains(key)) return false;
            if (!client->chunks.at(key)) return false;
            if (client->chunks.at(key)->should_render != needed_render_state) return false;
            return true;
        };

        auto attempt = [&](const ChunkPos& pos, bool render) {
            const ChunkIndex key{pos, lod};
            if (chunk_exists(pos, render)) return false;
            auto chunk = client->host->request_chunk(key);
            if (!chunk) return false;
            chunk->should_render = render;
            unique_lock __(client->chunks_mutex);
            client->chunks[key] = chunk;

            if (!mesh_handler) mesh_handler = &parent->getmod<WorldMeshHandler>();
            if (render) {
                unique_lock __(mesh_handler->chunks_to_mesh);
                if (!std::ranges::contains(mesh_handler->chunks_to_mesh, key))
                    mesh_handler->chunks_to_mesh.push_front(key);
            }
            client->chunks.at(key)->should_render = render;
            return true;
        };

        for (const auto& pos : primary_chunks)
            if (attempt(pos, true)) return true;
        for (const auto& pos : secondary_chunks)
            if (attempt(pos, false)) return true;

        optional<ChunkPos> cleanup_pos = [&] -> optional<ChunkPos> {
            shared_lock __(client->chunks_mutex);
            for (const auto& [key, chunk] : client->chunks) {
                if (key.lod != lod) continue;
                if (!primary_chunks.contains(key.pos) && !secondary_chunks.contains(key.pos)) return key.pos;
            }
            return nullopt;
        }();

        if (cleanup_pos) {
            const ChunkIndex key{*cleanup_pos, lod};
            unique_lock __(client->chunks_mutex);
            client->chunks.erase(key);
            return true;
        }

        return false;
    }

    bool try_unload_chunk(LodLevel lod, const set<ChunkPos>& primary_chunks, const set<ChunkPos>& secondary_chunks) {
        return true;
    }

    bool try_lod(LodLevel lod) {
        ChunkPos campos = client->get_camera_pos(lod);
        size_t radius = get_view_distance(lod);

        // Visible chunks
        set<ChunkPos> primary_chunks{};
        // Invisible chunks needed to mesh the visible ones
        set<ChunkPos> secondary_chunks{};

        // loop in a growing spiral, closest goes first
        for (uint r = 0; r <= radius; r++)
            for (int x = -r; x <= r; x++)
                for (int y = -r; y <= r; y++)
                    for (int z = -r; z <= r; z++) {
                        if (abs(x) != r && abs(y) != r && abs(z) != r) continue;

                        ChunkIndex index{{campos.x + x, campos.y + y, campos.z + z}, lod};

                        vector<ChunkIndex> neighbors = client->get_overlapping_neighbors(index, false);

                        for (const auto& neighbor : neighbors) {
                            if (neighbor.lod != lod) continue;
                            if (!primary_chunks.contains(neighbor.pos)) secondary_chunks.insert(neighbor.pos);
                        }
                    }

        for (auto& index : primary_chunks) {
            constexpr static array<ChunkPos, 6> neighbor_offsets = {
                ChunkPos{0, 1, 0},   // up
                ChunkPos{0, -1, 0},  // down
                ChunkPos{0, 0, 1},   // north
                ChunkPos{0, 0, -1},  // south
                ChunkPos{1, 0, 0},   // east
                ChunkPos{-1, 0, 0},  // west
            };

            for (const auto& off : neighbor_offsets) {
                const ChunkPos neighbor{index.x + off.x, index.y + off.y, index.z + off.z};
                if (!primary_chunks.contains(neighbor) && !secondary_chunks.contains(neighbor))
                    secondary_chunks.insert(neighbor);
            }
        }

        enum WorkType { Load, Unload };

        const static thread_local utils::weighted_set<WorkType> work_types = [] {
            utils::weighted_set<WorkType> set{};
            set.add(WorkType::Load, 1.0);
            set.add(WorkType::Unload, 0.5);
            return set;
        }();
        return true;
    }

    void init() override {
        WorldClient::ThreadedModule::init();
        shared_lock __(loader_options_mutex);
        for (uint i = 0; i < *max_lod_count; i++) {
            double priority = get_lod_priority(i);
            if (priority <= 0.0) {
                tassertmsg(i != 0, "Uhhhhhhhhh... guys?? Might be cooked fr");
                break;
            }
            lod_priorities.add(i, priority);
        }
    }

    bool tick_thread() override {
        shared_lock __(loader_options_mutex);
        return lod_priorities.try_random([&](LodLevel lod) { return try_lod(lod); });
    }
} REGISTER_MOD(WorldLoader, WorldClient);
