/**
 * @author 60Trees_ (github.com/60Trees)
 * @version 1.0.0
 */

#include <SDL3/SDL_events.h>
#include <triolib.hpp>
#include <array>

struct InputHandler : triolib::AppModule {
    std::array<bool, SDL_SCANCODE_COUNT> held{};
    std::array<bool, SDL_SCANCODE_COUNT> pressed{};
    std::array<bool, SDL_SCANCODE_COUNT> repeated{};

    void digest_event(const SDL_Event& event) {
        if (event.type != SDL_EVENT_KEY_DOWN &&
            event.type != SDL_EVENT_KEY_UP)
            return;

        const auto scancode = event.key.scancode;
        if (scancode < 0 || scancode >= SDL_SCANCODE_COUNT) return;

        if (event.type == SDL_EVENT_KEY_DOWN) {
            held[scancode] = true;
            if (event.key.repeat)
                repeated[scancode] = true;
            else
                pressed[scancode] = true;
        } else {
            held[scancode] = false;
        }
    }

    bool is_held(SDL_Scancode key) const { return held[key]; }

    bool consume_press(SDL_Scancode key) {
        const bool was_pressed = pressed[key];
        pressed[key] = false;
        return was_pressed;
    }

    bool consume_repeat_press(SDL_Scancode key) {
        const bool was_pressed = pressed[key] || repeated[key];
        pressed[key] = false;
        repeated[key] = false;
        return was_pressed;
    }

    void init() override {}
    void loop() override {}
    void quit() override {}
};
