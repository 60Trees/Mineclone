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
#include <array>
#include <chrono>
#include <cmath>
#include <mutex>
#include <unordered_set>
#include <vector>
#include <world/client/main.hpp>
#include <world/server/main.hpp>

#include <utils/weighted_set.hpp>
#include "world/client/mesh/main.hpp"

using namespace std;

namespace {
    /// Upper bound on how many chunks to generate in one loader tick. The real limit is
    /// the time budget in \ref WorldLoader::tick_thread, so this just stops the scan from
    /// running away when generation is unusually fast.
    constexpr size_t load_batch = 64;
    /// How long the loader stays on its own work item before returning to the thread pool.
    constexpr chrono::milliseconds load_time_budget{8};
    /// How often an already-settled loader will scan for chunks that fell out of range.
    constexpr chrono::seconds cleanup_interval{1};

    constexpr array<ChunkPos, 6> neighbor_offsets = {
        ChunkPos{0, 1, 0},   // up
        ChunkPos{0, -1, 0},  // down
        ChunkPos{0, 0, 1},   // north
        ChunkPos{0, 0, -1},  // south
        ChunkPos{1, 0, 0},   // east
        ChunkPos{-1, 0, 0},  // west
    };

    inline bool within(int value, int radius) { return std::abs(value) <= radius; }

    /// The box of chunks that should be rendered at this lod.
    inline bool in_primary(const ChunkPos& pos, const ChunkPos& camera, Pair<size_t> radius) {
        return within(pos.x - camera.x, static_cast<int>(radius.x)) &&
               within(pos.y - camera.y, static_cast<int>(radius.y)) &&
               within(pos.z - camera.z, static_cast<int>(radius.x));
    }

    /// The one-chunk shell around the primary box. These chunks are not rendered, but
    /// they must exist so the chunks on the edge of the primary box can be meshed.
    inline bool in_secondary(const ChunkPos& pos, const ChunkPos& camera, Pair<size_t> radius) {
        const int dx = std::abs(pos.x - camera.x);
        const int dy = std::abs(pos.y - camera.y);
        const int dz = std::abs(pos.z - camera.z);
        const int rx = static_cast<int>(radius.x);
        const int ry = static_cast<int>(radius.y);

        return (dx == rx + 1 && dy <= ry && dz <= rx) || (dy == ry + 1 && dx <= rx && dz <= rx) ||
               (dz == rx + 1 && dx <= rx && dy <= ry);
    }

    /// Per-lod bookkeeping. Deliberately contains no position list: candidates are
    /// re-derived from the *current* camera every tick, so a fast camera can never
    /// leave the loader working through a stale queue around its previous position.
    struct CandidateCache {
        ChunkPos camera{};
        Pair<size_t> radius{};
        bool valid = false;
        /// True once every primary and secondary chunk for this camera position exists.
        bool settled = false;
        /// Used to log each LOD reaching a full load once, instead of every time.
        bool announced = false;
    };
}  // namespace

void WorldLoader_default_options(nlohmann::json& o);

struct WorldLoader : WorldClient::ThreadedModule {
    tClass;
    utils::weighted_set<LodLevel> lod_priorities;
    shared_mutex loader_options_mutex;
    mutex loading_mutex;

    WorldMeshHandler* mesh_handler = nullptr;
    vector<CandidateCache> candidate_cache;
    chrono::steady_clock::time_point last_cleanup = chrono::steady_clock::now();

    WorldMeshHandler* get_mesh_handler() {
        if (!mesh_handler) mesh_handler = &parent->getmod<WorldMeshHandler>();
        return mesh_handler;
    }

    void queue_mesh(const ChunkIndex& key) {
        auto* handler = get_mesh_handler();
        lock_guard lock(handler->chunks_to_mesh_mutex);
        if (handler->chunks_to_mesh_pending.insert(key).second) handler->chunks_to_mesh.push_back(key);
    }

    /// Queue the chunk itself (if it is rendered) and its rendered neighbours for meshing.
    /// Neighbours are re-meshed because a chunk appearing / changing on their border
    /// changes which faces are visible.
    void mark_dirty(const ChunkIndex& key, bool render) {
        if (render) queue_mesh(key);

        shared_lock lock(client->chunks_mutex);
        for (const auto& offset : neighbor_offsets) {
            const ChunkIndex neighbor{key.pos + offset, key.lod};
            const auto it = client->chunks.find(neighbor);
            if (it != client->chunks.end() && it->second && it->second->should_render) queue_mesh(neighbor);
        }
    }

