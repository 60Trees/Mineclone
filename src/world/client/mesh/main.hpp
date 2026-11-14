/**
 * @file world/client/mesh/main.hpp
 * @brief Provides an interface for chunk meshing
 * @author 60Trees_ (github.com/60Trees)
 * @detail
 * This file has general mesh stuff. E.g schedule
 * a chunk for meshing, get a mesh, it has the main
 * BlockVertex struct, etc.
 */

#pragma once

#include <list>
#include <world/client/main.hpp>

struct BlockVertex {
    uint16_t x, y, z;
    uint16_t lighting;
    float u, v;
    int8_t normal_yaw, normal_pitch, normal_roll;
};

struct ChunkMesh {
    std::vector<BlockVertex> vertices;
    std::any renderer_data;
};

/// @brief A module that holds the chunks to mesh.
struct WorldMeshHandler : WorldClient::ThreadedModule {
    tClass;
    /// @brief Lock = update chunk mesh or mesh chunk
    std::mutex chunks_to_mesh_mutex;
    std::list<ChunkIndex> chunks_to_mesh;

    /// @brief Shared lock = rendering, unique lock = add chunk mesh
    std::shared_mutex chunk_meshes_mutex;
    std::map<ChunkIndex, ChunkMesh> chunk_meshes;
    std::mutex meshing_mutex;
};

/// @brief A module that holds the meshing function
struct WorldMesher : WorldClient::Module {
    tClass;
    /**
     * @brief Meshes a chunk.
     * @param own The chunk to mesh.
     * @param neighbours The neighbouring chunks, in the order of (up +Y, down -Y, north +Z, south -Z,
     * east +X, west -X).
     */
    virtual ChunkMesh mesh_chunk(
        ChunkIndex pos, std::shared_ptr<const Chunk> own, const std::array<std::shared_ptr<Chunk>, 6>& neighbours) = 0;
};
