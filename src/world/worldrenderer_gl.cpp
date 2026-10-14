#include <opengl.hpp>
#include "base/resources.hpp"
#include <renderer/glrenderer.hpp>
#include <structures/assets/models/block.hpp>
#include <base/controls.hpp>
#include <world/worldrenderer.hpp>

#include <span>
#include <vector>

using namespace std;

struct TestRenderer : MainWorldRenderer::Renderer {
    tClass;

    Controls* controls = nullptr;

    opengl::VBO vbo;
    opengl::VAO vao;
    opengl::EBO ebo;

    opengl::ShaderProgram shaderProgram;
    opengl::Texture texture;

    // clang-format off
    vector<float> vertices {
        // positions          // colors           // texture coords
         1.0f,  1.0f, 0.0f,   1.0f, 1.0f, 1.0f,   1.0f, 0.0f,   // top right
         1.0f, -1.0f, 0.0f,   0.5f, 0.5f, 0.5f,   1.0f, 1.0f,   // bottom right
        -1.0f, -1.0f, 0.0f,   0.3f, 0.3f, 0.3f,   0.0f, 1.0f,   // bottom left
        -1.0f,  1.0f, 0.0f,   0.5f, 0.5f, 0.5f,   0.0f, 0.0f    // top left 
    };

    constexpr static std::array<uint, 6> ebo_indices = {  // note that we start from 0!
        0, 1, 3,   // first triangle
        1, 2, 3    // second triangle
    };
    // clang-format on

    constexpr static std::string_view vertexShaderSource = R"(
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec3 aColor;
        layout (location = 2) in vec2 aTexCoord;

        out vec3 ourColor;
        out vec2 TexCoord;

        void main() {
            gl_Position = vec4(aPos, 1.0);
            ourColor = aColor;
            TexCoord = aTexCoord;
        })";
    constexpr static std::string_view fragmentShaderSource = R"(
        out vec4 FragColor;

        in vec3 ourColor;
        in vec2 TexCoord;

        uniform sampler2D ourTexture;

        void main() {
            FragColor = texture(ourTexture, TexCoord) * vec4(ourColor, 1.0);  
        })";

    void init() override {
        controls = &parent->application->getmod<Controls>();

        {  // Texture stuff
            texture.create();
            texture.bind();
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

            auto* atlas =
                parent->application->getmod<ResourceHandler>().resources.get_atlas("assets/minecraft/textures/block");
            ResourceHandler::Image img = atlas->image;

            // auto img = parent->application->getmod<ResourceHandler>()
            //                .resources.get("minecraft/textures/block/honeycomb_block.png")
            //                .image();

            glTexImage2D(
                GL_TEXTURE_2D, 0, GL_RGBA, img.width, img.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.bytes().data());

            texture.generateMipmap();
        }

        vbo.create();
        vbo.bind();
        vbo.data(std::span<const float>(vertices), GL_STATIC_DRAW);

        ebo.create();
        ebo.bind();
        ebo.data(std::span<const uint>(ebo_indices), GL_STATIC_DRAW);

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
        vao.vertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), 0);
        vao.enableAttribute(0);
        vao.vertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), 3 * sizeof(float));
        vao.enableAttribute(1);
        vao.vertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), 6 * sizeof(float));
        vao.enableAttribute(2);
    }

    void loop() override {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        // Later, with depth testing (apparently): glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        shaderProgram.use();
        vao.bind();
        ebo.bind();
        texture.bind();
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    }
    void quit() override {}
} REGISTER_MOD(TestRenderer, MainWorldRenderer);
