#pragma once

#include <SDL3/SDL_stdinc.h>
#include <world/world.hpp>
#include <functional>
#include <atomic>
#include <world/worldgen.hpp>

struct WorldLoader : World::Module {
    struct ThreadLoader {
        Vec3i pos;
        bool has_work;
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
            pos = *work;
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
    std::function<std::optional<Vec3i>(PosLodLevel)> get_next_work;
    /// @brief Threads should call this when their work is finished.
    /// @note This function is called from multiple threads with no locks.
    std::function<void(Vec3i, PosLodLevel)> finish_work;
    /// @brief The number of threads to use for each LOD level. Last element is used for all higher LOD levels.
    /// @note Must not be empty.
    /// @detail If the LOD level is greater than the size
    /// of this vector, then the last element will be used for all higher LOD levels.
    /// E.g {4, 2, 1} means 4 threads for LOD 0, 2 threads for LOD 1, and 1 thread for LOD 2 and above.
    std::vector<uint> threads_per_lod;

    inline uint get_thread_count(PosLodLevel lod) const {
        if (lod < threads_per_lod.size()) return threads_per_lod[lod];
        return threads_per_lod.back();
    }

    WorldGen* generator = nullptr;
};
