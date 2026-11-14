#pragma once

#include <triolib.hpp>

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

struct BaseRendererModule;
struct BaseRenderer;

/// The main renderer class. It is a subapp
/// that owns the SDL window
struct MainRenderer : triolib::SubApp {
    tClass;
    SDL_Window* window = nullptr;

    virtual const glm::vec<2, int>& window_size() const = 0;

    std::string backend;
    std::string shader_backend;
    std::string version;
    std::string shader_version;

    virtual bool has_extension(std::string_view ext) const = 0;

    std::map<size_t, std::unique_ptr<BaseRenderer>> renderers;
};

/// Override this class to implement a renderer.
/// E.g for ui, ingame stuff, etc.
struct BaseRenderer : triolib::BaseClass, triolib::Ordered<BaseRenderer> {
    tClass;
    MainRenderer* renderer = nullptr;
    BaseRendererModule* _parent_module = nullptr;
};

/// Use this class to add a \ref BaseRenderer
/// to the \ref MainRenderer . It is a module
/// that is added to the \ref MainRenderer 's module list.
struct BaseRendererModule : triolib::Module {
    tClass;
    MainRenderer* renderer = nullptr;
    void init() final override {
        tassert(parent);
        renderer = dynamic_cast<MainRenderer*>(parent);
        tassertmsgerr(renderer, "BaseRendererModule must be a child of MainRenderer", std::logic_error);
    }
    void loop() final override {}
    void quit() final override {}

    /// e.g for a renderer that draws blocks, it has to
    /// be disabled if the world subapp is not active
    virtual bool is_active() { return true; }

    /// e.g `renderer->shader_backend == "GLSL"`
    virtual bool is_compatible() { return true; }

    virtual std::unique_ptr<BaseRenderer> create_renderer() = 0;
};

#define REGISTER_RENDERER_MODULE(classname, active, compatible, on_construct)     \
    ;                                                                             \
    struct classname##Module : BaseRendererModule {                               \
        tClass;                                                                   \
        std::unique_ptr<BaseRenderer> create_renderer() override {                \
            auto retval = std::make_unique<classname>();                          \
            retval->_parent_module = this;                                        \
            retval->renderer = this->renderer;                                    \
            {                                                                     \
                on_construct;                                                     \
            }                                                                     \
            return retval;                                                        \
        }                                                                         \
        bool is_active() override active bool is_compatible() override compatible \
    } REGISTER_MOD(classname##Module, MainRenderer);

/**
 * `init()` = run before loop
 * `loop()` = render a frame
 * `quit()` = run after loop
 */
