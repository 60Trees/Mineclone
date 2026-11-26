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
#include <array>
#include <chrono>
#include <cmath>
#include <mutex>
#include <utils/noise.hpp>
#include <vector>

struct Octave {
    BigInt scale;      // Wavelength in world units (e.g., 10,000,000 down to 100)
    double amplitude;  // Height contribution in world units
};

struct FastOctave {
    long long scale;
    double amplitude;
};

namespace {
    constexpr RegistryUint air = 0;
    constexpr RegistryUint stone = 1;

    // Ordered from continental/planetary macro-scales down to fine detail.
    const std::vector<Octave> octaves = {
        {BigInt("100000000"), 5000000.0},  // Mega-continents / planetary curvature scale
        {BigInt("10000000"), 800000.0},    // Massive mountain ranges
        {BigInt("1000000"), 120000.0},     // Regional mountain chains
        {BigInt("100000"), 20000.0},       // Valleys & ridges
        {BigInt("15000"), 4000.0},         // Hills
        {BigInt("1500"), 640.0},           // Local terrain variation
        {BigInt("150"), 80.0},             // Boulders / rough ground
        {BigInt("15"), 10.0}               // Micro detail
    };

    const std::array<FastOctave, 8> fast_octaves = {{
        {100000000, 5000000.0},
        {10000000, 800000.0},
        {1000000, 120000.0},
        {100000, 20000.0},
        {15000, 4000.0},
        {1500, 640.0},
        {150, 80.0},
        {15, 10.0},
    }};

    /// Anything whose coordinate is exactly representable as an int64 can skip the
    /// arbitrary-precision path without changing the result.
    const BigInt fast_limit = BigInt(1) << 53;
}  // namespace

struct WorldGenImpl : WorldGen {
    tImplClass(WorldGen);

    noise::Perlin noise;

    void init() override {}
    void loop() override {}
    void quit() override {}

    /// @brief Height for coordinates that fit exactly in an int64.
    /// @detail Chunk generation only ever produces coordinates around
    /// `chunk_position * chunk_width * lod_scale`, far below 2^53, so plain integer
    /// division keeps the same precision as the BigInt path at a fraction of the cost.
    long long height_fast(long long x, long long z) {
        if (x == 0 || z == 0) return 0;

        double total_height = 32.0;

        for (const auto& octave : fast_octaves) {
            const long long ix = x / octave.scale;
            const long long iz = z / octave.scale;
            const double fx = static_cast<double>(x % octave.scale) / static_cast<double>(octave.scale);
            const double fz = static_cast<double>(z % octave.scale) / static_cast<double>(octave.scale);

            // Wrap integer grid coordinates so the noise generator never sees huge values
            const double nx = static_cast<double>(ix % 65536) + fx;
            const double nz = static_cast<double>(iz % 65536) + fz;

            total_height += noise.noise(nx, 0.0, nz) * octave.amplitude;
        }

        return static_cast<long long>(std::floor(total_height));
    }

    /// @brief Arbitrary-precision fallback for callers outside the int64 range.
    BigInt height_big(const BigInt& x, const BigInt& z) {
        if (x == 0 || z == 0) return 0;

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

    BigInt get_height_at(const BigInt& x, const BigInt& z) override {
        if (x >= -fast_limit && x <= fast_limit && z >= -fast_limit && z <= fast_limit)
            return BigInt(height_fast(x.convert_to<long long>(), z.convert_to<long long>()));

        return height_big(x, z);
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
                // Chunk coordinates always fit in an int64, so stay on the fast path and
                // keep the inner y loop as a plain integer comparison.
                const long long world_x = chunk_base_x + static_cast<long long>(x) * scalar;
                const long long world_z = chunk_base_z + static_cast<long long>(z) * scalar;
                const long long height = height_fast(world_x, world_z);

                for (int y = 0; y < width; ++y) {
                    const long long world_y = chunk_base_y + static_cast<long long>(y) * scalar;
                    result.write(x, y, z, world_y < height ? Tile{stone, 0} : Tile{air, 0});
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
