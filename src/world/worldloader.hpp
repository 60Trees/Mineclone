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
    /// @brief How many threads serve a LOD level. Signed:
    ///  * `> 0`  : that many dedicated threads for this LOD.
    ///  * `< 0`  : LODs share threads, `-N` = one thread serves N LODs (e.g. -4: one thread for four LODs).
    ///  * `== 0` : undefined, treated as 1 dedicated thread.
    /// @note Called once per LOD every time the thread layout is (re)built, so it should be cheap and pure.
    /// @detail Consecutive LODs that return the same negative value form one run. A run of R LODs at `-N` gets
    /// ceil(R / N) threads and the LODs are split between them as evenly as possible (sizes differ by at most 1,
    /// never above N). E.g. 16 LODs at -5 -> 4 threads x 4 LODs, instead of 5+5+5+1.
    /// A thread that serves several LODs polls them round-robin, so a busy LOD can not starve its neighbours.
    std::function<int(PosLodLevel)> threads_per_lod = [](PosLodLevel) { return 1; };

    std::function<uint(PosLodLevel)> get_chunk_radius = [](PosLodLevel) { return 3; };

    /// @brief Resolves `threads_per_lod` into one entry per OS thread: the LODs that thread serves.
    inline std::vector<std::vector<PosLodLevel>> plan_threads(PosLodLevel lod_count) const {
        std::vector<std::vector<PosLodLevel>> plan;
        const auto query = [&](PosLodLevel lod) { return threads_per_lod ? threads_per_lod(lod) : 1; };

        PosLodLevel lod = 0;
        while (lod < lod_count) {
            const int value = query(lod);

            if (value >= 0) {
                const int count = value == 0 ? 1 : value;
                for (int i = 0; i < count; ++i) plan.push_back({lod});
                ++lod;
                continue;
            }

            const auto per_thread = static_cast<PosLodLevel>(-static_cast<long long>(value));
            PosLodLevel end = lod + 1;
            while (end < lod_count && query(end) == value) ++end;

            const PosLodLevel run = end - lod;
            const PosLodLevel groups = (run + per_thread - 1) / per_thread;
            const PosLodLevel base = run / groups, extra = run % groups;

            PosLodLevel next = lod;
            for (PosLodLevel g = 0; g < groups; ++g) {
                std::vector<PosLodLevel> lods;
                for (PosLodLevel i = 0, size = base + (g < extra ? 1 : 0); i < size; ++i) lods.push_back(next++);
                plan.push_back(std::move(lods));
            }
            lod = end;
        }
        return plan;
    }

    WorldGen* generator = nullptr;
};