    /// Tries to make the given chunk exist, and rendered if requested.
    /// Returns true only if something changed (a chunk was generated or promoted).
    bool attempt(const ChunkIndex& key, bool render) {
        {
            shared_lock lock(client->chunks_mutex);
            const auto it = client->chunks.find(key);
            if (it != client->chunks.end() && it->second) {
                // Never demote a rendered chunk, and never regenerate an existing one.
                if (!render || it->second->should_render) return false;
            }
        }

        shared_ptr<Chunk> chunk;
        {
            // It might exist as a secondary (non-rendered) chunk; promote it.
            unique_lock lock(client->chunks_mutex);
            const auto it = client->chunks.find(key);
            if (it != client->chunks.end() && it->second) {
                if (!render || it->second->should_render) return false;
                it->second->should_render = true;
                chunk = it->second;
            }
        }

        if (!chunk) {
            // Generate outside of the lock so other threads can keep reading the map.
            auto generated = client->host->request_chunk(key);
            if (!generated) return false;
            generated->should_render = render;
            chunk = generated;

            unique_lock lock(client->chunks_mutex);
            const auto [it, inserted] = client->chunks.try_emplace(key, chunk);
            if (!inserted) {
                // Somebody else got there first; keep the existing chunk.
                if (render && !it->second->should_render) it->second->should_render = true;
                chunk = it->second;
            }
        }

        mark_dirty(key, render);
        return true;
    }

    /// Walks the primary box in Chebyshev shells around the camera, so the closest
    /// missing chunks are always attempted first — even if the camera just teleported.
    void load_primary(LodLevel lod, const ChunkPos& camera, Pair<size_t> radius, size_t& budget, bool& worked) {
        const int rx = static_cast<int>(radius.x);
        const int ry = static_cast<int>(radius.y);
        const int max_radius = std::max(rx, ry);

        for (int r = 0; r <= max_radius && budget > 0; ++r) {
            for (int x = std::max(-r, -rx); x <= std::min(r, rx) && budget > 0; ++x)
                for (int y = std::max(-r, -ry); y <= std::min(r, ry) && budget > 0; ++y)
                    for (int z = std::max(-r, -rx); z <= std::min(r, rx) && budget > 0; ++z) {
                        if (std::max({std::abs(x), std::abs(y), std::abs(z)}) != r) continue;

                        if (attempt({{camera.x + x, camera.y + y, camera.z + z}, lod}, true)) {
                            worked = true;
                            --budget;
                        }
                    }
        }
    }

    /// Loads the six faces of the shell around the primary box. These are invisible,
    /// they only exist so edge chunks can be meshed.
    void load_secondary(LodLevel lod, const ChunkPos& camera, Pair<size_t> radius, size_t& budget, bool& worked) {
        const int rx = static_cast<int>(radius.x);
        const int ry = static_cast<int>(radius.y);

        const auto try_face = [&](int x, int y, int z) {
            if (budget == 0) return;
            if (attempt({{x, y, z}, lod}, false)) {
                worked = true;
                --budget;
            }
        };

        for (int y = -ry; y <= ry && budget > 0; ++y)
            for (int z = -rx; z <= rx && budget > 0; ++z) {
                try_face(camera.x - rx - 1, camera.y + y, camera.z + z);
                try_face(camera.x + rx + 1, camera.y + y, camera.z + z);
            }
        for (int x = -rx; x <= rx && budget > 0; ++x)
            for (int z = -rx; z <= rx && budget > 0; ++z) {
                try_face(camera.x + x, camera.y - ry - 1, camera.z + z);
                try_face(camera.x + x, camera.y + ry + 1, camera.z + z);
            }
        for (int x = -rx; x <= rx && budget > 0; ++x)
            for (int y = -ry; y <= ry && budget > 0; ++y) {
                try_face(camera.x + x, camera.y + y, camera.z - rx - 1);
                try_face(camera.x + x, camera.y + y, camera.z + rx + 1);
            }
    }

    /// Drops chunks that are no longer in range, and marks shell chunks as non-rendered
    /// so they cannot be drawn even if a stale mesh is still cached.
    bool reconcile_chunks(LodLevel lod, const ChunkPos& camera, Pair<size_t> radius) {
        vector<ChunkIndex> stale;
        vector<ChunkIndex> demote;
        {
            shared_lock lock(client->chunks_mutex);
            stale.reserve(64);
            demote.reserve(64);

            for (const auto& [key, chunk] : client->chunks) {
                if (key.lod != lod || !chunk) continue;

                const bool primary = in_primary(key.pos, camera, radius);
                const bool secondary = !primary && in_secondary(key.pos, camera, radius);
                if (!primary && !secondary)
                    stale.push_back(key);
                else if (secondary && chunk->should_render)
                    demote.push_back(key);
            }
        }

        if (stale.empty() && demote.empty()) return false;

        if (!demote.empty()) {
            unique_lock lock(client->chunks_mutex);
            for (const auto& key : demote) {
                const auto it = client->chunks.find(key);
                if (it != client->chunks.end() && it->second) it->second->should_render = false;
            }
        }

        if (!stale.empty()) {
            {
                unique_lock lock(client->chunks_mutex);
                for (const auto& key : stale) client->chunks.erase(key);
            }

            auto* handler = get_mesh_handler();
            {
                unique_lock lock(handler->chunks_to_mesh_mutex);
                unordered_set<ChunkIndex> stale_set(stale.begin(), stale.end());
                handler->chunks_to_mesh.remove_if([&](const ChunkIndex& key) { return stale_set.contains(key); });
                for (const auto& key : stale) handler->chunks_to_mesh_pending.erase(key);
            }
            {
                unique_lock lock(handler->chunk_meshes_mutex);
                for (const auto& key : stale) handler->chunk_meshes.erase(key);
            }
        }

        return true;
    }

