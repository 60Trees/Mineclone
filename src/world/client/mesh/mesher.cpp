/**
 * @file world/client/mesh/mesher.cpp
 * @brief The chunk mesher implementation
 * @author 60Trees_ (github.com/60Trees)
 * @detail
 * This file is responsible for building
 * the mesh for a chunk, given the chunk and its neighbors.
 */

#include "main.hpp"
#include <chrono>
#include <stdexcept>

using namespace std;

// The actual chunk mesher. Should not deal with any thread shenadigans,
// it is already passed in via the parameters
struct WorldMesherImpl : WorldMesher {
    tImplClass(WorldMesher);

    std::mutex speed_mutex;
    chrono::steady_clock::time_point speed_window_start = chrono::steady_clock::now();
    size_t completed_in_window = 0;

    ChunkMesh mesh_chunk(
        ChunkIndex ownpos, shared_ptr<const Chunk> own, const array<shared_ptr<Chunk>, 6>& neighbors) override {
        if (!own) return {};

        vector<BlockVertex> mesh;
        // Rough upper bound for a terrain surface; the vector grows if a chunk has more faces.
        mesh.reserve(own->size() * own->size() * 6);
        const auto width = static_cast<int>(own->size());

        // A block at LOD n is drawn `scale` times larger, so its face covers `scale`
        // texture tiles in each direction. Keep the texture density identical at every
        // LOD by repeating the tile once per world unit.
        const auto lod_scale = host->lod_descriptors.at(ownpos.lod).scale;
        if (lod_scale < 1 || lod_scale > 255)
            throw std::runtime_error("LOD scale does not fit in the 8-bit texture repeat field");
        const auto repeat = static_cast<uint8_t>(lod_scale);

        const auto add_face = [&](array<array<uint16_t, 3>, 4> positions, uint16_t lighting, int8_t yaw, int8_t pitch,
                                  int8_t roll) {
            constexpr array<unsigned, 6> indices{0, 1, 2, 0, 2, 3};
            constexpr array<array<uint16_t, 2>, 4> uv{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
            for (const auto i : indices) {
                const auto& position = positions[i];
                mesh.push_back({position[0], position[1], position[2], lighting, static_cast<float>(uv[i][0]),
                    static_cast<float>(uv[i][1]), repeat, repeat, yaw, pitch, roll});
            }
        };

        const auto visible = [&](int x, int y, int z, int nx, int ny, int nz) {
            const Chunk* chunk = own.get();
            if (nx < 0)
                chunk = neighbors[5].get();
            else if (nx >= width)
                chunk = neighbors[4].get();
            else if (ny < 0)
                chunk = neighbors[1].get();
            else if (ny >= width)
                chunk = neighbors[0].get();
            else if (nz < 0)
                chunk = neighbors[3].get();
            else if (nz >= width)
                chunk = neighbors[2].get();
            if (!chunk) return true;

            const auto local = [width](int coordinate) {
                if (coordinate < 0) return coordinate + width;
                if (coordinate >= width) return coordinate - width;
                return coordinate;
            };
            const Tile tile = chunk->at(local(nx), local(ny), local(nz));
            return !tile || tile.id == 0;
        };

        for (int x = 0; x < width; ++x)
            for (int y = 0; y < width; ++y)
                for (int z = 0; z < width; ++z) {
                    if (!own->at(x, y, z)) continue;
                    const auto x0 = static_cast<uint16_t>(x);
                    const auto x1 = static_cast<uint16_t>(x + 1);
                    const auto y0 = static_cast<uint16_t>(y);
                    const auto y1 = static_cast<uint16_t>(y + 1);
                    const auto z0 = static_cast<uint16_t>(z);
                    const auto z1 = static_cast<uint16_t>(z + 1);

                    if (visible(x, y, z, x, y - 1, z))
                        add_face({{{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}}}, 36000, 0, -1, 0);
                    if (visible(x, y, z, x, y + 1, z))
                        add_face({{{x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {x0, y1, z0}}}, 65535, 0, 1, 0);
                    if (visible(x, y, z, x + 1, y, z))
                        add_face({{{x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}}}, 52000, 0, 0, -1);
                    if (visible(x, y, z, x - 1, y, z))
                        add_face({{{x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}}}, 48000, 0, 0, 1);
                    if (visible(x, y, z, x, y, z - 1))
                        add_face({{{x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}}}, 44000, -1, 0, 0);
                    if (visible(x, y, z, x, y, z + 1))
                        add_face({{{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}}}, 40000, 1, 0, 0);
                }

        {
            std::lock_guard lock(speed_mutex);
            completed_in_window++;

            const auto now = chrono::steady_clock::now();
            const auto elapsed = chrono::duration<double>(now - speed_window_start).count();
            if (elapsed >= 1.0) {
                console.debug << "Mesher throughput (chunks per second):\n"
                              << static_cast<double>(completed_in_window) / elapsed
                              << "\ntrials=" << completed_in_window;
                completed_in_window = 0;
                speed_window_start = now;
            }
        }

        return {std::move(mesh), {}};
    }
} REGISTER_MOD(WorldMesherImpl, WorldClient);
