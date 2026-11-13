/**
 * @file world/client/mesh/mesher.cpp
 * @brief The chunk mesher implementation
 * @author 60Trees_ (github.com/60Trees)
 * @detail
 * This file is responsible for building
 * the mesh for a chunk, given the chunk and its neighbors.
 */

#include "main.hpp"

using namespace std;

// The actual chunk mesher. Should not deal with any thread shenadigans,
// it is already passed in via the parameters
struct WorldMesherImpl : WorldMesher {
    tImplClass(WorldMesher);
    ChunkMesh mesh_chunk(
        ChunkIndex ownpos, shared_ptr<const Chunk> own, const array<shared_ptr<Chunk>, 6>& neighbors) override {
        // TODO: (needed) Add chunk meshing

        return ChunkMesh{};
    }
} REGISTER_MOD(WorldMesherImpl, WorldClient);
