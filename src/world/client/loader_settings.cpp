#include <nlohmann/json.hpp>

void WorldLoader_default_options(nlohmann::json& o) {
    // One radius per LOD level: {horizontal chunks, vertical chunks}. A chunk doubles
    // in world size at every LOD, so these shrink in chunk count while covering roughly
    // the same (then progressively larger) world-space area.
    o["radius"] = nlohmann::json::array({
        {8, 4},  // LOD 0: 32-block chunks
        {6, 3},  // LOD 1: 64-block chunks
        {4, 2},  // LOD 2: 128-block chunks
        {3, 1},  // LOD 3: 256-block chunks
    });
    // One priority per LOD. Finer detail gets more loader time; every value must be
    // positive or that LOD would never be loaded.
    o["lod_priorities"] = {4.0, 2.0, 1.25, 1.0};
}
