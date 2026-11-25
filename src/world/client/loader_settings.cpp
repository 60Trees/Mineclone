#include <nlohmann/json.hpp>

void WorldLoader_default_options(nlohmann::json& o) {
    // One radius per LOD level: {horizontal chunks, vertical chunks}. A chunk doubles
    // in world size at every LOD, so these shrink in chunk count while covering roughly
    // the same (then progressively larger) world-space area.
    //
    // Terrain height is roughly 32 +/- 128 blocks, i.e. -96..160. Vertical radius 1
    // only covered 96 blocks at LOD 0, so moving up one chunk pushed the ground out of
    // range and coarse LODs flooded in. Cover the full column at every LOD instead.
    o["radius"] = nlohmann::json::array({
        {20, 4},  // LOD 0: 32-block chunks, 640-block radius, 288 blocks tall
        {15, 3},  // LOD 1: 64-block chunks, 960-block radius, 448 blocks tall
        {11, 2},  // LOD 2: 128-block chunks, 1408-block radius, 640 blocks tall
        {8, 1},   // LOD 3: 256-block chunks, 2048-block radius, 768 blocks tall
    });
    // One priority per LOD. Finer detail gets more loader time; every value must be
    // positive or that LOD would never be loaded.
    o["lod_priorities"] = {4.0, 2.0, 1.25, 1.0};
}
