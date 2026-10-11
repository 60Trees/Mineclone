#include "loading_screen.hpp"
#include <triolib.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_render.h>
#include <glm/vec2.hpp>

struct LoadingScreenImpl : LoadingScreen {
    tImplClass(LoadingScreen);

    void init() override {}
    void loop() override {
        if (!progress) return;

        const float p = *progress;
        tassume(p >= 0.0f && p <= 1.0f, "Loading progress must be in [0, 1] range, not " + std::to_string(p));

        uint32_t bg_colour = 0xEF323D;

        SDL_Renderer* renderer =
            SDL_CreateRenderer(dynamic_cast<MainRenderer*>(this->parent)->window, "Loading screen renderer");

        SDL_SetRenderDrawColor(renderer, (bg_colour >> 16) & 0xFF, (bg_colour >> 8) & 0xFF, bg_colour & 0xFF, 255);
        SDL_RenderClear(renderer);

        // Draw a white progress bar
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        const auto& window_size = dynamic_cast<MainRenderer*>(this->parent)->window_size();
        SDL_FRect progress_bar_rect = {static_cast<float>(window_size.x) / 4,
            static_cast<float>(window_size.y) / 2 - 10, static_cast<float>(window_size.x) / 2 * p, 20};
        SDL_RenderFillRect(renderer, &progress_bar_rect);
    }
    void quit() override {}

    bool order_loop_does_go_after(const triolib::Module*) const override { return true; }
} REGISTER_MOD(LoadingScreenImpl, MainRenderer);
