/**
 * @file world/server/worldgen/main.cpp
 * @brief The world generator implementation
 * @author 60Trees_ (github.com/60Trees)
 * @detail
 * This file is responsible for actually generating
 * the world, and other worldgen related things.
 */
#include "main.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <utils/noise.hpp>

namespace {
    constexpr RegistryUint air = 0;
    constexpr RegistryUint stone = 1;
}  // namespace

struct WorldGenImpl : WorldGen {
    tImplClass(WorldGen);

    noise::Perlin noise;

    void init() override {}
    void loop() override {}
    void quit() override {}

    BigInt get_height_at(const BigInt& x, const BigInt& z) override {
        const auto xd = x.convert_to<double>();
        const auto zd = z.convert_to<double>();
        if (x == 0 || z == 0) return 0;

        const auto terrain_noise = noise.noise(xd / 16.0, 0.0, zd / 16.0);
        const auto height = std::floor(32.0 + terrain_noise * 12.0);
        return BigInt(std::clamp(height, 1.0, 64.0));
    }

    std::mutex speed_mutex;
    unsigned long long average_over_second;
    size_t average_amount = 0;

    Chunk generate_chunk(ChunkIndex& index) override {
        auto start = std::chrono::high_resolution_clock::now();
        const auto width = static_cast<int>(*default_chunk_size);
        const auto scalar = BigInt(1u << index.lod);
        Chunk result(*default_chunk_size);

        for (int x = 0; x < width; ++x)
            for (int z = 0; z < width; ++z) {
                const BigInt world_x = BigInt(index.pos.x) * width * scalar + x * scalar;
                const BigInt world_z = BigInt(index.pos.z) * width * scalar + z * scalar;
                const BigInt height = get_height_at(world_x, world_z);

                for (int y = 0; y < width; ++y) {
                    const BigInt world_y = BigInt(index.pos.y) * width * scalar + y * scalar;
                    result.at(x, y, z) = world_y < height ? Tile{stone, 0} : Tile{air, 0};
                }
            }

        auto end = std::chrono::high_resolution_clock::now();

        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

        std::lock_guard __(speed_mutex);

        average_over_second += duration;
        average_amount++;

        run_limited(1) {
            if (average_over_second == 0) {
                average_amount = 0;
                return result;
            }

            double chunks_per_second = (1'000'000.0 * average_amount) / average_over_second;

            console.debug << "Worldgen average speed (chunks per second):\n"
                          << chunks_per_second << "\ntrials=" << average_amount;

            average_over_second = 0;
            average_amount = 0;
        }

        return result;
    }
};

REGISTER_MOD(WorldGenImpl, WorldServer);
