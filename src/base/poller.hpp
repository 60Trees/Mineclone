#pragma once

#include "fps_handler.hpp"
#include <triolib.hpp>

struct Poller : triolib::AppModule {
    void init() override {}
    void loop() override {
        time_since_last_poll += parent->get<FpsHandler>().dt;
        if (time_since_last_poll >= poll_interval()) {
            poll();
            time_since_last_poll -= poll_interval();
        }
    }
    void quit() override {}

    virtual void poll() = 0;
    virtual double poll_interval() = 0;
    double time_since_last_poll = 0.f;
};
