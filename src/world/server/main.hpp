/**
 * @file world/server/main.hpp
 * @brief The server interface of the world
 * @author 60Trees_ (github.com/60Trees)
 * @detail
 * This file is responsible for defining the interface
 * for the server side implementation.
 */

#pragma once

#include <world/shared.hpp>

struct WorldServer : triolib::SubApp {
    tClass;

    /// @brief Gets a chunk from the server. Can be called from any thread
    /// @note Hangs the calling thread until the chunk is available.
    /// Can return nullptr if the server is not bothered to send it
    virtual std::shared_ptr<Chunk> request_chunk(ChunkIndex index) = 0;

    std::vector<LodDescriptor> lod_descriptors;
};
