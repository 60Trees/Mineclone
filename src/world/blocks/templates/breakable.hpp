#pragma once

#include <world/blocks/main.hpp>
#include <world/entities/entity.hpp>

/// @brief A collection of blocks or block templates
namespace minecraft::Blocks {
    /// @brief A block that can be broken. Includes all the necessary
    /// logic (e.g hardness, which tool is best, loot table when broekn, etc.)
    struct Breakable : virtual Block {
        float hardness = 1.0f;
        /// @brief Whether or not a certain entity can break this block. By default, all entities can break it.
        virtual bool can_break(Entity& entity) const { return true; }
        /// @brief What to do when the block is broken. By default, it will be replaced with air.
        virtual void on_break(WorldEditor& editor, const BlockCoord& coord) {
            editor.set_tile_at(
                coord, {editor.server.application->get_subapp<RegistryList>().getmod<BlockRegistry>().get_index(
                            "minecraft:air"),
                           0});
        }
    };
}  // namespace minecraft::Blocks
