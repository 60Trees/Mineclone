/**
 * @file world/options.cpp
 * @brief The world options
 * @author 60Trees_ (github.com/60Trees)
 * @detail
 * This file has a bunch of options for the world.
 * It is designed to compile quickly and easy to edit.
 */

#include <cstddef>
#include <world/shared.hpp>

const size_t* default_chunk_size = new size_t(32);

template <typename T>
struct Pair { T x; T y; };

Pair<size_t> default_view_distance(LodLevel lod) {
    return {20, 20};
    //constexpr size_t distances[] = {8, 6, 4, 2};
    //return lod < sizeof(distances) / sizeof(distances[0]) ? distances[lod] : 0;
}

double default_lod_priority(LodLevel lod) {
    return lod != 0 ? 0.0 : 1.0;
    return lod < 4 ? 1.0 / static_cast<double>(lod + 1) : 0.0;
}

size_t* max_lod_count = new size_t(4);
Pair<size_t> (*default_get_view_distance)(LodLevel) = default_view_distance;
double (*default_get_lod_priority)(LodLevel) = default_lod_priority;

bool Tile::operator==(const Tile& other) const { return id == other.id && data == other.data; }

Tile::operator bool() const { return id != RegistryNull && id != 0; }
