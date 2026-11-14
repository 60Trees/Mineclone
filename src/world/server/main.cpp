/**
 * @file world/server/main.cpp
 * @brief The server implementation of the world
 * @author 60Trees_ (github.com/60Trees)
 * @detail
 * The server implementation of the world is responsible
 * for generating chunks and sending them to the client.
 * This code can be swapped out for a singleplayer world
 * or something that sends packets to a server and stuff
 */

#include "main.hpp"

#include <world/server/worldgen/main.hpp>
#include <triolib.hpp>

using namespace std;

struct TestServerImpl : WorldServer {
    tImplClass(WorldServer);
    shared_ptr<Chunk> request_chunk(ChunkIndex index) override {
        if (index.lod >= lod_descriptors.size()) return nullptr;
        auto chunk = make_shared<Chunk>(getmod<WorldGen>().generate_chunk(index));
        return chunk;
    }

    void init() override {
        lod_descriptors = {{*default_chunk_size, 1}, {*default_chunk_size / 2, 2}, {*default_chunk_size / 4, 4},
            {*default_chunk_size / 8, 8}};
        triolib::SubApp::init();
    }
} REGISTER_SUBAPP(TestServerImpl);