    bool try_lod(LodLevel lod) {
        if (lod >= candidate_cache.size()) return false;

        CandidateCache& cache = candidate_cache[lod];
        const ChunkPos camera = client->get_camera_pos(lod, false);
        const Pair<size_t> radius = client->view_distance(lod);

        const bool moved =
            !cache.valid || camera != cache.camera || radius.x != cache.radius.x || radius.y != cache.radius.y;
        if (moved) {
            const bool had_cache = cache.valid;
            cache.valid = true;
            cache.camera = camera;
            cache.radius = radius;
            cache.settled = false;
            // Drop chunks left behind by the old position immediately. This is what
            // makes a teleport stop loading (and rendering) around the old spot.
            if (had_cache) reconcile_chunks(lod, camera, radius);
        }

        if (cache.settled) {
            // The region is full. Only re-check occasionally for out-of-range chunks
            // (e.g. after changing lod or view distance).
            const auto now = chrono::steady_clock::now();
            if (now - last_cleanup >= cleanup_interval) {
                last_cleanup = now;
                return reconcile_chunks(lod, camera, radius);
            }
            return false;
        }

        size_t budget = load_batch;
        bool worked = false;

        load_primary(lod, camera, radius, budget, worked);
        if (budget > 0) load_secondary(lod, camera, radius, budget, worked);

        // If the budget was not exhausted, the whole region has been visited.
        cache.settled = budget > 0;
        if (cache.settled && !cache.announced) {
            cache.announced = true;
            console.debug << "LOD " << lod << " loaded (" << radius.x << ", " << radius.y << ")";
        }
        return worked;
    }

    void init() override {
        WorldClient::ThreadedModule::init();

        {
            unique_lock __(loader_options_mutex);
            WorldLoader_default_options(client->options["world"]["loader"]);
        }

        shared_lock __(loader_options_mutex);

        std::vector<double> lod_priorities_vec;
        std::vector<std::vector<size_t>> lod_radii;
        {
            shared_lock __(client->options_mutex);
            lod_priorities_vec = client->options["world"]["loader"]["lod_priorities"].get<std::vector<double>>();
            lod_radii = client->options["world"]["loader"]["radius"].get<std::vector<std::vector<size_t>>>();
        }

        tassertmsg(lod_priorities_vec.size() == client->lod_count,
            "There must be exactly one loader priority per LOD level");
        tassertmsg(lod_radii.size() == client->lod_count, "There must be exactly one radius per LOD level");

        for (size_t lod = 0; lod < lod_priorities_vec.size(); ++lod) {
            const double priority = lod_priorities_vec[lod];
            tassertmsg(priority >= 0.0, "Loader LOD priority must not be negative");
            lod_priorities.add(static_cast<LodLevel>(lod), priority);
        }

        client->lod_view_distance.resize(client->lod_count);
        for (size_t lod = 0; lod < lod_radii.size(); ++lod) {
            tassertmsg(lod_radii[lod].size() >= 2, "Each LOD radius needs [horizontal, vertical]");
            client->lod_view_distance[lod] = {lod_radii[lod][0], lod_radii[lod][1]};
        }

        candidate_cache.resize(client->lod_count);
        last_cleanup = chrono::steady_clock::now();
    }

    bool tick_thread() override {
        unique_lock load_lock(loading_mutex, try_to_lock);
        if (!load_lock.owns_lock()) return false;
        shared_lock __(loader_options_mutex);

        // Keep generating for a short slice instead of returning to the pool after every
        // batch. This keeps the loader responsive to a moving camera without starving it.
        const auto start = chrono::steady_clock::now();
        bool worked = false;
        while (chrono::steady_clock::now() - start < load_time_budget) {
            if (!lod_priorities.try_random([&](LodLevel lod) { return try_lod(lod); })) break;
            worked = true;
        }
        return worked;
    }
} REGISTER_MOD(WorldLoader, WorldClient);
