#pragma once

#include <SDL3/SDL_stdinc.h>
#include <world/world.hpp>
#include <functional>
#include <atomic>
#include <world/worldgen.hpp>

struct WorldLoader : World::Module {
    enum WorkMode { Load, Unload, Render, NotRender };
    struct ThreadLoader {
        Vec3i pos;
        bool has_work;
        WorkMode mode;
        inline operator bool() { return has_work; }
        WorldLoader* loader = nullptr;
        PosLodLevel lod;
        inline ThreadLoader(WorldLoader* _loader, PosLodLevel _lod) {
            tassert(_loader);
            loader = _loader;
            lod = _lod;
            auto work = loader->get_next_work(lod);
            has_work = work.has_value();
            if (!has_work) return;
            pos = work->first;
            mode = work->second;
        }
        inline ~ThreadLoader() {
            if (has_work) loader->finish_work(pos, lod);
        }
    };

    std::atomic_bool running = true;

    /// @brief Returns the next chunk position to load, given the LOD level.
    /// @note This function is called from multiple threads with no locks.
    /// It should properly handle edge cases where two threads call it at the
    /// same time, in order to avoid loading the same chunk twice.
    std::function<std::optional<std::pair<Vec3i, WorkMode>>(PosLodLevel)> get_next_work;
    /// @brief Threads should call this when their work is finished.
    /// @note This function is called from multiple threads with no locks.
    std::function<void(Vec3i, PosLodLevel)> finish_work;
    /// @brief Non-negative lottery tickets used to choose the next LOD job.
    std::function<uint(PosLodLevel)> lod_priority = [](PosLodLevel) { return 1; };
    std::function<uint(PosLodLevel)> get_chunk_radius = [](PosLodLevel) { return 2; };

    WorldGen* generator = nullptr;
};
