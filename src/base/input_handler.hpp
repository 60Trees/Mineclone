/**
 * @author 60Trees_ (github.com/60Trees)
 * @version 1.0.0
 */

#pragma once
#include <SDL3/SDL.h>
#include <SDL3/SDL_scancode.h>
#include <cmath>
#include <concepts>
#include <limits>

#include <triolib.hpp>

struct InputState {
    float pressure = 0.0f;
    float threshold = 0.5f;
    // Incremented every time it's pressed
    uint repeat_count = 0;

    /// 0 = transition this frame, >0 = seconds since the last transition.
    float duration = std::numeric_limits<float>::infinity();

    inline operator bool() const { return pushed(); }

    inline bool pushed() const { return pressure >= threshold; }

    inline bool just_pressed() const { return pushed() && duration == 0.0f; }

    inline bool just_released() const { return !pushed() && duration == 0.0f; }

    template <std::floating_point F>
    inline void update(float _pressure, F delta_time) {
        const bool was_pressed = pushed();
        const bool is_pressed = _pressure >= threshold;

        if (was_pressed != is_pressed)
            duration = 0.0f;
        else
            duration += static_cast<float>(delta_time);

        pressure = _pressure;
    }

    template <std::floating_point F>
    inline void update(bool pressed, F delta_time) {
        update(pressed ? 1.0f : 0.0f, delta_time);
    }
};

struct EventDigester : triolib::AppModule {
    tClass;

    void init() override {}
    void loop() override {}
    void quit() override {}

    virtual void digest(SDL_Event& e) = 0;
};

struct InputHandler : triolib::AppModule {
    tClass;

    bool mouse_fps_style = true;
    struct {
        uint up = 0, down = 0, left = 0, right = 0;
    } mouse_moved;
    int mouse_scroll = 0;

    InputState key_states[SDL_SCANCODE_COUNT]{};
    std::span<const bool> raw_state;
};
