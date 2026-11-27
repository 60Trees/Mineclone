#include <world/blocks/main.hpp>

namespace minecraft {
    struct Air : Block {
        blockDef(Air);
    } RESENTRY(Air, "minecraft:air", "minecraft:block");
}  // namespace minecraft
