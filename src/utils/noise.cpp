#include "noise.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <random>

namespace noise {
    struct PerlinImpl : PerlinBaseImpl {
        explicit PerlinImpl(uint64_t seed) : PerlinBaseImpl(seed), rng(seed) {
            std::array<int, 256> permutation;
            std::iota(permutation.begin(), permutation.end(), 0);

            std::shuffle(permutation.begin(), permutation.end(), rng);

            for (int i = 0; i < 256; ++i) {
                p[i] = permutation[i];
                p[i + 256] = permutation[i];
            }
        }

        // i love borrowing code
        double noise(double x, double y, double z) const override {
            const int X = static_cast<int>(std::floor(x)) & 255;
            const int Y = static_cast<int>(std::floor(y)) & 255;
            const int Z = static_cast<int>(std::floor(z)) & 255;

            x -= std::floor(x);
            y -= std::floor(y);
            z -= std::floor(z);

            const double u = fade(x);
            const double v = fade(y);
            const double w = fade(z);

            const int A = p[X] + Y;
            const int AA = p[A] + Z;
            const int AB = p[A + 1] + Z;
            const int B = p[X + 1] + Y;
            const int BA = p[B] + Z;
            const int BB = p[B + 1] + Z;

            const double result = lerp(w,
                lerp(v, lerp(u, grad(p[AA], x, y, z), grad(p[BA], x - 1, y, z)),
                    lerp(u, grad(p[AB], x, y - 1, z), grad(p[BB], x - 1, y - 1, z))),
                lerp(v, lerp(u, grad(p[AA + 1], x, y, z - 1), grad(p[BA + 1], x - 1, y, z - 1)),
                    lerp(u, grad(p[AB + 1], x, y - 1, z - 1), grad(p[BB + 1], x - 1, y - 1, z - 1))));

            return std::clamp(result * 0.5 + 0.5, -1.0, 1.0);
        }

        inline static double fade(double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }

        inline static double lerp(double t, double a, double b) { return a + t * (b - a); }

        inline static double grad(int hash, double x, double y, double z) {
            const int h = hash & 15;

            const double u = h < 8 ? x : y;
            const double v = h < 4 ? y : (h == 12 || h == 14 ? x : z);

            return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
        }

        std::array<int, 512> p{};
        mutable std::mt19937_64 rng;
    };

    std::unique_ptr<PerlinBaseImpl> (*create_perlin_impl)(uint64_t seed) =
        [](uint64_t seed) -> std::unique_ptr<PerlinBaseImpl> { return std::make_unique<PerlinImpl>(seed); };

    Seed (*get_random_seed)() = []() -> Seed {
        static_assert(std::is_same_v<Seed, uint64_t>, "Seed type must be uint64_t");

        thread_local std::mt19937_64 rng(std::random_device{}());
        return rng();
    };
}  // namespace noise
