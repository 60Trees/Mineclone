/**
 * @file world/worldloader.cpp
 * @author 60Trees_ (github.com/60Trees)
 *
 * @detail This file handles the loading of chunks in the world.
 * It uses a thread pool to load or generate chunks in the background.
 */

#include "worldloader.hpp"
#include <algorithm>
#include <chrono>
#include <optional>
#include <set>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <vector>
#include <world/worldgen.hpp>
#include <unordered_map>
#include <unordered_set>
#include "triolib.hpp"
#include "world/world.hpp"

struct WorldLoaderImpl : WorldLoader {
    tImplClass(WorldLoader);

    // @brief The radius of chunks to load around the player.
    static constexpr int load_radius = 2;

    // @brief Number of LOD levels. Change it with set_lod_count().
    int lod_count = 4;
    std::unordered_map<PosLodLevel, std::set<Vec3i>> in_progress_chunks;
    std::mutex in_progress_mutex;

    WorldLoaderImpl() {
        get_next_work = [&](PosLodLevel lod) -> std::optional<std::pair<Vec3i, WorldLoader::WorkMode>> {
            // std::this_thread::sleep_for(std::chrono::milliseconds(10));
            World& w = *world;

            std::shared_lock levels_guard(w.levels_mutex);
            if (lod >= w.levels.size()) return std::nullopt;

            Level& lvl = w.levels[lod];
            std::shared_lock level_guard(lvl.mutex);

            Vec3i cam_chunk = w.camchunk(lod);
            const int lod_radius = load_radius;

            const auto queue_work =
                [&](Vec3i pos, WorldLoader::WorkMode mode) -> std::optional<std::pair<Vec3i, WorldLoader::WorkMode>> {
                switch (mode) {
                    case WorldLoader::Load:
                        if (lvl.chunks.contains(pos)) return std::nullopt;
                        break;
                    case WorldLoader::Unload:
                        if (!lvl.chunks.contains(pos)) return std::nullopt;
                        break;
                    case WorldLoader::Render:
                        if (lvl.chunks.contains(pos) && lvl.chunks[pos].rendered) return std::nullopt;
                        break;
                    case WorldLoader::NotRender:
                        if (lvl.chunks.contains(pos) && !lvl.chunks[pos].rendered) return std::nullopt;
                        break;
                };

                std::lock_guard __(in_progress_mutex);
                if (!in_progress_chunks.contains(lod)) in_progress_chunks[lod] = {};
                if (in_progress_chunks[lod].contains(pos)) return std::nullopt;

                in_progress_chunks[lod].insert(pos);
                return std::pair<Vec3i, WorldLoader::WorkMode>{pos, mode};
            };

            // Chunks that are drawn at this LOD, nearest to the camera first.
            std::vector<Vec3i> primary_order;
            std::unordered_set<Vec3i, Vec3i::Vec3iHash> primary_chunks;
            // Invisible chunks that only exist so the mesher can see across the border of a primary chunk.
            std::vector<Vec3i> secondary_order;
            std::unordered_set<Vec3i, Vec3i::Vec3iHash> secondary_chunks;

            const auto add_primary = [&](const Vec3i& pos) {
                if (primary_chunks.insert(pos).second) primary_order.push_back(pos);
            };

            for (int x = -lod_radius; x <= lod_radius; x++)
                for (int y = -lod_radius; y <= lod_radius; y++)
                    for (int z = -lod_radius; z <= lod_radius; z++) {
                        const Vec3i pos{cam_chunk.x + x, cam_chunk.y + y, cam_chunk.z + z};
                        const Vec3i origin{pos.x & ~1, pos.y & ~1, pos.z & ~1};
                        for (int dx = 0; dx < 2; dx++)
                            for (int dy = 0; dy < 2; dy++)
                                for (int dz = 0; dz < 2; dz++)
                                    add_primary({origin.x + dx, origin.y + dy, origin.z + dz});
                    }

            const auto distance_sq = [&](const Vec3i& p) {
                const long long dx = p.x - cam_chunk.x, dy = p.y - cam_chunk.y, dz = p.z - cam_chunk.z;
                return dx * dx + dy * dy + dz * dz;
            };
            std::sort(primary_order.begin(), primary_order.end(),
                [&](const Vec3i& a, const Vec3i& b) { return distance_sq(a) < distance_sq(b); });

            for (const Vec3i& pos : primary_order)
                for (const Vec3i& off : CHUNK_NEIGHBOR_OFFSETS) {
                    const Vec3i neighbor{pos.x + off.x, pos.y + off.y, pos.z + off.z};
                    if (primary_chunks.contains(neighbor)) continue;
                    if (secondary_chunks.insert(neighbor).second) secondary_order.push_back(neighbor);
                }

            const auto try_primary_chunks = [&]() -> std::optional<std::pair<Vec3i, WorldLoader::WorkMode>> {
                for (const auto& pos : primary_order)
                    if (auto work = queue_work(pos, WorldLoader::Render)) return work;
                return std::nullopt;
            };

            const auto try_secondary_chunks = [&]() -> std::optional<std::pair<Vec3i, WorldLoader::WorkMode>> {
                for (const auto& pos : secondary_order)
                    if (auto work = queue_work(pos, WorldLoader::NotRender)) return work;
                return std::nullopt;
            };

            const auto try_cleanup = [&]() -> std::optional<std::pair<Vec3i, WorldLoader::WorkMode>> {
                for (const auto& [pos, chunk] : lvl.chunks)
                    if (!primary_chunks.contains(pos) && !secondary_chunks.contains(pos))
                        if (auto work = queue_work(pos, WorldLoader::Unload)) return work;
                return std::nullopt;
            };

            thread_local std::unordered_map<PosLodLevel, unsigned> state_by_lod;
            switch ((++state_by_lod[lod]) % 3) {
                case 0:
                    if (auto work = try_primary_chunks()) return work;
                    if (auto work = try_secondary_chunks()) return work;
                    if (auto work = try_cleanup()) return work;
                    break;
                case 1:
                    if (auto work = try_secondary_chunks()) return work;
                    if (auto work = try_cleanup()) return work;
                    if (auto work = try_primary_chunks()) return work;
                    break;
                case 2:
                    if (auto work = try_cleanup()) return work;
                    if (auto work = try_primary_chunks()) return work;
                    if (auto work = try_secondary_chunks()) return work;
                    break;
            }

            return std::nullopt;
        };
        finish_work = [&](Vec3i pos, PosLodLevel lod) {
            std::lock_guard lock(in_progress_mutex);
            tassert(in_progress_chunks[lod].contains(pos));
            in_progress_chunks[lod].erase(pos);
        };
        threads_per_lod = [](PosLodLevel lod) {
            float fretval = 2;
            while (lod > 0) {
                fretval -= 0.25;
                lod -= 1;
            }
            int retval = (int)std::floor(fretval);
            if (retval == 0) retval = 1;
            return retval;
        };
    }

