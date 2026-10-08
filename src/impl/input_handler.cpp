#include <base/input_handler.hpp>

using namespace triolib;
using namespace std;

struct EventPoller : AppModule {
    tClass;
    vector<InputHandler*> input_handlers{};

    void init() override { input_handlers = parent->get_mods<InputHandler>(); }

    bool order_loop_does_go_before(const AppModule*) const override { return true; }
    double loop_order_priority() const override { return 10.0; }
    void loop() override {
        for (SDL_Event event; SDL_PollEvent(&event);) {
            if (event.type == SDL_EVENT_QUIT) parent->done = true;
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) parent->done = true;
            for (auto* mod : input_handlers) mod->digest_event(event);
        }
    }

    void quit() override {}
} REGISTER_MOD(EventPoller);

struct KeyboardInput : InputHandler {
} REGISTER_MOD(KeyboardInput);
