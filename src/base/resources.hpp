#pragma once

#include <triolib.hpp>
#include <resources/resource_pack.hpp>
#include <utility>
#include <vector>
#include <unordered_set>

struct ResourceHandler : triolib::AppModule {
    // i love wrapping wrappers
    Mineclone::Resources resources;
    bool has_loaded = false;
    bool has_started_loading = false;

    enum class ResourceOriginType { ZipFromFs, FolderFromFs, ZipFromCmrc, FolderFromCmrc };
    std::vector<std::pair<std::string, ResourceOriginType>> resource_origins;
    std::unordered_set<std::string> whitelist;

    virtual void start_loading() = 0;

    bool does_go_after_init(const triolib::AppModule& other) const override { return true; }

    void init() override { start_loading(); }
    void loop() override {}
    void quit() override {}
};
