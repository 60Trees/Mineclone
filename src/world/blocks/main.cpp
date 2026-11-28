#include "main.hpp"

struct BlockRegistryImpl : BlockRegistry {
    registryClass("minecraft:block");
} REGISTRY(BlockRegistryImpl);
