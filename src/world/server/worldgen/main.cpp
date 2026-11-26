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
#include <vector>
#include <cmath>

struct Octave {
    BigInt scale;      // Wavelength in world units (e.g., 10,000,000 down to 100)
    double amplitude;  // Height contribution in world units
};

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
        if (x == 0 || z == 0) return 0;

        // Ordered from continental/planetary macro-scales down to fine detail
        static const std::vector<Octave> octaves = {
            {BigInt("100000000"), 5000000.0},  // Mega-continents / planetary curvature scale
            {BigInt("10000000"), 800000.0},    // Massive mountain ranges
            {BigInt("1000000"), 120000.0},     // Regional mountain chains
            {BigInt("100000"), 20000.0},       // Valleys & ridges
            {BigInt("15000"), 4000.0},         // Hills
            {BigInt("1500"), 640.0},           // Local terrain variation
            {BigInt("150"), 80.0},             // Boulders / rough ground
            {BigInt("15"), 10.0}               // Micro detail
        };

        double total_height = 32.0;

        for (const auto& octave : octaves) {
            // Perform integer division + remainder in BigInt first to preserve precision
            // at extreme distances (beyond 2^53) before converting to double
            const BigInt ix = x / octave.scale;
            const BigInt iz = z / octave.scale;
            const double fx = (x % octave.scale).convert_to<double>() / octave.scale.convert_to<double>();
            const double fz = (z % octave.scale).convert_to<double>() / octave.scale.convert_to<double>();

            // Wrap integer grid coordinates if your noise generator struggles with huge doubles
            const double nx = (ix % 65536).convert_to<double>() + fx;
            const double nz = (iz % 65536).convert_to<double>() + fz;

            total_height += noise.noise(nx, 0.0, nz) * octave.amplitude;
        }

        return BigInt(std::floor(total_height));
    }

    std::mutex speed_mutex;
    std::chrono::steady_clock::time_point speed_window_start = std::chrono::steady_clock::now();
    size_t completed_in_window = 0;

    Chunk generate_chunk(ChunkIndex& index) override {
        const int width = static_cast<int>(*default_chunk_size);
        const long long scalar = 1ll << index.lod;
        const long long chunk_base_x = static_cast<long long>(index.pos.x) * width * scalar;
        const long long chunk_base_y = static_cast<long long>(index.pos.y) * width * scalar;
        const long long chunk_base_z = static_cast<long long>(index.pos.z) * width * scalar;
        Chunk result(*default_chunk_size);

        for (int x = 0; x < width; ++x)
            for (int z = 0; z < width; ++z) {
                // Only the column's height needs big-integer math; the inner y loop is
                // plain integer comparison.
                const BigInt world_x = chunk_base_x + static_cast<long long>(x) * scalar;
                const BigInt world_z = chunk_base_z + static_cast<long long>(z) * scalar;
                const long long height = get_height_at(world_x, world_z).convert_to<long long>();

                for (int y = 0; y < width; ++y) {
                    const long long world_y = chunk_base_y + static_cast<long long>(y) * scalar;
                    result.at(x, y, z) = world_y < height ? Tile{stone, 0} : Tile{air, 0};
                }
            }

        {
            std::lock_guard lock(speed_mutex);
            completed_in_window++;

            const auto now = std::chrono::steady_clock::now();
            const auto elapsed = std::chrono::duration<double>(now - speed_window_start).count();
            if (elapsed >= 1.0) {
                console.debug << "Worldgen throughput (chunks per second):\n"
                              << static_cast<double>(completed_in_window) / elapsed
                              << "\ntrials=" << completed_in_window;
                completed_in_window = 0;
                speed_window_start = now;
            }
        }

        return result;
    }
};

REGISTER_MOD(WorldGenImpl, WorldServer);
