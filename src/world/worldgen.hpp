#pragma once

#include <world/world.hpp>

struct WorldGen : World::Module {
    tClass;
    /**
     * @note Called multithreaded, locks are handled by the caller
     */
    virtual RawChunk generate_chunk(World::ChunkIndex index, RegistryList& reglist) = 0;
    virtual BigInt get_approx_height_at(BigInt x, BigInt z) = 0;

    void init() override { World::Module::init(); }
    void loop() override {}
    void quit() override {}
};

