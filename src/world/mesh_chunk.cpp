#include "world.hpp"

#include <algorithm>
#include <base/resources.hpp>
#include <registry/registry.hpp>
#include <world/blocks/block.hpp>

struct ChunkMesherImpl : ChunkMesher {
    tImplClass(ChunkMesher);

    std::vector<BlockVertex> mesh(
        World::ChunkIndex i, const RawChunk& chunk, const std::function<Tile(int, int, int)>& sample) override {
        if (i.pos.x < 0 || i.pos.y < 0 || i.pos.z < 0)
            run_once console.log << "Meshing chunk at negative coordinates: " << i.pos.x << ", " << i.pos.y << ", "
                                 << i.pos.z;
        const auto* atlas = parent->application->getmod<ResourceHandler>()
                                .resources.get_atlas("assets/minecraft/textures/block");
        if (!atlas) throw std::runtime_error("Block texture atlas is not loaded");
        const auto& texture_rect = (*atlas)["assets/minecraft/textures/block/end_stone.png"];
        const uint16_t u0 = static_cast<uint16_t>(std::min(texture_rect.first.x, static_cast<unsigned int>(UINT16_MAX)));
        const uint16_t v0 = static_cast<uint16_t>(std::min(texture_rect.first.y, static_cast<unsigned int>(UINT16_MAX)));
        const uint16_t u1 = static_cast<uint16_t>(std::min(texture_rect.second.x, static_cast<unsigned int>(UINT16_MAX)));
        const uint16_t v1 = static_cast<uint16_t>(std::min(texture_rect.second.y, static_cast<unsigned int>(UINT16_MAX)));
        const auto air = parent->application->get_subapp<RegistryList>().getmod<BlockRegistry>()
                             .get_index("minecraft:air");

        std::vector<BlockVertex> mesh;
        mesh.reserve(CHUNK_BLOCK_COUNT * 36);

        const auto add_face = [&](std::array<std::array<uint16_t, 3>, 4> positions, uint16_t lighting, int8_t yaw,
                                  int8_t pitch, int8_t roll) {
            constexpr std::array<unsigned, 6> indices{0, 1, 2, 0, 2, 3};
            constexpr std::array<std::array<uint16_t, 2>, 4> uv{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
            for (const auto index : indices) {
                const auto vertex = positions[index];
                mesh.push_back({
                    vertex[0], vertex[1], vertex[2], lighting,
                    static_cast<uint16_t>(uv[index][0] ? u1 : u0),
                    static_cast<uint16_t>(uv[index][1] ? v1 : v0),
                    yaw, pitch, roll,
                });
            }
        };

        const auto greedy_faces = [&](int axis, bool positive, uint16_t lighting, int8_t yaw, int8_t pitch,
                                      int8_t roll) {
            for (size_t slice = 0; slice < CHUNK_SIZE; ++slice) {
                std::array<std::array<bool, CHUNK_SIZE>, CHUNK_SIZE> mask{};
                for (size_t a = 0; a < CHUNK_SIZE; ++a)
                    for (size_t b = 0; b < CHUNK_SIZE; ++b) {
                        size_t x = axis == 0 ? slice : (axis == 1 ? a : a);
                        size_t y = axis == 1 ? slice : (axis == 0 ? a : b);
                        size_t z = axis == 2 ? slice : b;
                        const Tile tile = sample(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z));
                        if (!tile || tile.id == air) continue;
                        int nx = static_cast<int>(x), ny = static_cast<int>(y), nz = static_cast<int>(z);
                        (axis == 0 ? nx : axis == 1 ? ny : nz) += positive ? 1 : -1;
                        const Tile neighbor = sample(nx, ny, nz);
                        mask[a][b] = !neighbor || neighbor.id == air;
                    }

                for (size_t a = 0; a < CHUNK_SIZE; ++a)
                    for (size_t b = 0; b < CHUNK_SIZE; ++b) {
                        if (!mask[a][b]) continue;
                        size_t width = 1;
                        while (a + width < CHUNK_SIZE && mask[a + width][b]) ++width;
                        size_t height = 1;
                        while (b + height < CHUNK_SIZE) {
                            bool row = true;
                            for (size_t i = 0; i < width; ++i) row &= mask[a + i][b + height];
                            if (!row) break;
                            ++height;
                        }
                        for (size_t i = 0; i < width; ++i)
                            for (size_t j = 0; j < height; ++j) mask[a + i][b + j] = false;

                        const uint16_t p = static_cast<uint16_t>(slice * CHUNK_SIZE + (positive ? CHUNK_SIZE : 0));
                        const uint16_t a0 = static_cast<uint16_t>(a * CHUNK_SIZE);
                        const uint16_t a1 = static_cast<uint16_t>((a + width) * CHUNK_SIZE);
                        const uint16_t b0 = static_cast<uint16_t>(b * CHUNK_SIZE);
                        const uint16_t b1 = static_cast<uint16_t>((b + height) * CHUNK_SIZE);
                        if (axis == 0)
                            add_face(positive ? std::array<std::array<uint16_t, 3>, 4>{{{p, a0, b1}, {p, a0, b0}, {p, a1, b0}, {p, a1, b1}}}
                                             : std::array<std::array<uint16_t, 3>, 4>{{{p, a0, b0}, {p, a0, b1}, {p, a1, b1}, {p, a1, b0}}},
                                lighting, yaw, pitch, roll);
                        else if (axis == 1)
                            add_face(positive ? std::array<std::array<uint16_t, 3>, 4>{{{a0, p, b1}, {a1, p, b1}, {a1, p, b0}, {a0, p, b0}}}
                                             : std::array<std::array<uint16_t, 3>, 4>{{{a0, p, b0}, {a1, p, b0}, {a1, p, b1}, {a0, p, b1}}},
                                lighting, yaw, pitch, roll);
                        else
                            add_face(positive ? std::array<std::array<uint16_t, 3>, 4>{{{a1, b0, p}, {a0, b0, p}, {a0, b1, p}, {a1, b1, p}}}
                                             : std::array<std::array<uint16_t, 3>, 4>{{{a0, b0, p}, {a1, b0, p}, {a1, b1, p}, {a0, b1, p}}},
                                lighting, yaw, pitch, roll);
                    }
            }
        };

        greedy_faces(1, false, 36000, 0, -1, 0);
        greedy_faces(1, true, 65535, 0, 1, 0);
        greedy_faces(0, true, 52000, 0, 0, -1);
        greedy_faces(0, false, 48000, 0, 0, 1);
        greedy_faces(2, false, 44000, -1, 0, 0);
        greedy_faces(2, true, 40000, 1, 0, 0);

        return mesh;
    }
} REGISTER_MOD(ChunkMesherImpl, World);
