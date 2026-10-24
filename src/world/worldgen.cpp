#include "worldgen.hpp"
#include <world/blocks/block.hpp>
#include "utils/noise.hpp"
#include "world/world.hpp"
#include <utils/noise.hpp>

struct WorldGenImpl : WorldGen {
    tImplClass(WorldGen);

    noise::Perlin noise;

    RawChunk generate_chunk(World::ChunkIndex index, RegistryList& reglist) override {
        // Example implementation: Starts off relatively flat with ground at 0,0,0 then an increasing amount of
        // growing sine waves to show off how big terrain can get (infinite render distance, infinite height minecraft
        // clone). Kind of like the player is in this giant bumpy basin

        RawChunk retval;

        BlockRegistry& block_registry = reglist.getmod<BlockRegistry>();

        Tile stone{block_registry.get_index("minecraft:stone"), 0};
        Tile air{block_registry.get_index("minecraft:air"), 0};

        // Index.lod has 0=full detail, 1=half detail, 2=quarter detail, 3 = eighth detail, etc
        uint scalar = std::pow(2, index.lod);

        for (int x = 0; x < CHUNK_SIZE; x++)
            for (int z = 0; z < CHUNK_SIZE; z++) {
                int world_x = index.pos.x * CHUNK_SIZE * scalar + x * scalar;
                int world_z = index.pos.z * CHUNK_SIZE * scalar + z * scalar;

                std::vector<noise::Octave> octaves = {
                    {1.0 / 25.0, 10.0},
                    {1.0 / 50.0, 5.0},
                    {1.0 / 100.0, 100.0},
                };

                double height = noise.noise(world_x, 0, world_z, octaves);
                for (int y = 0; y < CHUNK_SIZE; y++) {
                    int world_y = index.pos.y * CHUNK_SIZE * scalar + y * scalar;
                    retval.set(x, y, z, height > world_y ? stone : air);  // Fill with stone below height, air above
                }
            }

        return retval;
    }
} REGISTER_MOD(WorldGenImpl, World);
