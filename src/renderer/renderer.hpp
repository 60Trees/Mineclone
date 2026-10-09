#pragma once

#include <triolib.hpp>

#include <SDL3/SDL.h>
#include <include/position.hpp>
#include <world/world.hpp>

struct MainRenderer : triolib::SubApp {
    tSubApp(MainRenderer);
    SDL_Window* window = nullptr;

    virtual const glm::vec<2, int>& window_size() const = 0;

    std::string backend;
    std::string shader_backend;
    std::string version;
    std::string shader_version;
};

/**
 * `init()` = run before loop
 * `loop()` = render a frame
 * `quit()` = run after loop
 */
struct Renderer : triolib::Module {
    tClass;

    void init() override = 0;
    void loop() override = 0;
    void quit() override = 0;
};
