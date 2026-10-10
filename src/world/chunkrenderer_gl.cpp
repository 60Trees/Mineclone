
#include <opengl.hpp>
#include "world.hpp"
#include <renderer/glrenderer.hpp>
#include <structures/assets/models/block.hpp>

#include <span>
#include <vector>

using namespace std;

struct WorldRenderer : World::Module {
    tClass;

    MainGlRenderer* renderer = nullptr;

    opengl::VBO vbo;
    opengl::VAO vao;
    opengl::ShaderProgram shaderProgram;

    // clang-format off
    vector<float> vertices {
       -0.5f, -0.5f, 0.0f,
        0.5f, -0.5f, 0.0f,
        0.0f,  0.5f, 0.0f
    };
    // clang-format on

    constexpr static std::string_view vertexShaderSource = R"(
        layout (location = 0) in vec3 aPos;
        void main() {
           gl_Position = vec4(aPos.x, aPos.y, aPos.z, 1.0);
        })";
    constexpr static std::string_view fragmentShaderSource = R"(
        out vec4 FragColor;

        void main() {
            FragColor = vec4(1.0f, 0.5f, 0.2f, 1.0f);
        })";

    void init() override {
        World::Module::init();

        vbo.create();
        vbo.bind();
        vbo.data(std::span<const float>(vertices), GL_STATIC_DRAW);

        {
            opengl::Shader vertexShader(GL_VERTEX_SHADER);
            vertexShader.create();
            vertexShader.source_from_strview(vertexShaderSource);
            vertexShader.compile();

            opengl::Shader fragmentShader(GL_FRAGMENT_SHADER);
            fragmentShader.create();
            fragmentShader.source_from_strview(fragmentShaderSource);
            fragmentShader.compile();

            shaderProgram.create();
            shaderProgram.attach(vertexShader);
            shaderProgram.attach(fragmentShader);

            shaderProgram.link();
        }

        vao.create();
        vao.bind();
        vao.vertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), 0);
        vao.enableAttribute(0);
    }

    void loop() override {
        shaderProgram.use();
        vao.bind();
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    void quit() override {}
} REGISTER_MOD(WorldRenderer, World);

struct WorldRendererWrapper : GlRenderer {
    tClass;

    WorldRenderer* worldrenderer = nullptr;

    void init() override {}
    void loop() override {
        if (World* world = parent->application->get_subapp_ptr<World>()) {
            if (!worldrenderer) worldrenderer = &world->getmod<WorldRenderer>();
            worldrenderer->loop();
        } else {
            worldrenderer = nullptr;
            return;
        }
    }
    void quit() override {}
} REGISTER_MOD(WorldRendererWrapper, MainGlRenderer);
