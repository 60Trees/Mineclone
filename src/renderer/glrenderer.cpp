#include "glrenderer.hpp"
#include <opengl.hpp>
#include <unordered_set>

using namespace std;
using namespace triolib;

struct MainGlRendererImpl : MainGlRenderer {
    tImplClass(MainGlRenderer);

    MainGlRendererImpl() : MainGlRenderer() {}
    glm::vec<2, int> size;

    unordered_set<string_view> gl_extensions{};

    void init() override {
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
        opengl::gl_log << "Glad return val: " << loaded;
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

        // Initialize all the renderers
        load_all_mods();
        for (auto* mod : init_mod_order) {
            GlRenderer* glmod = dynamic_cast<GlRenderer*>(mod);
            if (glmod) glmod->main_renderer = this;
            mod->init();
        }
    }

    void loop() override {
        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(window, &width, &height);
        glViewport(0, 0, width, height);

        SubApp::loop();

        SDL_GL_SwapWindow(window);
    }
    void quit() override {
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
    bool has_extension(std::string_view ext) const override { return gl_extensions.contains(ext); }

    const glm::vec<2, int>& window_size() const override { return size; }

} REGISTER_SUBAPP(MainGlRendererImpl);
