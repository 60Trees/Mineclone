#include "world.hpp"

Tile TileNull{};

uint Chunk::get_index(size_t block_index) const noexcept {
    if (bucket == 0) return 0;

    const size_t bit = block_index * bucket;
    const size_t byte = bit / 8;
    const uint shift = bit % 8;

    uint value = 0;

    for (uint i = 0; i < bucket; i += 8) {
        value |= uint(data[byte + i / 8]) << i;
    }

    return (value >> shift) & ((uint(1) << bucket) - 1);
}
void Chunk::set_index(size_t block_index, uint value) noexcept {
    if (bucket == 0) return;

    const size_t bit = block_index * bucket;
    const size_t byte = bit / 8;
    const uint shift = bit % 8;

    const uint mask = (uint(1) << bucket) - 1;

    value &= mask;

    // Clear and write the bits.
    uint current = 0;

    for (uint i = 0; i < bucket; i += 8) {
        current |= uint(data[byte + i / 8]) << i;
    }

    current &= ~(mask << shift);
    current |= value << shift;

    for (uint i = 0; i < bucket; i += 8) {
        data[byte + i / 8] = uint(current >> i);
    }
}
void Chunk::resize_palette() noexcept {
    const uint new_bucket = get_bucket_size(palette.size());

    if (new_bucket == bucket) return;

    // Save the old packed indices.
    std::vector<uint> old_indices(BLOCK_COUNT);

    for (size_t i = 0; i < BLOCK_COUNT; ++i) old_indices[i] = get_index(i);

    bucket = new_bucket;

    const size_t total_bits = BLOCK_COUNT * bucket;

    data.resize(bytes_for(total_bits));
    std::fill(data.begin(), data.end(), uint8_t{0});

    // Repack using the new bucket size.
    for (size_t i = 0; i < BLOCK_COUNT; ++i) set_index(i, old_indices[i]);
}
void Chunk::set_blocks(const std::array<Tile, BLOCK_COUNT>& tiles) {
    palette.clear();
    for (const Tile& tile : tiles)
        if (std::find(palette.begin(), palette.end(), tile) == palette.end())
            palette.push_back(tile);

    resize_palette();

    for (size_t i = 0; i < tiles.size(); ++i) {
        auto it = std::find(palette.begin(), palette.end(), tiles[i]);

        set_index(i, uint(it - palette.begin()));
    }
}
Tile Chunk::get(size_t x, size_t y, size_t z) const noexcept {
    const size_t i = index(x, y, z);
    if (palette.empty()) return TileNull;

    return palette[get_index(i)];
}
void Chunk::set(size_t x, size_t y, size_t z, const Tile& tile) noexcept {
    auto it = std::find(palette.begin(), palette.end(), tile);

    size_t palette_index;

    if (it == palette.end()) {
        // New block type.
        palette.push_back(tile);

        palette_index = palette.size() - 1;

        resize_palette();
    } else {
        palette_index = size_t(it - palette.begin());
    }

    set_index(index(x, y, z), uint(palette_index));
}
