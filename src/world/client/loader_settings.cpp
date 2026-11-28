#include <nlohmann/json.hpp>
#include <world/shared.hpp>

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>

namespace {
    size_t lod_count = 3;

    /// @brief Test/debug override: `MINECLONE_LOAD_RADIUS=horizontal[,vertical]`
    /// applies the same chunk radius to every LOD. This is useful for running on
    /// memory-constrained machines without changing the defaults.
    std::optional<std::pair<size_t, size_t>> load_radius_override() {
        const char* value = std::getenv("MINECLONE_LOAD_RADIUS");
        if (!value || !*value) return std::nullopt;

        const std::string text = value;
        const auto comma = text.find(',');
        try {
            const int horizontal = std::stoi(text.substr(0, comma));
            const int vertical = comma == std::string::npos ? std::max(1, horizontal / 2)
                                                            : std::stoi(text.substr(comma + 1));
            if (horizontal <= 0 || vertical <= 0) return std::nullopt;
            return std::pair{static_cast<size_t>(horizontal), static_cast<size_t>(vertical)};
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
}

std::vector<LodDescriptor> get_default_lod_descriptors() {
    std::vector<LodDescriptor> retval;

    tassert(lod_count > 0);

    retval.reserve(lod_count);
    size_t scale = 1;
    size_t size = *default_chunk_size;
    for (int i = 0; i < lod_count; i++) {
        retval.emplace_back(size, scale);
        scale *= 2;
        // if (size >= 2) size /= 2;
    }
    return retval;
}

void WorldLoader_default_options(nlohmann::json& o) {
    // One radius per LOD level: {horizontal chunks, vertical chunks}. A chunk doubles
    // in world size at every LOD, so these shrink in chunk count while covering roughly
    // the same (then progressively larger) world-space area.
    //
    // Terrain height is roughly 32 +/- 128 blocks, i.e. -96..160. Vertical radius 1
    // only covered 96 blocks at LOD 0, so moving up one chunk pushed the ground out of
    // range and coarse LODs flooded in. Cover the full column at every LOD instead.
    o["radius"] = nlohmann::json::array({
        {40, 20},
        {30, 15},
        {22, 11},
        //{18, 9},
        //{8, 4},
    });

    if (const auto override_radius = load_radius_override()) {
        o["radius"] = nlohmann::json::array();
        for (size_t i = 0; i < lod_count; ++i)
            o["radius"].push_back({override_radius->first, override_radius->second});
    }

    // One priority per LOD. Finer detail gets more loader time; every value must be
    // positive or that LOD would never be loaded.
    o["lod_priorities"] = {4.0, 3.0, 2.5};
}
