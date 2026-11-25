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

    /// @brief One candidate that still needs work once the batch scan is done.
    struct CandidateWork {
        ChunkIndex key;
        bool render = false;
        /// Already exists but must be promoted from secondary to primary.
        bool promote = false;
        /// The chunk to insert / the promoted chunk, filled in while the batch is processed.
        shared_ptr<Chunk> chunk;
    };

    /// Per-lod bookkeeping. The candidate list is rebuilt only when the camera enters a
    /// new chunk (or the radius changes); until then a forward-only cursor walks it once.
    /// This is what stops the loader from re-deriving and re-scanning the whole region
    /// from the centre on every single tick.
    struct CandidateCache {
        ChunkPos camera{};
        Pair<size_t> radius{};
        bool valid = false;
        /// True once every primary and secondary chunk for this camera position exists.
        bool settled = false;
        /// Used to log each LOD reaching a full load once, instead of every time.
        bool announced = false;

        /// @brief Every chunk this LOD needs, in load order: the centre-out primary box
        /// followed by the secondary shell.
        std::vector<ChunkIndex> candidates;
        /// @brief `candidates[0..primary_count)` are primary (renderable) chunks.
        size_t primary_count = 0;
        /// @brief Next candidate that has not been inspected yet.
        size_t cursor = 0;
        /// @brief Scratch buffer so a tick does not allocate.
        std::vector<CandidateWork> batch;
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

    /// @brief Fill \p cache's candidate list for one camera position, in load order.
    /// @detail The primary box is walked in Chebyshev shells so the closest chunks are
    /// attempted first. The shell walk visits only the shell itself, instead of scanning
    /// the whole cube and skipping the interior. The secondary shell follows.
    void rebuild_candidates(CandidateCache& cache, LodLevel lod, const ChunkPos& camera, Pair<size_t> radius) {
        const int rx = static_cast<int>(radius.x);
        const int ry = static_cast<int>(radius.y);
        const int max_radius = std::max(rx, ry);

        cache.candidates.clear();
        cache.candidates.reserve(static_cast<size_t>(2 * rx + 3) * (2 * ry + 3) * (2 * rx + 3));

        const auto add = [&](int x, int y, int z) {
            cache.candidates.push_back({{camera.x + x, camera.y + y, camera.z + z}, lod});
        };

        add(0, 0, 0);
        for (int r = 1; r <= max_radius; ++r) {
            const int xr = std::min(r, rx);
            const int yr = std::min(r, ry);

            // x = +/-r faces, the full y/z cross-section.
            if (r <= rx) {
                for (int y = -yr; y <= yr; ++y)
                    for (int z = -xr; z <= xr; ++z) {
                        add(-r, y, z);
                        add(r, y, z);
                    }
            }
            // y = +/-r faces, without the x edges already covered above.
            if (r <= ry) {
                for (int x = -xr; x <= xr; ++x) {
                    if (std::abs(x) == r) continue;
                    for (int z = -xr; z <= xr; ++z) {
                        add(x, -r, z);
                        add(x, r, z);
                    }
                }
            }
            // z = +/-r faces, without the x/y edges already covered above.
            if (r <= rx) {
                for (int x = -xr; x <= xr; ++x) {
                    if (std::abs(x) == r) continue;
                    for (int y = -yr; y <= yr; ++y) {
                        if (std::abs(y) == r) continue;
                        add(x, y, -r);
                        add(x, y, r);
                    }
                }
            }
        }
        cache.primary_count = cache.candidates.size();

        // The six faces of the one-chunk shell. These are invisible; they only exist so
        // the chunks on the edge of the primary box can be meshed.
        for (int y = -ry; y <= ry; ++y)
            for (int z = -rx; z <= rx; ++z) {
                add(-rx - 1, y, z);
                add(rx + 1, y, z);
            }
        for (int x = -rx; x <= rx; ++x)
            for (int z = -rx; z <= rx; ++z) {
                add(x, -ry - 1, z);
                add(x, ry + 1, z);
            }
        for (int x = -rx; x <= rx; ++x)
            for (int y = -ry; y <= ry; ++y) {
                add(x, y, -rx - 1);
                add(x, y, rx + 1);
            }

        cache.cursor = 0;
        cache.settled = false;
    }

    /// @brief Walk forward from the cursor, collecting up to \p limit candidates that
    /// still need work. Every existence check is made under a single shared lock, and
    /// the cursor only moves forward, so each candidate is inspected once.
    void collect_batch(CandidateCache& cache, size_t limit, vector<CandidateWork>& out) {
        out.clear();
        shared_lock lock(client->chunks_mutex);
        while (cache.cursor < cache.candidates.size() && out.size() < limit) {
            const size_t index = cache.cursor++;
            const ChunkIndex& key = cache.candidates[index];
            const bool render = index < cache.primary_count;

            const auto it = client->chunks.find(key);
            if (it != client->chunks.end() && it->second) {
                // Already there. Primary chunks that exist but are not rendered are
                // promoted; everything else needs nothing.
                if (it->second->should_render || !render) continue;
                out.push_back({key, render, true, nullptr});
            } else {
                out.push_back({key, render, false, nullptr});
            }
        }
    }

    /// @brief Generate / promote one batch of candidates.
    /// @detail Generation runs outside the map lock, and the map is only locked once to
    /// insert the whole batch and once to queue the resulting mesh work. The loader is
    /// the only writer and ticks are serialised by \ref loading_mutex, so the state the
    /// batch scan saw is still valid when it is applied.
    void process_batch(CandidateCache& cache, size_t& budget, bool& worked) {
        vector<CandidateWork>& batch = cache.batch;
        collect_batch(cache, std::min(budget, load_batch), batch);

        for (auto& item : batch) {
            if (item.promote) continue;
            item.chunk = client->host->request_chunk(item.key);
            if (item.chunk) item.chunk->should_render = item.render;
        }

        {
            unique_lock lock(client->chunks_mutex);
            for (auto& item : batch) {
                if (item.promote) {
                    const auto it = client->chunks.find(item.key);
                    if (it != client->chunks.end() && it->second) {
                        it->second->should_render = true;
                        item.chunk = it->second;
                    }
                    continue;
                }
                if (!item.chunk) continue;
                const auto [it, inserted] = client->chunks.try_emplace(item.key, item.chunk);
                if (!inserted) {
                    if (item.render && !it->second->should_render) it->second->should_render = true;
                    item.chunk = it->second;
                }
            }
        }

        // Queue the new / promoted chunks and the rendered neighbours whose border
        // changed. One shared lock and one queue lock for the whole batch.
        vector<ChunkIndex> to_mesh;
        to_mesh.reserve(batch.size() * (neighbor_offsets.size() + 1));
        {
            shared_lock lock(client->chunks_mutex);
            for (const auto& item : batch) {
                if (!item.chunk) continue;
                if (item.render) to_mesh.push_back(item.key);
                for (const auto& offset : neighbor_offsets) {
                    const ChunkIndex neighbor{item.key.pos + offset, item.key.lod};
                    const auto it = client->chunks.find(neighbor);
                    if (it != client->chunks.end() && it->second && it->second->should_render)
                        to_mesh.push_back(neighbor);
                }
            }
        }
        if (!to_mesh.empty()) {
            auto* handler = get_mesh_handler();
            lock_guard lock(handler->chunks_to_mesh_mutex);
            for (const auto& key : to_mesh)
                if (handler->chunks_to_mesh_pending.insert(key).second) handler->chunks_to_mesh.push_back(key);
        }

        size_t processed = 0;
        for (const auto& item : batch)
            if (item.chunk) processed++;
        budget -= std::min(budget, processed);
        worked = worked || processed > 0;
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
            // Drop chunks left behind by the old position immediately. This is what
            // makes a teleport stop loading (and rendering) around the old spot.
            if (had_cache) reconcile_chunks(lod, camera, radius);
            // Build the ordered candidate list for the new position. This is one pass
            // over the region (about 14 us at LOD 0) and only happens when the camera
            // actually enters a new chunk.
            rebuild_candidates(cache, lod, camera, radius);
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
        process_batch(cache, budget, worked);

        // The cursor only stops short of the end when the budget ran out, so reaching
        // the end means every candidate has been visited.
        if (cache.cursor >= cache.candidates.size()) {
            cache.settled = true;
            if (!cache.announced) {
                cache.announced = true;
                console.debug << "LOD " << lod << " loaded (" << radius.x << ", " << radius.y << ")";
            }
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
