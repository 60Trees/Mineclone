#include "registry.hpp"

std::map<RawRegistryEntry, RegistryEntry* (*)()> registry_entry_constructors = {};
std::map<Identifier, std::unique_ptr<Registerable>> raw_registries = {};

using namespace std;
using namespace triolib;

struct RegistryListImpl : RegistryList {
    tClass;
    double init_order_priority() const override { return 100.0; }
    bool order_init_does_go_before(const triolib::AppModule*) const override { return true; }

    void unload_all() override {
        // clear all resources
        for (auto& [regid, registry] : registries) {
            registry->resources.clear();
        }
    }
    void load_all() override {}
} REGISTER_MOD(RegistryListImpl);
