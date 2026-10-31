#include "worldgen.hpp"
#include <world/blocks/block.hpp>
#include "utils/noise.hpp"
#include "world/world.hpp"
#include <utils/noise.hpp>

namespace {
    namespace mp {
        using namespace boost::multiprecision;
    }
}  // namespace

struct WorldGenImpl : WorldGen {
    tImplClass(WorldGen);

    noise::Perlin noise{69};

    BigInt get_approx_height_at(BigInt x, BigInt z) override { return getheight(x, z); }

    inline BigInt getheight(const BigInt& x, const BigInt& z) {
        double distance = (double)(mp::sqrt(x * x + z * z));
        return (BigInt)(sin(distance / 1000) * (distance / 4));

        std::vector<noise::Octave> octaves = {
            {1.0 / 25.0, 10.0},
            {1.0 / 50.0, 5.0},
            {1.0 / 100.0, 50.0},
            {1.0 / 1000.0, 500.0},
            {1.0 / 10000.0, 5000.0},
            {1.0 / 30000.0, 2000.0},
        };

        return BigInt(noise.noise((double)x, 0, (double)z, octaves));
    }

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
                BigInt world_x = (long)index.pos.x * (long)(CHUNK_SIZE * scalar) + x * (long)scalar + world->offset.x;
                BigInt world_z = (long)index.pos.z * (long)(CHUNK_SIZE * scalar) + z * (long)scalar + world->offset.z;

                BigInt height = world_x == 0 || world_z == 0 ? 0 : getheight(world_x, world_z);

                if (world_x == 1 && world_z == 1) run_once console.log << "Height at (1, 1) = " << height;
                for (int y = 0; y < CHUNK_SIZE; y++) {
                    BigInt world_y =
                        (long)index.pos.y * (long)(CHUNK_SIZE * scalar) + y * (long)scalar + world->offset.y;
                    retval.set(x, y, z, height > world_y ? stone : air);  // Fill with stone below height, air above
                }
            }

        return retval;
    }

} REGISTER_MOD(WorldGenImpl, World);
