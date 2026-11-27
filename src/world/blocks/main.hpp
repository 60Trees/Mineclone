#pragma once

#include <registry/registry.hpp>

#include <world/server/main.hpp>
#include "base/resources.hpp"
#include "triolib.hpp"

/**
 * @brief The main block class, where all blocks inherit from.
 *
 * @detail An example of this:
 *
 * ```cpp
 * struct Stone : virtual Block, Blocks::Breakable, Blocks::Placeable {
 *     blockDef(Stone);
 * } RESENTRY(Stone, "minecraft:stone", "minecraft:block");
 * ```
 *
 * @note Please extend virtually, e.g `struct MyBlock : virtual Block { ... }` to avoid diamond inheritance issues.
 */
struct Block : RegistryEntry {
    virtual ~Block() = default;
    virtual std::string vname() const { return get_dynamic_name(*this); }

    bool solid = true;
    bool opaque = true;
    uint8_t light_emission = 0;
    uint8_t light_filter = 15;

    template <typename T>
    inline const T* to() const noexcept {
        return dynamic_cast<const T*>(this);
    };

    /// @TODO: Replace `get_block_model` with blockstates. Will be painful.
    // Not now though
    virtual std::string get_block_model() const {
        return ResourceHandler::format_identifier(resid, "assets/{0}/models/{1}.json");
    }
};

#define blockDef(classname)                                             \
    tClass;                                                             \
    virtual IdentifierView get_resid() const override { return resid; } \
    virtual RegistryUint get_residx() const override { return resindex; }
