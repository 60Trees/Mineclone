#include "worldgen.hpp"
#include <world/blocks/block.hpp>
#include "world/world.hpp"

struct WorldGenImpl : WorldGen {
    tImplClass(WorldGen);

    RawChunk generate_chunk(World::ChunkIndex index, RegistryList& reglist) override {
        // Example implementation: Starts off relatively flat with ground at 0,0,0 then an increasing amount of
        // growing sine waves to show off how big terrain can get (infinite render distance, infinite height minecraft
        // clone). Kind of like the player is in this giant bumpy basin

        if (index.pos.x < 0 || index.pos.y < 0 || index.pos.z < 0)
            run_once console.log << "Generating chunk at negative coordinates: " << index.pos.x << ", " << index.pos.y
                                 << ", " << index.pos.z;

        RawChunk retval;

        BlockRegistry& block_registry = reglist.getmod<BlockRegistry>();

        Tile stone{block_registry.get_index("minecraft:stone"), 0};
        Tile air{block_registry.get_index("minecraft:air"), 0};

        // Index.lod has 0=full detail, 1=half detail, 2=quarter detail, 3 = eighth detail, etc
        uint scalar = std::pow(2, index.lod);

        for (int x = 0; x < CHUNK_SIZE; ++x) {
            for (int z = 0; z < CHUNK_SIZE; ++z) {
                int world_x = index.pos.x * CHUNK_SIZE * scalar + x * scalar;
                int world_z = index.pos.z * CHUNK_SIZE * scalar + z * scalar;

                double height = 0;

                for (int i = 1; i < 100; i *= 2) {
                    float frequency = 1.0f / (i * 100.0f);
                    float amplitude = 100.0f / i;

                    height += std::sin(world_x * frequency) * std::cos(world_z * frequency) * amplitude;
                }

                int block_height = static_cast<int>(std::round(height));

                for (int y = 0; y < CHUNK_SIZE; ++y) {
                    int world_y = index.pos.y * CHUNK_SIZE * scalar + y * scalar;
                    if (world_y <= block_height) {
                        retval.set(x, y, z, stone);
                    } else {
                        retval.set(x, y, z, air);
                    }
                }
            }
        }

        return retval;
    }
} REGISTER_MOD(WorldGenImpl, World);
