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
#include <unordered_map>
#include <unordered_set>
#include <world/client/main.hpp>

struct BlockVertex {
    uint16_t x, y, z;
    uint16_t lighting;
    /// @brief Texture coordinates **inside the block's atlas tile**, in the range `[0, 1]`.
    float u, v;
    /// @brief How many times the atlas tile is repeated across this face, per axis.
    /// @detail A chunk at LOD `n` draws its blocks `scale` times larger in world space,
    /// so the tile is repeated `scale` times to keep the texture density constant.
    /// At most 255 (the LOD scales are 1, 2, 4, 8, ...).
    uint8_t texture_repeat_x = 1, texture_repeat_y = 1;
    int8_t normal_yaw, normal_pitch, normal_roll;
};

struct ChunkMesh {
    std::vector<BlockVertex> vertices;
    std::any renderer_data;
    /// @brief Incremented whenever this mesh is (re)built, so renderers can cache GPU buffers.
    uint64_t revision = 0;
};

/// @brief A module that holds the chunks to mesh.
struct WorldMeshHandler : WorldClient::ThreadedModule {
    tClass;
    /// @brief Lock = update chunk mesh or mesh chunk
    std::mutex chunks_to_mesh_mutex;
    std::list<ChunkIndex> chunks_to_mesh;
    /// @brief Mirrors \ref chunks_to_mesh so queueing is O(1) instead of O(n).
    std::unordered_set<ChunkIndex> chunks_to_mesh_pending;

    /// @brief Shared lock = rendering, unique lock = add chunk mesh
    std::shared_mutex chunk_meshes_mutex;
    std::unordered_map<ChunkIndex, ChunkMesh> chunk_meshes;
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
