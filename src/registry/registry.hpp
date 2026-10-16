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
 * - Index / Idx: A number associated with a resource. E.g grass = 0, dirt = 1, etc
 *
 * These names will be mixed in the code, e.g RegID = the ID of a registry,
 * ResID = the ID of a resource, etc.
 */
#pragma once

#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <memory>
#include <triolib.hpp>

using Identifier = std::string;
using IdentifierView = std::string_view;

using RegistryUint = uint16_t;
constexpr static auto RegistryNull = std::numeric_limits<RegistryUint>::max();

/// e.g `resid=minecraft:stone`, `regid=minecraft:block`
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

    RawRegistryEntry(Identifier resid, Identifier regid) : resid(std::move(resid)), regid(std::move(regid)) {}

    bool operator<(const RawRegistryEntry& other) const {
        if (regid != other.regid) return regid < other.regid;

        return resid < other.resid;
    }
};

/**
 * @brief Contains a \ref RawRegistryEntry and a unique index.
 * @note Is virtual
 */
struct RegistryEntry {
    virtual ~RegistryEntry() = default;

    /**
     * @brief The identifier of the resource entry
     * for example, `"minecraft:stone"` or `"minecraft:oak_log"`
     */
    Identifier resid;

    /**
     * @brief A number associated with the resource. E.g grass = 0, dirt = 1, etc
     * @detail
     * It is unique within the registry and will be automatically
     * assigned when loaded.
     * @note Do not touch plz
     */
    RegistryUint resindex;
};

extern std::map<RawRegistryEntry, std::unique_ptr<RegistryEntry> (*)()> registry_entry_constructors;
extern std::vector<std::string> registry_entry_errors;

/// Resource entry
#define RESENTRY(classname, __resid, __regid)                                                                  \
    ;                                                                                                          \
    run_before_main {                                                                                          \
        /**                                                                                                    \
         *  TODO: Replace warn with a priority system (double).                                                \
         *  Higher priority = overrides potential lower priority.                                              \
         */                                                                                                    \
        if (registry_entry_constructors.contains({__resid, __regid})) {                                        \
            registry_entry_errors.push_back(                                                                   \
                "Duplicate registry entry: " + std::string(__resid) + " in registry " + std::string(__regid)); \
        }                                                                                                      \
        registry_entry_constructors[{__resid, __regid}] = []() -> std::unique_ptr<RegistryEntry> {             \
            auto retval = std::make_unique<classname>();                                                       \
            retval->resid = __resid;                                                                           \
            return retval;                                                                                     \
        };                                                                                                     \
    }

/**
 * @brief A registry, which is a collection of registry entries.
 * It is a base class for all registries.
 * @note Call `Registry::init()` to load all the entries in the registry.
 * Also call `Registry::quit()` to unload all the entries in the registry.
 *
 * No looping allowed.
 */
struct Registry : triolib::Module {
    tClass;
    virtual ~Registry() = default;

    /// e.g `minecraft:block`
    std::string regid;

    std::map<RegistryUint, std::unique_ptr<RegistryEntry>> entries;
    virtual std::string get_regid() const = 0;
    RegistryUint get_index(IdentifierView resid) const {
        for (auto& [idx, entry] : entries) {
            if (entry->resid == resid) return idx;
        }
        return RegistryNull;
    }

    inline RegistryEntry& get_entry(RegistryUint idx) const {
        if (!entries.contains(idx)) throw std::out_of_range("Registry entry index not found: " + std::to_string(idx));
        return *entries.at(idx);
    }
    inline RegistryEntry& get_entry(IdentifierView resid) const {
        for (auto& [idx, entry] : entries)
            if (entry->resid == resid) return *entry;
        throw std::out_of_range("Registry entry not found: " + std::string(resid));
    }

    inline bool has_entry(RegistryUint idx) const { return entries.contains(idx); }
    inline bool has_entry(IdentifierView resid) const {
        for (auto& [idx, entry] : entries)
            if (entry->resid == resid) return true;
        return false;
    }

    /**
     * @brief Makes certain IDs reserved.
     *
     * @detail
     * For example, if a world saves all `minecraft:grass` blocks as `2`,
     * then it will be added to this map in a sort of reservation system.
     * So the only thing allowed to be `2` is `minecraft:grass` and nothing else.
     */
    std::map<Identifier, RegistryUint> residx_reservations{};

    /**
     * @brief Gets the next empty index for a given resource ID.
     * @param resid The resource ID to get the next empty index for.
     * @return The next empty index for the given resource ID.
     * @throws std::logic_error if the registry is full or if the resource ID is already taken.
     */
    RegistryUint get_empty_index(std::string resid) const;

    void init() override {
        quit();
        regid = get_regid();
        for (auto& [raw, constructor] : registry_entry_constructors) {
            if (raw.regid == this->regid) {
                auto entry = constructor();
                auto i = get_empty_index(entry->resid);
                entry->resindex = i;
                entries[i] = std::move(entry);
            }
        }
    }
    void loop() override final {}
    void quit() override { entries.clear(); }
};

#define registryClass(_regid)                                \
    tClass;                                                  \
    static std::string static_get_regid() { return _regid; } \
    std::string get_regid() const override { return _regid; }

#define REGISTRY(classname) REGISTER_MOD(classname, RegistryList)

/**
 * @brief A collection of registries.
 */
struct RegistryList : triolib::SubApp {
    tClass;

    void init() override = 0;
    void loop() final override {}
    void quit() override = 0;

    std::map<RawRegistryEntry, RegistryUint> residx_reservations{};
    std::vector<Registry*> registries;

    /// @note Safe to call multiple times
    virtual void unload_all() = 0;

    /// @note Safe to call multiple times
    virtual void load_all() = 0;

    /// @note Safe to call multiple times
    inline void reload_all() {
        unload_all();
        load_all();
    }
};
