#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <glm/vec2.hpp>

namespace noise {
    using Seed = uint64_t;

    extern Seed (*get_random_seed)();

    struct Octave {
        double frequency = 1.0;
        double amplitude = 1.0;
    };

    struct PerlinBaseImpl {
        virtual ~PerlinBaseImpl() = default;
        inline PerlinBaseImpl(Seed seed) : seed(seed) {}
        Seed seed = 0;
        // @brief Returns a noise value in the range [-1.0, 1.0] for the given coordinates.
        virtual double noise(double x, double y, double z) const = 0;
    };

    extern std::unique_ptr<PerlinBaseImpl> (*create_perlin_impl)(uint64_t seed);

    struct Perlin final {
        inline Perlin(uint64_t seed = get_random_seed()) : impl(create_perlin_impl(seed)) {}
        inline double noise(double x, double y, double z) const { return impl->noise(x, y, z); }
        inline double noise(double x, double y, double z, std::vector<Octave> octaves) const {
            double total = 0.0;
            for (const auto& octave : octaves) {
                total += impl->noise(x * octave.frequency, y * octave.frequency, z * octave.frequency) *
                         octave.amplitude * 2;
            }
            return total;
        }

        private:
        std::unique_ptr<PerlinBaseImpl> impl;
    };
}  // namespace noise
