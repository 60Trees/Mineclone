#pragma once

#include <renderer/renderer.hpp>
#include <triolib.hpp>

struct LoadingScreen : Renderer {
    tClass;

    float* progress = nullptr;
};

