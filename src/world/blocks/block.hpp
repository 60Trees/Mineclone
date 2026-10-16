#pragma once

#include <registry/registry.hpp>
#include <triolib.hpp>

struct BlockRegistry : Registry {
    registryClass("minecraft:block");
};

struct BlockEntry : RegistryEntry {
    virtual std::string block_model_path() const {
        return "assets/" + resid.substr(0, resid.find(':')) + "/models/block/" + resid.substr(resid.find(':') + 1) +
               ".json";
    }
};
