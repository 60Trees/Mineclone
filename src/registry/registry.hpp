/**
 * @author 60Trees (github.com/60Trees)
 * @brief Minecraft registry implementation
 * for C++
 * https://minecraft.wiki/w/Identifier#Java_Edition_usage
 *
 * ## Names
 * - ID / Identifier: a namespace-path pair, e.g. `minecraft:stone`
 * - Reg / Registry: a collection of IDs, e.g. items or blocks
 * - RegEntry / RegistryEntry / Res / Resource: an entry in a registry, e.g. stone or oak log
 *
 * These names will be mixed in the code, e.g RegID = the ID of a registry, ResID = the ID of a resource, etc.
 */
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <memory>
#include <triolib.hpp>

using Identifier = std::string;
using IdentifierView = std::string_view;

using RegistryUint = uint16_t;
constexpr static auto RegistryNull = std::numeric_limits<RegistryUint>::max();

struct RawRegistryEntry {
    /**
     * @brief The identifier of the registry entry
     * for example, `"minecraft:stone"` or `"minecraft:oak_log"`
     */
    Identifier resid;

    /**
     * @brief The type of the registry entry
     * for example, `"minecraft:item"` or `"minecraft:worldgen/biome"`
     */
    Identifier regid;
};

/**
 * @brief A registry entry, which is a resource in a registry.
 * It is a base class for all registry entries.
 */
struct RegistryEntry : RawRegistryEntry {
    friend struct Registry;
    virtual ~RegistryEntry() = default;

    /**
     * @brief A number associated with the registry entry.
     * It is unique within the registry.
     */
    RegistryUint regindex;
};

/**
 * @brief A registry, which is a collection of registry entries.
 * It is a base class for all registries.
 */
struct Registerable {
    virtual ~Registerable() = default;

    std::map<RegistryUint, std::unique_ptr<RegistryEntry>> entries;

    RegistryUint get_id_of(IdentifierView id) const {
        for (const auto& [index, entry] : entries)
            if (entry->resid == id) return index;
        return RegistryNull;
    }

    /**
     * @brief Get the next available ID in the registry.
     * @return The next available ID.
     */
    inline RegistryUint get_empty_id() const {
        auto retval = entries.size();
        while (entries.contains(retval)) retval++;
        if (retval == RegistryNull)
            throw std::runtime_error("Registry " + regid + " is full (" + std::to_string(RegistryNull) + " entries)");
        return retval;
    }

    void add_resource(std::unique_ptr<RegistryEntry> entry) {
        RegistryUint index = get_empty_id();
        entry->regindex = index;
        entries[index] = std::move(entry);
    }

    Identifier regid;
};

extern std::map<RawRegistryEntry, RegistryEntry* (*)()> registry_entry_constructors;
extern std::map<Identifier, std::unique_ptr<Registerable>> raw_registries;

struct RegistryList : triolib::AppModule {
    std::map<Identifier, std::unique_ptr<Registerable>> registries;
};

#define REGISTER(classname, resid, regid)                                                                 \
    ;                                                                                                     \
    run_before_main {                                                                                     \
        registry_entry_constructors[{resid, regid}] = []() -> RegistryEntry* { return new classname(); }; \
    }
