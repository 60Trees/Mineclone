#pragma once

#include <renderer/glrenderer.hpp>
#include <opengl.hpp>

struct UI_RendererImpl : GlRenderer {
    opengl::VBO vbo;
    opengl::VAO vao;

    void init() override {
        
    }
    void loop() override {}
    void quit() override {}
} REGISTER_MOD(UI_RendererImpl, MainRenderer);
