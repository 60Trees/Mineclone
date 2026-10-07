#include <base/fps_handler.hpp>
#include <base/input_handler.hpp>
#include <base/poller.hpp>
#include <base/renderer.hpp>
#include <base/resources.hpp>
#include <unordered_set>
#include <vector>

#include <opengl.hpp>

BigInt Position::snap_distance = 1000;
float Position::snap_distancef = static_cast<float>(Position::snap_distance);

using namespace std;

struct WindowImpl : Window {
    SDL_Window* window = nullptr;
    SDL_GLContext glcontext = nullptr;
    unordered_set<string_view> gl_extensions{};
    glm::vec<2, int> size{};
    bool quit_called = false;

    SDL_Window* sdl_window() const override { return window; }
    glm::vec<2, int> window_size() const override { return size; }
    void set_window_size(glm::vec<2, int> value) override { size = value; }
    bool has_extension(string_view ext) const override {
        return gl_extensions.contains(ext);
    }

    void init() override {
        quit_called = false;
        tassertmsg(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        set_opengl_version();

        const auto title = application ? application->name : string{"Window"};
        window = SDL_CreateWindow(
            title.c_str(), 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
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
            gl_extensions.emplace(
                reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, i)));

        opengl::gl_log << "OpenGL " << glGetString(GL_VERSION)
                       << "\nVendor: " << glGetString(GL_VENDOR)
                       << "\nRenderer: " << glGetString(GL_RENDERER)
                       << "\nGLSL: " << glGetString(GL_SHADING_LANGUAGE_VERSION)
                       << "\nHas " << gl_extensions.size() << " extensions.";

        SubApp::init();
    }

    void loop() override { SubApp::loop(); }

    void quit() override {
        if (quit_called) return;
        quit_called = true;
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
    ~WindowImpl() override { quit(); }
} REGISTER_SUBAPP(WindowImpl);

struct WindowResizeHandler : triolib::Module {
    void init() override {}
    void quit() override {}
    void digest_event(const SDL_Event& event) {
        if (event.type != SDL_EVENT_WINDOW_RESIZED) return;
        auto& window = static_cast<Window&>(*parent);
        window.set_window_size({event.window.data1, event.window.data2});
    }
} REGISTER_MOD(WindowResizeHandler, Window);

#define UNIFORM_DEF(name, func)                 \
    Uniform {                                   \
        name, [](Uniform& u, GameRenderer& r) { \
            const auto b = u.base.id;           \
            func;                               \
        }                                       \
    }
#define ADD_UNIFORM(name, func) uniforms.push_back(UNIFORM_DEF(name, func))

struct GameRenderer : Renderer {
    FpsHandler* fps = nullptr;
    Window* window = nullptr;
    ResourceHandler::Resources* resources = nullptr;
    opengl::Texture texture;
    GLint texture_location = -1;

    struct UV {
        float u0, u1, v0, v1;
    };

    struct Uniform {
        string name;
        void (*update)(Uniform&, GameRenderer&);
        opengl::ShaderUniform base;
    };

    vector<UV> block_uvs;
    vector<float> vertices;
    vector<Uniform> uniforms;
    opengl::ShaderProgram shaderprogram;
    opengl::VBO vbo;
    opengl::VAO vao;
    size_t displayed_block = 0;

    void set_quad_uv(const UV& uv) {
        vertices = {
            // Top-left, top-right, bottom-right.
            0.0f, 0.0f, 0.0f, uv.u0, uv.v0,
            1.0f, 0.0f, 0.0f, uv.u1, uv.v0,
            1.0f, 1.0f, 0.0f, uv.u1, uv.v1,
            // Bottom-right, bottom-left, top-left.
            1.0f, 1.0f, 0.0f, uv.u1, uv.v1,
            0.0f, 1.0f, 0.0f, uv.u0, uv.v1,
            0.0f, 0.0f, 0.0f, uv.u0, uv.v0,
        };
    }

    void init() override {
        window = &static_cast<Window&>(*parent);
        fps = &parent->application->get<FpsHandler>();
        resources = &parent->application->get<ResourceHandler>().resources;

        const auto paths =
            resources->list_pngs("assets/minecraft/textures/block/");
        resources->define_atlas("assets/textures/block", paths);
        resources->load_all_atlases();
        const auto* atlas = resources->get_atlas("assets/textures/block");
        if (!atlas)
            throw runtime_error("Block texture atlas is empty.");

        const float width = atlas->image.width;
        const float height = atlas->image.height;
        block_uvs.reserve(atlas->locations.size());
        for (const auto& [path, rect] : atlas->locations) {
            block_uvs.push_back({
                static_cast<float>(rect.first.x) / width,
                static_cast<float>(rect.second.x) / width,
                1.0f - static_cast<float>(rect.second.y) / height,
                1.0f - static_cast<float>(rect.first.y) / height,
            });
        }
        if (block_uvs.empty())
            throw runtime_error("Block texture atlas has no textures.");
        set_quad_uv(block_uvs.front());

        {
            texture.create();
            texture.bind();
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, atlas->image.width,
                atlas->image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                atlas->image.pixels.data());

            opengl::handle_error();
        }

        ADD_UNIFORM(
            "u_time", glUniform1ui(b, static_cast<unsigned int>(r.fps->ticks)));
        ADD_UNIFORM("u_windowsize", glUniform2i(b, r.window->window_size().x,
                                        r.window->window_size().y));

        vao.create();
        vao.bind();

        vbo.create();
        vbo.bind();
        vbo.data<float>(vertices, GL_DYNAMIC_DRAW);

        vao.vertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), 0);
        vao.enableAttribute(0);
        vao.vertexAttribPointer(
            1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), 3 * sizeof(float));
        vao.enableAttribute(1);

        opengl::Shader vert{GL_VERTEX_SHADER};
        vert.create();
        vert.source_from_path("assets/vert.glsl");
        vert.compile();

        opengl::Shader frag{GL_FRAGMENT_SHADER};
        frag.create();
        frag.source_from_path("assets/frag.glsl");
        frag.compile();

        shaderprogram.create();
        shaderprogram.attach(vert);
        shaderprogram.attach(frag);
        shaderprogram.link();

        for (auto& uniform : uniforms) {
            uniform.base.id = shaderprogram.id;
            uniform.base.init(uniform.name.c_str());
        }

        shaderprogram.use();
        texture_location =
            shaderprogram.get_uniform_location("u_texture", true);
        glUniform1i(texture_location, 0);
    }

    void loop() override {
        auto& window = *this->window;
        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(window.sdl_window(), &width, &height);
        glViewport(0, 0, width, height);
        const size_t block =
            static_cast<size_t>((fps->ticks / 100) % block_uvs.size());
        if (block != displayed_block) {
            displayed_block = block;
            set_quad_uv(block_uvs[displayed_block]);
            vbo.bind();
            vbo.data<float>(vertices, GL_DYNAMIC_DRAW);
        }
        glClearColor(1.f, 0.05f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        shaderprogram.use();
        glActiveTexture(GL_TEXTURE0);
        texture.bind();
        glUniform1i(texture_location, 0);
        for (auto& uniform : uniforms) uniform.update(uniform, *this);
        vao.bind();
        glDrawArrays(GL_TRIANGLES, 0, 6);
        SDL_GL_SwapWindow(window.sdl_window());
    }

    void quit() override {
        texture.destroy();
        shaderprogram.destroy();
    }
} REGISTER_MOD(GameRenderer, Window);
