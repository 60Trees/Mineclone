/**
 * @file world/worldloader.cpp
 * @author 60Trees_ (github.com/60Trees)
 *
 * @detail This file handles the loading of chunks in the world.
 * It uses a thread pool to load or generate chunks in the background.
 */

#include "worldloader.hpp"
#include <chrono>
#include <optional>
#include <set>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <vector>
#include <world/worldgen.hpp>
#include <unordered_map>
#include "triolib.hpp"
#include "world/world.hpp"

struct WorldLoaderImpl : WorldLoader {
    tImplClass(WorldLoader);

    static constexpr int load_radius = 4;
    std::unordered_map<PosLodLevel, std::set<Vec3i>> in_progress_chunks;
    std::mutex in_progress_mutex;

    WorldLoaderImpl() {
        threads_per_lod = {2, 2, 1};
        get_next_work = [&](PosLodLevel lod) -> std::optional<Vec3i> {
            // threads takin a chill pill
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            World& w = *world;

            std::shared_lock __(w.levels_mutex);
            if (lod >= w.levels.size()) return std::nullopt;

            Level& lvl = w.levels[lod];
            std::shared_lock __(lvl.mutex);

            Vec3i cam_chunk = w.camchunk(lod);
            const int lod_radius = load_radius;

            for (int radius = 0; radius <= lod_radius; ++radius)
                for (int x = -radius; x <= radius; ++x)
                    for (int z = -radius; z <= radius; ++z)
                        for (int y = -radius; y <= radius; ++y) {
                            // Only inspect the perimeter of this shell.
                            if (radius > 0 && std::abs(x) != radius && std::abs(z) != radius) continue;

                            Vec3i pos{cam_chunk.x + x, cam_chunk.y + y, cam_chunk.z + z};

                            if (lvl.chunks.contains(pos)) continue;

                            std::lock_guard __(in_progress_mutex);

                            if (!in_progress_chunks.contains(lod)) in_progress_chunks[lod] = {};
                            if (!in_progress_chunks[lod].contains(pos)) {
                                in_progress_chunks[lod].insert(pos);
                                if (pos.x < 0 || pos.y < 0 || pos.z < 0)
                                    run_once console.log << "Suggesting work for chunk at LOD=" << lod << " with negative coordinates: " << pos.x << ", "
                                                         << pos.y << ", " << pos.z;
                                return pos;
                            }
                        }
            return std::nullopt;
        };
        finish_work = [&](Vec3i pos, PosLodLevel lod) {
            std::lock_guard lock(in_progress_mutex);
            tassert(in_progress_chunks[lod].contains(pos));
            in_progress_chunks[lod].erase(pos);
        };
    }

    // @brief Number of LOD levels. Change it with set_lod_count().
    int lod_count = 3;

    // @brief Loader threads, indexed by LOD level and thread index
    std::vector<std::vector<std::jthread>> thread_array;

    // @brief Resizes the LOD arrays
    virtual void set_lod_count(int count) {
        lod_count = count;
        {
            std::unique_lock levels_lock(world->levels_mutex);
            world->levels.resize(static_cast<std::size_t>(count));
        }
        thread_array.resize(count);

        for (int lod = 0; lod < count; ++lod) {
            if (!thread_array[lod].empty()) continue;

            auto thread_count = get_thread_count(lod);
            for (uint i = 0; i < thread_count; ++i)
                thread_array[lod].emplace_back([this, lod](std::stop_token stop) { run_thread(stop, lod); });
        }
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

        if (val.pos.x < 0 || val.pos.y < 0 || val.pos.z < 0)
            run_once console.log << "Loading chunk at LOD=" << lod << " with negative coordinates: " << val.pos.x << ", "
                                 << val.pos.y << ", " << val.pos.z;

        auto generated = generator->generate_chunk({val.pos, lod}, reglist);
        generated.try_compress();

        std::shared_lock __(world->levels_mutex);
        std::unique_lock level_lock(world->levels[lod].mutex);
        world->levels[lod].chunks[val.pos] = std::move(generated);
        world->mesh_data.needs_mesh(val.pos, lod, world->levels[lod].chunks.at(val.pos));
        return true;
    }

    virtual void run_thread(std::stop_token stop, PosLodLevel lod) {
        RegistryList& reglist = parent->application->get_subapp<RegistryList>();

        while (!stop.stop_requested())
            if (!loop_thread(lod, reglist)) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    void loop() override {}

    void quit() override {
        running = false;
        thread_array.clear();
    }

} REGISTER_MOD(WorldLoaderImpl, World);
