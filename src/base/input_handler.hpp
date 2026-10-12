/**
 * @author 60Trees_ (github.com/60Trees)
 * @version 1.0.0
 */

#include <SDL3/SDL.h>
#include <SDL3/SDL_scancode.h>
#include <cmath>
#include <triolib.hpp>

struct ScancodeKeyState {
    bool pressed = false;
    /// 0 = this frame, >0 = how many seconds ago
    float duration = INFINITY;
    inline operator bool() const { return pressed; }

    inline bool pushed() const { return pressed; }
    inline bool just_pressed() const { return pressed && duration == 0.0; }
    inline bool just_released() const { return !pressed && duration == 0.0; }
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

    ScancodeKeyState key_states[SDL_SCANCODE_COUNT]{};
    std::span<const bool> raw_state;
};
