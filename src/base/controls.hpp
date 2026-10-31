#pragma once

#include <SDL3/SDL_scancode.h>
#include <base/input_handler.hpp>

struct Control {
    virtual std::string vname() const { return typeid(*this).name(); }
    virtual ~Control() = default;
    Control(InputHandler& input_handler) : input_handler(input_handler) {}
    InputHandler& input_handler;
    virtual operator bool() const = 0;
    virtual void update() = 0;
};

struct PushCtl : Control {
    tClass;
    float threshold = 0.5f;
    PushCtl(InputHandler& input_handler, float threshold = 0.5f) : Control(input_handler), threshold(threshold) {}

    operator bool() const final override { return get_pressure() >= threshold; }
    virtual float get_pressure() const = 0;
    void update() override {}
};

struct ToggleCtl : Control {
    tClass;
    ToggleCtl(InputHandler& input_handler, uint toggle_amount, uint default_amount = 0)
        : Control(input_handler), toggle_amount(toggle_amount), toggle_count(default_amount) {}
    // toggle_count % toggle_amount to get the current state
    uint toggle_count = 0;
    // E.g inventory = 2 (open and closed), perspective = 3 (first, second, third)
    uint toggle_amount;

    operator bool() const override { return operator()(0); }

    inline bool operator()(uint amount) const { return (toggle_count % toggle_amount) == amount; }

    void update() override { toggle_count = toggle_count % toggle_amount; }
};

struct KbToggleCtl : ToggleCtl {
    tClass;
    KbToggleCtl(InputHandler& input_handler, SDL_Scancode scancode, uint default_amount = 0, uint toggle_amount = 2)
        : scancode(scancode), ToggleCtl(input_handler, toggle_amount, default_amount) {}
    SDL_Scancode scancode;
    void update() override {
        if (input_handler.key_states[scancode].just_pressed()) toggle_count++;
    }
};

struct KbPushCtl : PushCtl {
    tClass;
    SDL_Scancode scancode;
    KbPushCtl(InputHandler& input_handler, SDL_Scancode scancode, float threshold = 0.5f)
        : PushCtl(input_handler, threshold), scancode(scancode) {}

    float get_pressure() const override { return input_handler.key_states[scancode].pushed() ? 1.0f : 0.0f; }
};

struct MousePushCtl : PushCtl {
    tClass;
    float sensitivity = 1.0f;
    enum Direction { UP, DOWN, LEFT, RIGHT } direction;
    MousePushCtl(InputHandler& input_handler, Direction direction, float sensitivity = 1.0f)
        : PushCtl(input_handler), direction(direction), sensitivity(sensitivity) {}

    float get_pressure() const override {
        switch (direction) {
            case UP:
                return input_handler.mouse_moved.up;
            case DOWN:
                return input_handler.mouse_moved.down;
            case LEFT:
                return input_handler.mouse_moved.left;
            case RIGHT:
                return input_handler.mouse_moved.right;
            default:
                return false;
        }
    }
};

struct MouseScrollCtl : PushCtl {
    tClass;
    float sensitivity = 1.0f;
    enum Direction { UP, DOWN } direction;
    MouseScrollCtl(InputHandler& input_handler, Direction direction, float sensitivity = 1.0f)
        : PushCtl(input_handler), direction(direction), sensitivity(sensitivity) {}

    float get_pressure() const override {
        switch (direction) {
            case UP:
                return input_handler.mouse_scroll > 0 ? input_handler.mouse_scroll * sensitivity : 0.0f;
            case DOWN:
                return input_handler.mouse_scroll < 0 ? -input_handler.mouse_scroll * sensitivity : 0.0f;
            default:
                return false;
        }
    }
};

struct Controls : triolib::AppModule {
    tClass;

    std::map<std::string, std::unique_ptr<Control>> controls;

    Control& operator[](const std::string& name) {
        if (controls.contains(name)) {
            return *controls.at(name);
        }
        throw std::runtime_error("Control not found: " + name);
    }

    template <typename T>
    inline T& get(const std::string& name) {
        Control& ctl = (*this)[name];
        T* ptr = dynamic_cast<T*>(&ctl);
        if (!ptr) {
            throw std::runtime_error(
                "Control " + name + "'s requested type (" + T::sname() + ") is not of type " + ctl.vname());
        }
        return *ptr;
    }

    InputHandler* input_handler = nullptr;
};
