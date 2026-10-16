#include "registry.hpp"

std::map<RawRegistryEntry, std::unique_ptr<RegistryEntry> (*)()> registry_entry_constructors{};
std::vector<std::string> registry_entry_errors{};

using namespace std;
using namespace triolib;

RegistryUint Registry::get_empty_index(std::string resid) const {
    if (residx_reservations.contains(resid)) {
        auto needed_id = residx_reservations.at(resid);
        if (needed_id == RegistryNull)
            throw std::logic_error("Registry " + regid + " has an invalid reserved index for " + resid);
        if (entries.contains(needed_id))
            throw std::runtime_error(
                "Registry " + regid + " already has an entry for " + resid + " at index " + std::to_string(needed_id));
        return needed_id;
    }
    for (RegistryUint retval = 0; retval < RegistryNull; ++retval) {
        if (entries.contains(retval)) {
            continue;
        }
        if (std::find_if(residx_reservations.begin(), residx_reservations.end(),
                [retval](const auto& pair) { return pair.second == retval; }) != residx_reservations.end()) {
            retval++;
            continue;
        }
        return retval;
    }

    throw std::runtime_error("Registry " + regid + " is full (" + std::to_string(entries.size()) +
                             " entries, " + std::to_string(residx_reservations.size()) + " reserved)");
}

/// The mods are the registries
struct RegistryListImpl : RegistryList {
    tImplClass(RegistryList);

    void load_all() override { init(); }
    void unload_all() override { quit(); }

    void init() override {
        mods.clear();
        load_all_mods();
        for (auto& mod : mods) {
            if (auto* registry = dynamic_cast<Registry*>(mod.get())) {
                registry->regid = registry->get_regid();
                registry->residx_reservations.clear();
                for (auto& [raw, id] : residx_reservations) {
                    if (raw.regid == registry->get_regid()) registry->residx_reservations[raw.resid] = id;
                }
            }
            mod->init();
        }

        for (auto& err : registry_entry_errors) console.err << "Registry error:\n" << err;
        registry_entry_errors.clear();
    }
    void quit() override {
        for (auto& mod : mods) mod->quit();
        mods.clear();
    }
} REGISTER_SUBAPP(RegistryListImpl);
