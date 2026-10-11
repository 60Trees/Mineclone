#pragma once

#include <triolib.hpp>
#include <world/world.hpp>
#include <renderer/renderer.hpp>

/// Relies on World and Renderer both being valid and initialized.
/// Will be controled by the main Application.
struct MainWorldRenderer : triolib::SubApp {
    tClass;

    struct Renderer : triolib::Module {
        tClass;
        MainWorldRenderer* parent = nullptr;
    };

    World* world = nullptr;
    Renderer* renderer = nullptr;
};
