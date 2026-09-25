#pragma once

#include "registry.hpp"
#include <triolib.hpp>

extern std::map<RawRegistryEntry, RegistryEntry*(*)()> registry_entry_constructors;
extern std::map<Identifier, std::unique_ptr<Registerable>> raw_registries;

struct RegistryList : triolib::AppModule {
    std::map<Identifier, std::unique_ptr<Registerable>> registries;

    void init() override {
        registries = std::move(raw_registries);
        for (const auto& [raw_entry, constructor] : registry_entry_constructors) {
            auto& registry = registries[raw_entry.regid];
            if (!registry) {
                throw std::runtime_error("Registry " + raw_entry.regid + " does not exist for entry " + raw_entry.resid);
            }
            auto entry = constructor();
            entry->resid = raw_entry.resid;
            entry->regid = raw_entry.regid;
            registry->add_resource(std::unique_ptr<RegistryEntry>(entry));
        }
    }

    void loop() override {}
    void quit() override {}
};

