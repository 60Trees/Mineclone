#include "block.hpp"

struct AirBlock : BlockEntry {
} RESENTRY(AirBlock, "minecraft:air", "minecraft:block");
struct StoneBlock : BlockEntry {
} RESENTRY(StoneBlock, "minecraft:stone", "minecraft:block");

struct BlockRegistryImpl : BlockRegistry {
    tImplClass(BlockRegistry);
} REGISTRY(BlockRegistryImpl);