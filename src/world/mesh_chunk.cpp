#include "world.hpp"

#include <algorithm>
#include <base/resources.hpp>
#include <registry/registry.hpp>
#include <world/blocks/block.hpp>
#include <structures/assets/models/block.hpp>
#include <ui/debug_screen.hpp>

struct ChunkMesherImpl : ChunkMesher {
    tImplClass(ChunkMesher);

    uint chunks_done = 0;
    std::mutex chunks_done_mutex;

    std::vector<BlockVertex> mesh(World::ChunkIndex i, const RawChunk& chunk, const RawChunk* down, const RawChunk* up,
        const RawChunk* north, const RawChunk* south, const RawChunk* west, const RawChunk* east) override {
        {
            std::lock_guard __(chunks_done_mutex);
            chunks_done++;
            run_limited(1) {
                if (auto* debug_screen = parent->application->get_subapp_ptr<DebugScreen>())
                    debug_screen->show("Chunks meshed per second", std::to_string(chunks_done));
                chunks_done = 0;
            }
        }

        const auto* atlas =
            parent->application->getmod<ResourceHandler>().resources.get_atlas("assets/textures/block");
        if (!atlas) throw std::runtime_error("Block texture atlas is not loaded");

        // const auto& texture_rect = (*atlas)["assets/minecraft/textures/block/grass_block_side.png"];
        const auto& texture_rect = (*atlas)["assets/minecraft/textures/block/netherrack.png"];

        const uint16_t u0 =
            static_cast<uint16_t>(std::min(texture_rect.first.x, static_cast<unsigned int>(UINT16_MAX)));
        const uint16_t v0 =
            static_cast<uint16_t>(std::min(texture_rect.first.y, static_cast<unsigned int>(UINT16_MAX)));
        const uint16_t u1 =
            static_cast<uint16_t>(std::min(texture_rect.second.x, static_cast<unsigned int>(UINT16_MAX)));
        const uint16_t v1 =
            static_cast<uint16_t>(std::min(texture_rect.second.y, static_cast<unsigned int>(UINT16_MAX)));
        const uint16_t u_size = static_cast<uint16_t>(u1 - u0);
        const uint16_t v_size = static_cast<uint16_t>(v1 - v0);

        // A mesh block spans chunk_size real blocks, clamped to the vertex format's range.
        const auto chunk_scale = world->chunk_size(i.lod);
        const uint8_t repeat = chunk_scale >= 255 ? static_cast<uint8_t>(255) : static_cast<uint8_t>(chunk_scale);

        const auto air =
            parent->application->get_subapp<RegistryList>().getmod<BlockRegistry>().get_index("minecraft:air");

        // Sky / empty chunks are a single air tile: skip the 4096-block scan entirely.
        if (!chunk.array_chunk && (!chunk.single_tile || chunk.single_tile.id == air)) return {};

        std::vector<BlockVertex> mesh;
        mesh.reserve(2048);

        const auto add_face = [&](std::array<std::array<uint16_t, 3>, 4> positions, uint16_t lighting, int8_t yaw,
                                  int8_t pitch, int8_t roll) {
            constexpr std::array<unsigned, 6> indices{0, 1, 2, 0, 2, 3};
            constexpr std::array<std::array<uint16_t, 2>, 4> uv{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};

            for (const auto index : indices) {
                const auto vertex = positions[index];
                mesh.push_back({
                    vertex[0],
                    vertex[1],
                    vertex[2],
                    lighting,
                    u0,
                    v0,
                    u_size,
                    v_size,
                    static_cast<uint8_t>(uv[index][0] ? repeat : 0),
                    static_cast<uint8_t>(uv[index][1] ? repeat : 0),
                    yaw,
                    pitch,
                    roll,
                });
            }
        };

        for (size_t x = 0; x < CHUNK_SIZE; ++x)
            for (size_t y = 0; y < CHUNK_SIZE; ++y)
                for (size_t z = 0; z < CHUNK_SIZE; ++z) {
                    const Tile tile = chunk.get(x, y, z);

                    if (!tile || tile.id == air) continue;

                    const auto visible = [&](int nx, int ny, int nz) {
                        const RawChunk* neighbor_chunk = &chunk;
                        if (nx < 0)
                            neighbor_chunk = west;
                        else if (nx >= static_cast<int>(CHUNK_SIZE))
                            neighbor_chunk = east;
                        else if (ny < 0)
                            neighbor_chunk = down;
                        else if (ny >= static_cast<int>(CHUNK_SIZE))
                            neighbor_chunk = up;
                        else if (nz < 0)
                            neighbor_chunk = north;
                        else if (nz >= static_cast<int>(CHUNK_SIZE))
                            neighbor_chunk = south;
                        if (!neighbor_chunk) return true;
                        const auto local_coordinate = [](int coordinate) {
                            if (coordinate < 0) return coordinate + static_cast<int>(CHUNK_SIZE);
                            if (coordinate >= static_cast<int>(CHUNK_SIZE))
                                return coordinate - static_cast<int>(CHUNK_SIZE);
                            return coordinate;
                        };
                        const Tile neighbor = neighbor_chunk->get(static_cast<size_t>(local_coordinate(nx)),
                            static_cast<size_t>(local_coordinate(ny)), static_cast<size_t>(local_coordinate(nz)));
                        return !neighbor || neighbor.id == air;
                    };

                    const uint16_t x0 = static_cast<uint16_t>(x * CHUNK_SIZE);
                    const uint16_t x1 = static_cast<uint16_t>((x + 1) * CHUNK_SIZE);
                    const uint16_t y0 = static_cast<uint16_t>(y * CHUNK_SIZE);
                    const uint16_t y1 = static_cast<uint16_t>((y + 1) * CHUNK_SIZE);
                    const uint16_t z0 = static_cast<uint16_t>(z * CHUNK_SIZE);
                    const uint16_t z1 = static_cast<uint16_t>((z + 1) * CHUNK_SIZE);

                    if (visible(static_cast<int>(x), static_cast<int>(y) - 1, static_cast<int>(z)))
                        add_face({{{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}}}, 36000, 0, -1, 0);

                    if (visible(static_cast<int>(x), static_cast<int>(y) + 1, static_cast<int>(z)))
                        add_face({{{x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {x0, y1, z0}}}, 65535, 0, 1, 0);

                    if (visible(static_cast<int>(x) + 1, static_cast<int>(y), static_cast<int>(z)))
                        add_face({{{x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}}}, 52000, 0, 0, -1);

                    if (visible(static_cast<int>(x) - 1, static_cast<int>(y), static_cast<int>(z)))
                        add_face({{{x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}}}, 48000, 0, 0, 1);

                    // north (z-1): winding fixed
                    if (visible(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z) - 1))
                        add_face({{{x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}}}, 44000, -1, 0, 0);

                    // south (z+1): winding fixed
                    if (visible(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z) + 1))
                        add_face({{{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}}}, 40000, 1, 0, 0);
                }

        return mesh;
    }
} REGISTER_MOD(ChunkMesherImpl, World);
