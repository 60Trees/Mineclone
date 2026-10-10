#include "world.hpp"

#include <base/resources.hpp>
#include <registry/registry.hpp>

struct ChunkMesherImpl : ChunkMesher {
    tImplClass(ChunkMesher);

    void mesh(Vec3i pos, UnlockedChunk& chunk) override {
        chunk.mesh.clear();


        chunk.needs_remesh = false;
    }
} REGISTER_MOD(ChunkMesherImpl, World);

