#include <SDL3/SDL_oldnames.h>
#include <base/input_handler.hpp>
#include <base/fps_handler.hpp>
#include <renderer/renderer.hpp>
#include <cmath>
#include "triolib.hpp"
#include <SDL3/SDL_mouse.h>

static SDL_Window* _window = nullptr;

struct RelativeSdlMouseMode : Renderer {
    tClass;

    void before_sdl_init() override {
    }

    bool enabled = false;
    bool reported_failure = false;

    void try_enable() {
        MainRenderer& renderer = dynamic_cast<MainRenderer&>(*parent);
        if (!(SDL_GetWindowFlags(renderer.window) & SDL_WINDOW_INPUT_FOCUS)) return;
        if (SDL_GetWindowRelativeMouseMode(renderer.window)) {
            enabled = true;
            return;
        }
        if (!SDL_SetWindowRelativeMouseMode(renderer.window, true)) {
            if (!reported_failure) {
                reported_failure = true;
                const std::string relative_mode_error = SDL_GetError();
                const bool grabbed = SDL_SetWindowMouseGrab(renderer.window, true);
                console.err << "RelativeSdlMouseMode\nWindow: " << renderer.window
                            << "\nFailed to enable relative mouse mode: " << relative_mode_error
                            << (grabbed ? "\nMouse grab fallback enabled." : "\nMouse grab fallback failed: " + std::string(SDL_GetError()));
            }
            return;
        }
        enabled = true;
        console.log << "RelativeSdlMouseMode\nWindow: " << renderer.window << "\nRelative mouse mode enabled.";
        _window = renderer.window;
    }

    void init() override { try_enable(); }
    void loop() override { try_enable(); }
    void quit() override {}
} REGISTER_MOD(RelativeSdlMouseMode, MainRenderer);

struct InputHandlerImpl : InputHandler {
    tImplClass(InputHandler);

    std::vector<EventDigester*> digesters;

    void init() override { digesters = parent->get_mods<EventDigester>(); }
    void loop() override {
        mouse_moved = {};

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_MOUSE_MOTION) {
                console.info << "Mouse motion: relative=" << e.motion.xrel << "," << e.motion.yrel;
                if (e.motion.xrel < 0) mouse_moved.left += static_cast<uint>(std::abs(e.motion.xrel));
                if (e.motion.xrel > 0) mouse_moved.right += static_cast<uint>(e.motion.xrel);
                if (e.motion.yrel < 0) mouse_moved.up += static_cast<uint>(std::abs(e.motion.yrel));
                if (e.motion.yrel > 0) mouse_moved.down += static_cast<uint>(e.motion.yrel);
            }
            for (auto d : digesters) d->digest(e);
            if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
                key_states[e.key.scancode].duration = 0.0;
            }
            if (e.type == SDL_EVENT_QUIT) {
                parent->done = true;
            }
            if (e.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
                SDL_Log("Driver: %s", SDL_GetCurrentVideoDriver());
SDL_Log("Window flags: 0x%llx",
    static_cast<unsigned long long>(SDL_GetWindowFlags(_window)));
SDL_Log("Focused: %s",
    (SDL_GetWindowFlags(_window) & SDL_WINDOW_INPUT_FOCUS)
        ? "yes" : "no");

SDL_ClearError();

if (!SDL_SetWindowRelativeMouseMode(_window, true)) {
    SDL_Log("Relative mode failed: %s", SDL_GetError());
}
            }
        }

        auto state = SDL_GetKeyboardState(nullptr);
        raw_state = std::span<const bool>(state, SDL_SCANCODE_COUNT);
        for (uint i = 0; i < SDL_SCANCODE_COUNT; ++i) {
            bool is_pressed = state[i];
            if (key_states[i].pushed()) {
                key_states[i].duration += parent->getmod<FpsHandler>().dt;
                continue;
            }
            key_states[i].pressure = is_pressed ? 1.0f : 0.0f;
            key_states[i].duration = 0.0;
        }
    }

    bool order_init_does_go_before(const triolib::AppModule* app) const override {
        return dynamic_cast<const EventDigester*>(app);
    }

    bool order_loop_does_go_before(const triolib::AppModule* app) const override {
        return !dynamic_cast<const FpsHandler*>(app);
    }
    void quit() override {}
} REGISTER_MOD(InputHandlerImpl);
