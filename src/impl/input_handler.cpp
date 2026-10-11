#include <SDL3/SDL_oldnames.h>
#include <base/input_handler.hpp>
#include <base/fps_handler.hpp>
#include "triolib.hpp"

struct InputHandlerImpl : InputHandler {
    tImplClass(InputHandler);

    std::vector<EventDigester*> digesters;

    void init() override { digesters = parent->get_mods<EventDigester>(); }
    void loop() override {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            for (auto d : digesters) d->digest(e);
            if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
                key_states[e.key.scancode].duration = 0.0;
            }
            if (e.type == SDL_EVENT_QUIT) {
                parent->done = true;
            }
        }

        auto state = SDL_GetKeyboardState(nullptr);
        raw_state = std::span<const bool>(state, SDL_SCANCODE_COUNT);
        for (uint i = 0; i < SDL_SCANCODE_COUNT; ++i) {
            bool is_pressed = state[i];
            if (key_states[i].pressed == is_pressed) {
                key_states[i].duration += parent->getmod<FpsHandler>().dt;
                continue;
            }
            key_states[i].pressed = is_pressed;
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
