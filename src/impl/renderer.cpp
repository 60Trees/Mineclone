#include <base/fps_handler.hpp>
#include <base/input_handler.hpp>
#include <base/poller.hpp>
#include <base/renderer.hpp>
#include <base/resources.hpp>
#include <iostream>
#include <unordered_set>
#include <vector>

#include <opengl.hpp>

BigInt Position::snap_distance = 1000;
float Position::snap_distancef = static_cast<float>(Position::snap_distance);

struct WindowImpl : Window {
  SDL_Window *window = nullptr;
  SDL_GLContext glcontext = nullptr;
  std::unordered_set<std::string_view> gl_extensions{};
  glm::vec<2, int> size{};
  bool quit_called = false;

  SDL_Window *sdl_window() const override { return window; }
  glm::vec<2, int> window_size() const override { return size; }
  void set_window_size(glm::vec<2, int> value) override { size = value; }
  bool has_extension(std::string_view ext) const override {
    return gl_extensions.contains(ext);
  }

  void init() override {
    quit_called = false;
    tassertmsg(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
    set_opengl_version();

    const auto title = application ? application->name : std::string{"Window"};
    window = SDL_CreateWindow(title.c_str(), 1280, 720,
                              SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    tassertmsg(window, SDL_GetError());

    glcontext = SDL_GL_CreateContext(window);
    tassertmsg(glcontext, SDL_GetError());
    SDL_GetWindowSizeInPixels(window, &size.x, &size.y);

#ifndef __EMSCRIPTEN__
    const int loaded = gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress);
    std::cout << "glad load result: " << loaded << "\n";
    tassert(loaded);
#endif

    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    gl_extensions.clear();
    gl_extensions.reserve(count);
    for (GLint i = 0; i < count; ++i)
      gl_extensions.emplace(
          reinterpret_cast<const char *>(glGetStringi(GL_EXTENSIONS, i)));

    std::cout << "OpenGL " << glGetString(GL_VERSION) << ":\n";
    std::cout << "- Vendor: " << glGetString(GL_VENDOR) << '\n';
    std::cout << "- Renderer: " << glGetString(GL_RENDERER) << '\n';
    std::cout << "- GLSL: " << glGetString(GL_SHADING_LANGUAGE_VERSION) << '\n';
    std::cout << "- Has " << gl_extensions.size() << " extensions.\n";
    SubApp::init();
  }

  void loop() override { SubApp::loop(); }

  void quit() override {
    if (quit_called)
      return;
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
  void digest_event(const SDL_Event &event) {
    if (event.type != SDL_EVENT_WINDOW_RESIZED)
      return;
    auto &window = static_cast<Window &>(*parent);
    window.set_window_size({event.window.data1, event.window.data2});
  }
} REGISTER_MOD(WindowResizeHandler, Window);

#define UNIFORM_DEF(name, func)                                                \
  Uniform {                                                                    \
    name, [](Uniform &u, GameRenderer &r) {                                    \
      const auto b = u.base.id;                                                \
      func;                                                                    \
    }                                                                          \
  }
#define ADD_UNIFORM(name, func) uniforms.push_back(UNIFORM_DEF(name, func))

struct GameRenderer : Renderer {
  FpsHandler *fps = nullptr;
  Window *window = nullptr;
  ResourceHandler::Resources *resources = nullptr;
  GLuint texture = 0;
  GLint texture_location = -1;
  bool should_resize = true;

  struct Uniform {
    std::string name;
    void (*update)(Uniform &, GameRenderer &);
    opengl::ShaderUniform base;
  };

  std::vector<float> vertices{
      -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
       1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
       1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
       1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
      -1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
      -1.0f, -1.0f, 0.0f, 0.0f, 0.0f};
  std::vector<Uniform> uniforms;
  opengl::ShaderProgram shaderprogram;
  opengl::VBO vbo;
  opengl::VAO vao;

  void init() override {
    window = &static_cast<Window &>(*parent);
    fps = &parent->application->get<FpsHandler>();
    resources = &parent->application->get<ResourceHandler>().resources;

    const auto paths = resources->list_files("assets/minecraft/textures/block/");
    resources->define_atlas("assets/textures/block", paths);
    resources->load_all_atlases();
    const auto &atlases = resources->get_atlases("assets/textures/block");
    if (atlases.empty())
      throw std::runtime_error("Block texture atlas is empty.");
    const auto &atlas = atlases.front();
    const auto &texture_path = atlas.locations.begin()->first;
    const auto rect = atlas[texture_path];
    const auto width = static_cast<float>(atlas.image.width);
    const auto height = static_cast<float>(atlas.image.height);
    const float u0 = 0.0f;
    const float u1 = 1.0f;
    const float v0 = 0.0f;
    const float v1 = 1.0f;
    const auto first_pixel = atlas.image.pixels.data();
    std::cout << "[DEBUG] Atlas " << atlas.image.width << "x"
              << atlas.image.height << ", texture " << texture_path
              << ", first pixel "
              << static_cast<unsigned int>(std::to_integer<unsigned char>(first_pixel[0]))
              << ", " << static_cast<unsigned int>(std::to_integer<unsigned char>(first_pixel[1]))
              << ", " << static_cast<unsigned int>(std::to_integer<unsigned char>(first_pixel[2]))
              << ", UV " << u0 << "," << v0 << " -> " << u1 << "," << v1 << '\n';
    vertices = {-1.0f, -1.0f, 0.0f, u0, v0, 1.0f, -1.0f, 0.0f, u1, v0,
                1.0f, 1.0f, 0.0f, u1, v1, 1.0f, 1.0f, 0.0f, u1, v1,
                -1.0f, 1.0f, 0.0f, u0, v1, -1.0f, -1.0f, 0.0f, u0, v0};

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, atlas.image.width, atlas.image.height,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, atlas.image.pixels.data());
    const auto upload_error = glGetError();
    if (upload_error != GL_NO_ERROR)
      throw std::runtime_error("Failed to upload texture atlas: " +
                               std::to_string(upload_error));

    ADD_UNIFORM("u_time",
                glUniform1ui(b, static_cast<unsigned int>(r.fps->ticks)));
    ADD_UNIFORM("u_windowsize", glUniform2i(b, r.window->window_size().x,
                                            r.window->window_size().y));

    vao.create();
    vao.bind();

    vbo.create();
    vbo.bind();
    vbo.data<float>(vertices, GL_STATIC_DRAW);

    vao.vertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), 0);
    vao.enableAttribute(0);
    vao.vertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                            3 * sizeof(float));
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

    for (auto &uniform : uniforms) {
      uniform.base.id = shaderprogram.id;
      uniform.base.init(uniform.name.c_str());
    }
    glUseProgram(shaderprogram.id);
    texture_location = glGetUniformLocation(shaderprogram.id, "u_texture");
    if (texture_location < 0)
      throw std::runtime_error("Texture sampler uniform was not found.");
    glUniform1i(texture_location, 0);
  }

  void loop() override {
    auto &window = *this->window;
    glClearColor(1.f, 0.05f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    shaderprogram.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(texture_location, 0);
    for (auto &uniform : uniforms)
      uniform.update(uniform, *this);
    vao.bind();
    glDrawArrays(GL_TRIANGLES, 0, 6);
    if (should_resize) {
      const auto size = window.window_size();
      glViewport(0, 0, size.x, size.y);
      should_resize = false;
    }
    SDL_GL_SwapWindow(window.sdl_window());
  }

  void quit() override {
    if (texture) {
      glDeleteTextures(1, &texture);
      texture = 0;
    }
    shaderprogram.destroy();
  }
} REGISTER_MOD(GameRenderer, Window);
