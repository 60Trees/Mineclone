/**
 * @file world/client/main.hpp
 * @brief The client side of the world, interface.
 * @author 60Trees_ (github.com/60Trees)
 * @detail
 * The client side of the world is responsible
 * for handling its own chunks, deciding what
 * chunks to render, etc.
 */

#pragma once

#include <memory>
#include <unordered_map>
#include <world/client/stats.hpp>
#include <world/shared.hpp>
#include <world/server/main.hpp>
#include <threads/task.hpp>

/**
 * @brief The world client.
 * @note Requires WorldServer to be loaded on the main app
 * by the time it is initialized
 */
struct WorldClient : triolib::SubApp {
    tClass;
    /// @brief True if the chunk exists and is flagged as renderable. This does **not**
    /// apply the LOD parent/child selection rules; see the renderer for those.
    /// @note Pass \p locks = false when the caller already holds \ref chunks_mutex.
    virtual bool should_render_chunk(ChunkIndex index, bool locks = true) = 0;

    /// @brief Horizontal/vertical radius, in chunks, for a given LOD.
    /// Populated from options by the loader at startup.
    Pair<size_t> view_distance(LodLevel lod) const {
        if (lod < lod_view_distance.size()) return lod_view_distance[lod];
        return {8, 4};
    }

    /// @note The chunks are shared pointers so that, e.g if it is a singleplayer world then the server and client will
    /// have the same chunks, no need for storing two copies of the same world
    std::unordered_map<ChunkIndex, std::shared_ptr<Chunk>> chunks;
    /// @brief Shared lock = iterating / referencing, unique lock = insert chunk / remove chunk
    std::shared_mutex chunks_mutex;

    /// @brief How many levels of detail (LOD) are supported by this world
    /// @note Must not be `0`
    size_t lod_count;

    /// @brief Load radius per LOD, indexed by LOD. See \ref view_distance.
    std::vector<Pair<size_t>> lod_view_distance;

    WorldServer* host = nullptr;

    /// @brief Benchmarks for the chunk pipeline. See \ref ChunkLoadStats.
    ChunkLoadStats stats;

    struct Module : triolib::Module {
        tClass;
        WorldClient* client = nullptr;
        WorldServer* host = nullptr;
        void init() override {
            client = dynamic_cast<WorldClient*>(parent);
            host = client->host;
        }
        void loop() override {}
        void quit() override {}
    };
    struct ThreadedModule : triolib::Module {
        tClass;
        WorldClient* client = nullptr;
        WorldServer* host = nullptr;

        void init() override {
            client = dynamic_cast<WorldClient*>(parent);
            host = client->host;
            work_adder = std::make_unique<WorkAdder>(
                parent->application->get_subapp<ThreadPool>().main_list, [&](WorkList&) { return tick_thread(); },
                vname(), priority());
        }
        void loop() override final {}
        void quit() override {}

        virtual bool tick_thread() = 0;
        virtual uint priority() const { return 1; }

        private:
        std::unique_ptr<WorkAdder> work_adder;
    };

    virtual ChunkPos get_camera_pos(LodLevel lod = 0, bool locks = true) = 0;

    std::shared_mutex options_mutex;
    nlohmann::json options{};
};
