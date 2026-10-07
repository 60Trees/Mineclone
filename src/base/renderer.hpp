#pragma once

#include <triolib.hpp>

#include <SDL3/SDL.h>
#include <include/position.hpp>
#include <opengl.hpp>
#include <string_view>

struct Camera {
    float pitch = 0.0f;
    float yaw = 0.0f;
    float roll = 0.0f;
    Position pos;
};

struct Window : triolib::SubApp {
    virtual SDL_Window* sdl_window() const = 0;
    virtual glm::vec<2, int> window_size() const = 0;
    virtual void set_window_size(glm::vec<2, int> size) = 0;
    virtual bool has_extension(std::string_view ext) const = 0;
};

struct Renderer : triolib::Module {
    Camera camera;

    void init() override {}
    void quit() override {}
};
