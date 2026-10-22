#include <base/controls.hpp>

struct ControlsImpl : Controls {
    tImplClass(Controls);

    void init() override {
        input_handler = &parent->getmod<InputHandler>();

        controls["forward"] = std::make_unique<KbPushCtl>(*input_handler, SDL_SCANCODE_W);
        controls["backward"] = std::make_unique<KbPushCtl>(*input_handler, SDL_SCANCODE_S);
        controls["left"] = std::make_unique<KbPushCtl>(*input_handler, SDL_SCANCODE_A);
        controls["right"] = std::make_unique<KbPushCtl>(*input_handler, SDL_SCANCODE_D);
        controls["jump"] = std::make_unique<KbPushCtl>(*input_handler, SDL_SCANCODE_SPACE);
        controls["sneak"] = std::make_unique<KbPushCtl>(*input_handler, SDL_SCANCODE_LSHIFT);
        controls["sprint"] = std::make_unique<KbPushCtl>(*input_handler, SDL_SCANCODE_R);
        controls["look_up"] = std::make_unique<MousePushCtl>(*input_handler, MousePushCtl::UP);
        controls["look_down"] = std::make_unique<MousePushCtl>(*input_handler, MousePushCtl::DOWN);
        controls["look_left"] = std::make_unique<MousePushCtl>(*input_handler, MousePushCtl::LEFT);
        controls["look_right"] = std::make_unique<MousePushCtl>(*input_handler, MousePushCtl::RIGHT);
    }
    void loop() override {
        for (auto& [_, control] : controls) control->update();
    }
    void quit() override {}
} REGISTER_MOD(ControlsImpl);
