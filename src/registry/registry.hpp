/**
 * @author 60Trees (github.com/60Trees)
 * @brief Minecraft registry implementation
 * for C++
 * https://minecraft.wiki/w/Identifier#Java_Edition_usage
 *
 * ## Names
 * - ID / Identifier: a namespace-path pair, e.g. `minecraft:stone`
 * - Reg / Registry: a collection of IDs, e.g. items or blocks
 * - RegEntry / RegistryEntry / Res / Resource: an entry in a registry, e.g.
 * stone or oak log
 *
 * These names will be mixed in the code, e.g RegID = the ID of a registry,
 * ResID = the ID of a resource, etc.
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

    std::map<RegistryUint, std::unique_ptr<RegistryEntry>> resources;

    RegistryUint get_id_of(IdentifierView id) const {
        for (const auto& [index, entry] : resources)
            if (entry->resid == id) return index;
        return RegistryNull;
    }

    /**
     * @brief Get the next available ID in the registry.
     * @return The next available ID.
     */
    inline RegistryUint get_empty_id() const {
        auto retval = resources.size();
        while (resources.contains(retval)) retval++;
        if (retval == RegistryNull)
            throw std::runtime_error("Registry " + regid + " is full (" + std::to_string(RegistryNull) + " entries)");
        return retval;
    }

    void add_resource(std::unique_ptr<RegistryEntry> entry) {
        RegistryUint index = get_empty_id();
        entry->regindex = index;
        resources[index] = std::move(entry);
    }

    Identifier regid;
};

extern std::map<RawRegistryEntry, RegistryEntry* (*)()> registry_entry_constructors;
extern std::map<Identifier, std::unique_ptr<Registerable>> raw_registries;

/**
 * @brief A collection of registries.
 * Example of adding a registry:
 * ```cpp
 * struct Item : triolib::AppModule, Registerable { tClass;
 *     void init() override {
 *         parent->get<RegistryList>().registries["minecraft:item"] =
 *     }
 * }
 */
struct RegistryList : triolib::AppModule {
    tClass;
    void init() override { load_all(); }
    void loop() override {}
    void quit() override { unload_all(); }

    /// The list or registries (i.e items, blocks, etc)
    std::map<Identifier, Registerable*> registries;

    /// Adds a non-owned registry to the list of registries.
    /// Best used for AppModules.
    ///
    /// @note The registry must be valid for the lifetime of the RegistryList.
    virtual void add_registerable(Registerable* reg, Identifier regid) {
        if (!reg) throw std::invalid_argument("Cannot add a null registry.");
        registries[std::move(regid)] = reg;
    }

    /// Adds an owned registry to the list of registries.
    virtual void add_registerable(std::unique_ptr<Registerable> reg, Identifier regid) {
        if (!reg) throw std::invalid_argument("Cannot add a null registry.");
        auto* raw = reg.get();
        owned_registries.push_back(std::move(reg));
        registries[std::move(regid)] = raw;
    }

    /**
     * @brief Describes if any resource needs a specific uint
     * @note This is only considered when loading / reloading.
     */
    std::map<RawRegistryEntry, RegistryUint> needed_registry_ids{};
    std::vector<std::unique_ptr<Registerable>> owned_registries;

    bool order_init_does_go_after(const triolib::AppModule*) const override { return true; }

    /// @note Should be safe to call multiple times
    virtual void unload_all() = 0;

    /// @note Not safe to call multiple times
    virtual void load_all() = 0;

    /// @note Safe to call multiple times
    inline void reload_all() {
        unload_all();
        load_all();
    }
};

/**
 * @brief Registers a resource with a registry.
 *
 * @param classname The class of the resource to register. Must extend off
 * `RegistryEntry` and have a default constructor.
 *
 * @note The resource will be constructed every time \ref
 * RegistryList::load_all() or
 * \ref RegistryList::reload_all() is called.
 */
#define REGISTER(classname, resid, regid)                                                                 \
    ;                                                                                                     \
    run_before_main {                                                                                     \
        registry_entry_constructors[{resid, regid}] = []() -> RegistryEntry* { return new classname(); }; \
    }
