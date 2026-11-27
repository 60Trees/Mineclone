#pragma once

#include <triolib.hpp>

#include <map>
#include <string>

/// @brief Bare-bones debug screen. Can add text to corners of the screen
///
/// e.g
/// ```cpp
/// auto& dbg_screen = application->get_subapp<DebugScreen>();
/// dbg_screen.show("FPS", "FPS: " + std::to_string(fps));
/// dbg_screen.show_at(DebugScreen::Corner::BottomRight, "Memory", "Memory: " + std::to_string(memory_usage));
/// ```
struct DebugScreen : triolib::SubApp {
    tClass;

    using Entries = std::map<std::string, std::string>;
    enum class Corner { TopLeft, TopRight, BottomLeft, BottomRight };

    virtual void show(std::string key, std::string value) = 0;
    virtual void show_at(Corner corner, std::string key, std::string value) = 0;
    virtual Entries entries() const = 0;
    virtual Entries entries(Corner corner) const = 0;

    bool show_debug_screen = true;
    Corner corner = Corner::TopLeft;
};
