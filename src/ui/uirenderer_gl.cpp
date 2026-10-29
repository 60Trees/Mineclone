#include <opengl.hpp>
#include "base/resources.hpp"
#include <renderer/glrenderer.hpp>
#include <structures/assets/models/block.hpp>
#include <world/worldrenderer.hpp>

#include <span>
#include <vector>

using namespace std;

struct UI_RendererGl : GlRenderer {
    tClass;

    opengl::VBO vbo;
    opengl::VAO vao;
    opengl::ShaderProgram shaderProgram;
    opengl::Texture texture;

    // clang-format off
    vector<float> vertices {
        // positions          // colors           // texture coords
         0.5f,  0.5f, 0.0f,   1.0f, 0.0f, 0.0f,   1.0f, 1.0f,   // top right
         0.5f, -0.5f, 0.0f,   0.0f, 1.0f, 0.0f,   1.0f, 0.0f,   // bottom right
        -0.5f, -0.5f, 0.0f,   0.0f, 0.0f, 1.0f,   0.0f, 0.0f,   // bottom left
        -0.5f,  0.5f, 0.0f,   1.0f, 1.0f, 0.0f,   0.0f, 1.0f    // top left 
    };
    // clang-format on

    constexpr static std::string_view vertexShaderSource = R"(#version auto
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
    constexpr static std::string_view fragmentShaderSource = R"(#version auto
        out vec4 FragColor;

        in vec3 ourColor;
        in vec2 TexCoord;

        uniform sampler2D ourTexture;

        void main() {
            FragColor = texture(ourTexture, TexCoord);
        })";

    void init() override {
        {
            texture.create();
            texture.bind();
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            ResourceHandler::Image img = parent->application->getmod<ResourceHandler>()
                                             .resources.get("minecraft/textures/block/dirt.png")
                                             .image();
            glTexImage2D(
                GL_TEXTURE_2D, 0, GL_RGBA, img.width, img.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.bytes().data());
            texture.generateMipmap();
        }

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
        vao.vertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), 0);
        vao.enableAttribute(0);
        vao.vertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), 3 * sizeof(float));
        vao.enableAttribute(1);
        vao.vertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), 6 * sizeof(float));
        vao.enableAttribute(2);
    }

    void loop() override {
        shaderProgram.use();
        vao.bind();
        texture.bind();
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
    void quit() override {}

    bool order_loop_does_go_after(const triolib::Module* oth) const override { return true; }
} REGISTER_MOD(UI_RendererGl, MainWorldRenderer);
