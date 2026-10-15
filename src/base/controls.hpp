#pragma once

#include <base/input_handler.hpp>

struct Control {
    virtual std::string vname() const { return typeid(*this).name(); }
    virtual ~Control() = default;
    Control(InputHandler& input_handler) : input_handler(input_handler) {}
    InputHandler& input_handler;
    virtual operator bool() const = 0;
};

struct PushCtl : Control {
    tClass;
    float pressure = 0.0f;
    float threshold = 0.5f;
    PushCtl(InputHandler& input_handler, float threshold = 0.5f) : Control(input_handler), threshold(threshold) {}

    operator bool() const final override { return pressure >= threshold; }
    virtual float get_pressure() const = 0;
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
    T& operator[](const std::string& name) {
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
