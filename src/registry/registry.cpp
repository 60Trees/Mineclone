#include "registry.hpp"

std::map<RawRegistryEntry, RegistryEntry* (*)()> registry_entry_constructors = {};
std::map<Identifier, std::unique_ptr<Registerable>> raw_registries = {};

struct RegistryListImpl : RegistryList {
    float get_priority_init() const override { return 100.0f; }
    bool does_go_before_init(const triolib::AppModule&) const override {
        return true;
    }

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
} REGISTER_MOD(RegistryListImpl);
