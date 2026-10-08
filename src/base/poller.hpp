#pragma once

#include <triolib.hpp>
#include <base/fps_handler.hpp>

struct Poller {
    /// in seconds
    Poller(double poll_interval_seconds, bool catch_up_missed_polls = true)
        : poll_interval(poll_interval_seconds), poller_catch_up_missed_polls(catch_up_missed_polls) {}
    Poller() = default;

    bool poller_enabled = true;

    protected:
    inline void trypoll(double dt) {
        if (poller_enabled) poller_time_since_last_poll += dt;
        if (poller_time_since_last_poll >= poll_interval) {
            if (poller_enabled) poll();
            if (poller_catch_up_missed_polls)
                poller_time_since_last_poll -= poll_interval;
            else
                poller_time_since_last_poll = 0;
        }
    }
    inline void trypoll(triolib::Application* app) { trypoll(app->find_mod<FpsHandler>()->dt); }
    inline void trypoll(triolib::AppModule* appmod) { trypoll(appmod->parent->find_mod<FpsHandler>()->dt); }
    inline void trypoll(triolib::Module* appmod) { trypoll(appmod->parent->application->find_mod<FpsHandler>()->dt); }
    inline void trypoll(triolib::SubApp* subapp) { trypoll(subapp->application->find_mod<FpsHandler>()->dt); }

    virtual void poll() = 0;
    /// In seconds
    double poll_interval = 1.234;

    double poller_time_since_last_poll = 0.f;
    bool poller_catch_up_missed_polls = true;
};
