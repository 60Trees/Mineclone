#include <world/blocks/main.hpp>
#include <world/blocks/templates/breakable.hpp>
#include <world/blocks/templates/placeable.hpp>

namespace minecraft {
    struct Stone : virtual Block, Blocks::Breakable, Blocks::Placeable {
        blockDef(Stone);
    } RESENTRY(Stone, "minecraft:stone", "minecraft:block");
}  // namespace minecraft
