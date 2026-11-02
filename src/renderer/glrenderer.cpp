#include "renderer.hpp"
#include "triolib.hpp"

#include <SDL3/SDL.h>
#include <opengl.hpp>
#include <unordered_set>
#include <cstdlib>
#include <platform.hpp>

using namespace std;
using namespace triolib;

struct MainGlRendererImpl : MainRenderer {
    tImplClass(MainRenderer);

    SDL_GLContext glcontext = nullptr;

    MainGlRendererImpl() {
        backend = "OpenGL";
        shader_backend = "GLSL";
    }

    glm::vec<2, int> size;

    unordered_set<string_view> gl_extensions{};

    void init() override {
        if (!std::getenv("SDL_VIDEO_DRIVER") && std::getenv("WAYLAND_DISPLAY"))
            SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "wayland");
        tassertmsg(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        set_opengl_version();

        const auto title = application ? application->name : string{"Window"};
        window = SDL_CreateWindow(title.c_str(), 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
        tassertmsg(window, SDL_GetError());

        glcontext = SDL_GL_CreateContext(window);
        tassertmsg(glcontext, SDL_GetError());
        SDL_GetWindowSizeInPixels(window, &size.x, &size.y);

#ifndef __EMSCRIPTEN__
        const int loaded = gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress);
        if (DO_GL_DEBUG) opengl::gl_log << "Desktop Glad GL Debugging enabled.\nGlad return val: " << loaded;
#endif

#ifdef DO_GL_DEBUG
        opengl::enable_debugging();
#endif

        GLint count = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &count);
        gl_extensions.clear();
        gl_extensions.reserve(count);
        for (GLint i = 0; i < count; ++i)
            gl_extensions.emplace(reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, i)));

        version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        shader_version = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));

        opengl::gl_log << "OpenGL " << glGetString(GL_VERSION) << "\nVendor: " << glGetString(GL_VENDOR)
                       << "\nRenderer: " << glGetString(GL_RENDERER)
                       << "\nGLSL: " << glGetString(GL_SHADING_LANGUAGE_VERSION) << "\nHas " << gl_extensions.size()
                       << " extensions.";
        console.info << "OS: " OS_TRIPLET << " (" << sizeof(void*) * 8
                     << "-bit)\n"
                        "\nSDL version "
                     << SDL_GetVersion() << "\n"
                     << SDL_GetCurrentVideoDriver() << " video driver\n"
                     << SDL_GetNumLogicalCPUCores() << " CPU count\n"
                     << SDL_GetSystemRAM() << " MiB RAM\n"
                     << SDL_GetCPUCacheLineSize() << " byte cache line size\n";

        // Initialize all the renderers

        load_all_mods();
        for (auto* mod : init_mod_order) {
            mod->init();
            auto* renderermod = dynamic_cast<BaseRendererModule*>(mod);
            if (!renderermod) continue;
            if (!renderermod->is_compatible()) {
                opengl::gl_err << "Renderer module " << mod->vname() << " is not compatible with renderer " << vname()
                               << " (" << this->backend << " " << this->shader_backend << ")";
                continue;
            }
            if (!renderermod->is_active()) continue;
            auto renderer = renderermod->create_renderer();
            renderer->init();
            auto ptr = renderer.get();
            size_t hash = typeid(*ptr).hash_code();
            opengl::gl_log << "Activated renderer: " << renderer->vname();
            renderers.insert({hash, std::move(renderer)});
        }
    }

    void loop() override {
        SDL_GetWindowSizeInPixels(window, &size.x, &size.y);
        glViewport(0, 0, size.x, size.y);

        for (auto* mod : loop_mod_order) mod->loop();

        // 1. Add new active renderers
        for (auto* mod : loop_mod_order) {
            auto* renderermod = dynamic_cast<BaseRendererModule*>(mod);
            if (!renderermod) continue;
            size_t hash = typeid(*renderermod).hash_code();

            if (renderers.contains(hash)) continue;
            if (!renderermod->is_compatible()) continue;
            if (!renderermod->is_active()) continue;

            auto renderer = renderermod->create_renderer();
            renderer->init();
            opengl::gl_log << "Activated renderer: " << renderer->vname();
            renderers.insert({hash, std::move(renderer)});
        }

        // 2. Remove inactive renderers
        for (auto it = renderers.begin(); it != renderers.end();) {
            auto* mod = dynamic_cast<BaseRendererModule*>(it->second->_parent_module);
            if (!mod || !mod->is_active()) {
                opengl::gl_log << "Deactivated renderer: " << it->second->vname();
                it->second->quit();
                it = renderers.erase(it);
            } else {
                it++;
            }
        }

        // 3. Render all active renderers
        for (auto& [__, renderer] : renderers) {
            renderer->loop();
        }

        if (renderers.empty()) run_limited(1) opengl::gl_err << "No active renderers";

        SDL_GL_SwapWindow(window);
    }
    void quit() override {
        for (auto& [_, renderer] : renderers) renderer->quit();
        renderers.clear();
        SubApp::quit();

        if (glcontext) {
            SDL_GL_DestroyContext(glcontext);
            glcontext = nullptr;
        }
        if (window) {
            SDL_DestroyWindow(window);
            window = nullptr;
        }
        SDL_Quit();
    }

    public:
    bool has_extension(string_view ext) const override { return gl_extensions.contains(ext); }

    const glm::vec<2, int>& window_size() const override { return size; }

} REGISTER_SUBAPP(MainGlRendererImpl);
