#pragma once

#include <renderer/renderer.hpp>
#include <string_view>
#include <glm/vec2.hpp>

/// OpenGL renderer base class.
/// @brief `backed = "OpenGL" ; shader_backend = "GLSL"`
/// With this file included, feel free to use OpenGL functions directly.
struct MainGlRenderer : MainRenderer {
    tClass;

    MainGlRenderer() {
        backend = "OpenGL";
        shader_backend = "GLSL";
    }

    SDL_GLContext glcontext = nullptr;
    virtual bool has_extension(std::string_view ext) const = 0;
    bool quit_called = false;
};
