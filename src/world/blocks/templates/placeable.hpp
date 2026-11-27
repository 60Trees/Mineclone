#pragma once

#include <world/blocks/main.hpp>
#include <world/entities/entity.hpp>

/// @brief A collection of blocks or block templates
namespace minecraft::Blocks {
    /// @brief A block that can be placed down. Contains all the
    /// necessary logic (e.g its gui item, placement rules, etc.)
    struct Placeable : virtual Block {
        /// @brief Whether or not a certain entity can place this block. By default, all entities can place it.
        virtual bool can_place(Entity& entity) const { return true; }
        /// @brief What to do when the block is placed. By default, it will be placed at the given coordinates.
        virtual void on_place(WorldEditor& editor, const BlockCoord& coord) {
            editor.set_tile_at(coord, {get_residx(), 0});
        }
    };
}  // namespace minecraft::Blocks