    virtual bool tick_unloader_thread() { return false; }

    // @brief Loader threads. Each one serves one or more LODs, see WorldLoader::plan_threads().
    std::vector<std::jthread> threads;
    std::jthread unloader_thread{[&](std::stop_token stop) {
        while (!stop.stop_requested())
            if (!tick_unloader_thread()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }};

    // @brief Resizes the LOD arrays and (re)builds the loader threads from `threads_per_lod`.
    virtual void set_lod_count(int count) {
        count = std::max(count, 0);

        threads.clear();

        lod_count = count;
        {
            std::unique_lock levels_lock(world->levels_mutex);
            world->levels.resize(static_cast<std::size_t>(count));
        }
        {
            auto& data = world->mesh_data;
            {
                std::lock_guard pending_lock(data.chunks_to_mesh_mutex);
                if (data.chunks_to_mesh.size() > static_cast<std::size_t>(count))
                    data.chunks_to_mesh.resize(static_cast<std::size_t>(count));
            }
            std::unique_lock meshes_lock(data.meshes_mutex);
            if (data.meshes.size() > static_cast<std::size_t>(count)) {
                data.meshes.resize(static_cast<std::size_t>(count));
                ++data.version;
            }
        }

        for (auto& lods : plan_threads(static_cast<PosLodLevel>(count)))
            threads.emplace_back([this, lods = std::move(lods)](std::stop_token stop) { run_thread(stop, lods); });
    }

    void init() override {
        World::Module::init();
        try {
            generator = &world->getmod<WorldGen>();
        } catch (triolib::DoesNotExist&) {
            throw std::runtime_error("No world generator found!");
        }
        set_lod_count(lod_count);
    }

    inline bool loop_thread(PosLodLevel lod, RegistryList& reglist) {
        WorldLoader::ThreadLoader val(this, lod);
        if (!val) return false;

        std::optional<RawChunk> generated;
        if (val.mode != WorldLoader::Unload) {
            bool exists = false;
            {
                std::shared_lock levels_guard(world->levels_mutex);
                std::shared_lock level_guard(world->levels[lod].mutex);
                exists = world->levels[lod].chunks.contains(val.pos);
            }

            if (!exists) {
                generated = generator->generate_chunk({val.pos, lod}, reglist);
                generated->try_compress();
            }
        }

        auto& data = world->mesh_data;
        std::shared_lock levels_guard(world->levels_mutex);
        Level& level = world->levels[lod];
        std::unique_lock level_guard(level.mutex);

        if (generated) {
            level.chunks[val.pos] = std::move(*generated);

            for (size_t i = 0; i < CHUNK_NEIGHBOR_OFFSETS.size(); ++i) {
                const Vec3i& off = CHUNK_NEIGHBOR_OFFSETS[i];
                const Vec3i neighbor{val.pos.x + off.x, val.pos.y + off.y, val.pos.z + off.z};
                const auto nit = level.chunks.find(neighbor);
                if (nit == level.chunks.end() || !nit->second.rendered) continue;
                if (nit->second.mesh_missing_neighbors.load() & (1u << (i ^ 1))) data.ensure_mesh(neighbor, lod);
            }
        }

        const auto it = level.chunks.find(val.pos);
        switch (val.mode) {
            case WorldLoader::Load:
                return true;
            case WorldLoader::Unload:
                level.chunks.erase(val.pos);
                data.drop_mesh(val.pos, lod);
                return true;
            case WorldLoader::Render:
                if (it == level.chunks.end()) return true;
                it->second.rendered = true;
                data.ensure_mesh(val.pos, lod);
                return true;
            case WorldLoader::NotRender:
                if (it != level.chunks.end()) it->second.rendered = false;
                data.drop_mesh(val.pos, lod);
                return true;
        }
        return false;
    }

    /// One loader thread. With several LODs it polls them round-robin, starting after the LOD it served last,
    /// so every LOD that has work gets an equal share of this thread's time.
    virtual void run_thread(std::stop_token stop, const std::vector<PosLodLevel>& lods) {
        RegistryList& reglist = parent->application->get_subapp<RegistryList>();
        if (lods.empty()) return;

        std::size_t cursor = 0;
        while (!stop.stop_requested()) {
            bool worked = false;
            for (std::size_t i = 0; i < lods.size() && !stop.stop_requested(); ++i) {
                const std::size_t index = (cursor + i) % lods.size();
                if (!loop_thread(lods[index], reglist)) continue;
                cursor = (index + 1) % lods.size();
                worked = true;
                break;
            }
            if (!worked) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    void loop() override {}

    void quit() override {
        running = false;
        threads.clear();
    }

} REGISTER_MOD(WorldLoaderImpl, World);
