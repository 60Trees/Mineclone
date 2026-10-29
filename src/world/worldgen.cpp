#include "worldgen.hpp"
#include <thread>
#include <world/blocks/block.hpp>
#include "utils/noise.hpp"
#include "world/world.hpp"
#include <utils/noise.hpp>

struct WorldGenImpl : WorldGen {
    tImplClass(WorldGen);

    noise::Perlin noise;

    RawChunk generate_chunk(World::ChunkIndex index, RegistryList& reglist) override {
        // Simulated lag
        // std::this_thread::sleep_for(std::chrono::milliseconds(10));

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
                    {1.0 / 1000.0, 500.0},
                    {1.0 / 10000.0, 5000.0},
                    {1.0 / 1000000.0, 1000000.0},
                };

                double height = world_x == 0 || world_z == 0 ? 0.0 : noise.noise(world_x, 0, world_z, octaves);
                if (world_x == 1 && world_z == 1) run_once console.log << "Height at (1, 1) = " << height;
                for (int y = 0; y < CHUNK_SIZE; y++) {
                    int world_y = index.pos.y * CHUNK_SIZE * scalar + y * scalar;
                    retval.set(x, y, z, height > world_y ? stone : air);  // Fill with stone below height, air above
                }
            }

        return retval;
    }
} REGISTER_MOD(WorldGenImpl, World);
