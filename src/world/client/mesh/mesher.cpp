/**
 * @file world/client/mesh/mesher.cpp
 * @brief The chunk mesher implementation
 * @author 60Trees_ (github.com/60Trees)
 * @detail
 * This file is responsible for building
 * the mesh for a chunk, given the chunk and its neighbors.
 */

#include "main.hpp"
#include <base/resources.hpp>
#include <chrono>
#include <stdexcept>

using namespace std;

// The actual chunk mesher. Should not deal with any thread shenadigans,
// it is already passed in via the parameters
struct WorldMesherImpl : WorldMesher {
    tImplClass(WorldMesher);
    float texture_u0 = 0.0f;
    float texture_v0 = 0.0f;
    float texture_u1 = 1.0f;
    float texture_v1 = 1.0f;

    void init() override {
        auto* atlas = parent->application->getmod<ResourceHandler>().resources.get_atlas("assets/textures/block");
        if (!atlas) throw std::runtime_error("Missing block texture atlas");
        const auto texture = atlas->locations.find("minecraft/textures/block/stone.png");
        if (texture == atlas->locations.end()) throw std::runtime_error("Missing stone texture in block atlas");
        const auto& rect = texture->second;
        texture_u0 = static_cast<float>(rect.first.x) / static_cast<float>(atlas->image.width);
        texture_v0 = static_cast<float>(rect.first.y) / static_cast<float>(atlas->image.height);
        texture_u1 = static_cast<float>(rect.second.x) / static_cast<float>(atlas->image.width);
        texture_v1 = static_cast<float>(rect.second.y) / static_cast<float>(atlas->image.height);
    }

    std::mutex speed_mutex;
    unsigned long long average_over_second;
    size_t average_amount = 0;

    ChunkMesh mesh_chunk(
        ChunkIndex ownpos, shared_ptr<const Chunk> own, const array<shared_ptr<Chunk>, 6>& neighbors) override {
        if (!own) return {};
        auto start = std::chrono::high_resolution_clock::now();

        vector<BlockVertex> mesh;
        const auto width = static_cast<int>(own->size());
        const auto add_face = [&](array<array<uint16_t, 3>, 4> positions, uint16_t lighting, int8_t yaw, int8_t pitch,
                                  int8_t roll) {
            constexpr array<unsigned, 6> indices{0, 1, 2, 0, 2, 3};
            constexpr array<array<uint16_t, 2>, 4> uv{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
            for (const auto i : indices) {
                const auto& position = positions[i];
                mesh.push_back(
                    {position[0], position[1], position[2], lighting, texture_u0 + uv[i][0] * (texture_u1 - texture_u0),
                        texture_v0 + uv[i][1] * (texture_v1 - texture_v0), yaw, pitch, roll});
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

        auto end = std::chrono::high_resolution_clock::now();

        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

        std::lock_guard __(speed_mutex);

        average_over_second += duration;
        average_amount++;

        run_limited(1) {
            if (average_over_second == 0) {
                average_amount = 0;
                return {std::move(mesh), {}};
            }

            double chunks_per_second = (1'000'000.0 * average_amount) / average_over_second;

            console.debug << "Worldgen average speed (chunks per second):\n"
                          << chunks_per_second << "\ntrials=" << average_amount;

            average_over_second = 0;
            average_amount = 0;
        }

        return {std::move(mesh), {}};
    }
} REGISTER_MOD(WorldMesherImpl, WorldClient);
