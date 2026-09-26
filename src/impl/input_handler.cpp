#include <base/input_handler.hpp>

using namespace triolib;
using namespace std;

struct EventPoller : AppModule {
    vector<InputHandler*> input_handlers{};

    void init() override { input_handlers = parent->get_mods<InputHandler>(); }

    bool does_go_before_loop(const AppModule& other) const override { return true; }
    float get_priority_loop() const override { return 10.0f; }
    void loop() override {
        for (SDL_Event event; SDL_PollEvent(&event);) {
            if (event.type == SDL_EVENT_QUIT) parent->done = true;
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) parent->done = true;
            for (auto* mod : input_handlers) mod->digest_event(event);
        }
    }

    void quit() override {}
} REGISTER_MOD(EventPoller);

