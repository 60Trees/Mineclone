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
    /**
     * @brief Get a list of all chunks (higher lod only) that the given index overlaps.
     * @detail For example: This chunk is lod=1 and is twice as physically large as lod=0 chunks, so it will return 8
     * lod=0 chunks that it overlaps. If it is lod=0, it will return an empty vector.
     * @note Because everything has to be a power of 2, it is physically impossible for a chunk to half overlap some
     * other chunk.
     * Also, it will not return any chunk not currently loaded.
     */
    virtual std::vector<ChunkIndex> get_overlapping_big(ChunkIndex index, bool locks = true) = 0;

    /**
     * @brief Get a list of all chunks (lower lod only) that the given index overlaps.
     * @detail For example: This chunk is lod=1 and there is a lod=2 and an lod=3 chunk that it overlaps, so it will
     * return those two chunks.
     * @note It will not return any chunk not currently loaded.
     */
    virtual std::vector<ChunkIndex> get_overlapping_small(ChunkIndex index, bool locks = true) = 0;

    /**
     * @brief Get a list of all chunks (this lod only) that the given index's bigger overlapping smaller overlaps.
     * Contains the input chunk as well
     * @detail This is confusing as shit. Logic terms: [The smallest of \ref get_overlapping_big piped into the
     * \ref get_overlapping_small] filtered by this lod only.
     * Here's an example: This chunk is lod=1 and in order to render, every lod=1 chunk that is in the shadow of
     * this one lod=2 chunk needs to exist, so this is all the chunks there.
     * Still don't get it? Too bad
     */
    virtual std::vector<ChunkIndex> get_overlapping_neighbors(ChunkIndex index, bool locks = true) = 0;

    virtual bool should_render_chunk(ChunkIndex index, bool locks = true) = 0;

    /// @note The chunks are shared pointers so that, e.g if it is a singleplayer world then the server and client will
    /// have the same chunks, no need for storing two copies of the same world
    std::map<ChunkIndex, std::shared_ptr<Chunk>> chunks;
    /// @brief Shared lock = iterating / referencing, unique lock = insert chunk / remove chunk
    std::shared_mutex chunks_mutex;

    /// @brief How many levels of detail (LOD) are supported by this world
    /// @note Must not be `0`
    size_t lod_count;

    WorldServer* host = nullptr;

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
};
