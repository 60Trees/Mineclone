#include <opengl.hpp>
#include <base/resources.hpp>
#include <renderer/renderer.hpp>
#include <structures/assets/models/block.hpp>
#include <ui/debug_screen.hpp>

#include <algorithm>
#include <map>
#include <mutex>
#include <span>
#include <string>
#include <utility>
#include <vector>

using namespace std;

struct DebugScreenImpl : DebugScreen {
    tImplClass(DebugScreen);

    map<Corner, Entries> values;
    mutable mutex values_mutex;

    void init() override {}
    void loop() override { SubApp::loop(); }
    void quit() override { SubApp::quit(); }

    void show(string key, string value) override {
        show_at(Corner::TopLeft, std::move(key), std::move(value));
    }
    void show_at(Corner corner, string key, string value) override {
        lock_guard lock(values_mutex);
        if (value.empty()) {
            values[corner].erase(key);
            return;
        }
        values[corner].insert_or_assign(std::move(key), std::move(value));
    }
    Entries entries() const override {
        lock_guard lock(values_mutex);
        return entries(Corner::TopLeft);
    }
    Entries entries(Corner corner) const override {
        lock_guard lock(values_mutex);
        const auto it = values.find(corner);
        return it == values.end() ? Entries{} : it->second;
    }
} REGISTER_SUBAPP(DebugScreenImpl);

// assets/minecraft/textures/font/ascii.png
// grid of 16x16 (= 255, the size of the ASCII table) characters, each 8x8 pixels
// each character has a pos of x=(char % 16) y=(char / 16)
// Bold = duplicate character and offset one by 0.5px and the other by -0.5px
// Italic = move all top vertices right by 0.5px and all bottom vertices left by 0.5px
// Underline = add a solid line 1 pixel think, character width long, 1 pixel below the character
struct UI_RendererGl : BaseRenderer {
    tClass;

    opengl::VBO vbo;
    opengl::VAO vao;
    opengl::ShaderProgram shaderProgram;
    opengl::Texture texture;
    DebugScreen* debug_screen = nullptr;
    GLint screen_size_location = -1;
    GLint textured_location = -1;
    GLint opacity_location = -1;
    float gui_scale = 3.0f;

    // clang-format off
    vector<float> vertices {
        0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f
    };
    // clang-format on

    constexpr static std::string_view vertexShaderSource = R"(#version auto
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec3 aColor;
        layout (location = 2) in vec2 aTexCoord;
        uniform vec2 screenSize;

        out vec3 ourColor;
        out vec2 TexCoord;

        void main() {
            vec2 ndc = vec2(aPos.x / screenSize.x * 2.0 - 1.0, 1.0 - aPos.y / screenSize.y * 2.0);
            gl_Position = vec4(ndc, 0.0, 1.0);
            ourColor = aColor;
            TexCoord = aTexCoord;
        })";
    constexpr static std::string_view fragmentShaderSource = R"(#version auto
        out vec4 FragColor;

        in vec3 ourColor;
        in vec2 TexCoord;

        uniform sampler2D ourTexture;
        uniform bool textured;
        uniform float opacity;

        void main() {
            vec4 color = textured ? texture(ourTexture, TexCoord) : vec4(1.0);
            FragColor = vec4(ourColor, textured ? color.a : opacity);
        })";

    void init() override {
        {
            texture.create();
            texture.bind();
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            ResourceHandler::Image img = resource_handler->resources.get("minecraft/textures/font/ascii.png").image();
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
            screen_size_location = glGetUniformLocation(shaderProgram, "screenSize");
            textured_location = glGetUniformLocation(shaderProgram, "textured");
            opacity_location = glGetUniformLocation(shaderProgram, "opacity");
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
        if (!debug_screen || !debug_screen->show_debug_screen) return;

        vector<float> background_vertices;
        vector<float> shadow_vertices;
        vector<float> text_vertices;
        const float glyph_width = 8.0f * gui_scale;
        const float glyph_height = 8.0f * gui_scale;
        const float shadow_offset = gui_scale;
        const float margin = 8.0f;
        for (const auto corner : {DebugScreen::Corner::TopLeft, DebugScreen::Corner::TopRight,
             DebugScreen::Corner::BottomLeft, DebugScreen::Corner::BottomRight}) {
        const auto entries = debug_screen->entries(corner);

        float block_width = 0.0f;
        float block_height = 0.0f;
        for (const auto& [key, value] : entries) {
            const string text = key + ": " + value;
            size_t line_length = 0;
            size_t max_line_length = 0;
            size_t line_count = 1;
            for (const char character : text) {
                if (character == '\n') {
                    max_line_length = std::max(max_line_length, line_length);
                    line_length = 0;
                    ++line_count;
                } else {
                    ++line_length;
                }
            }
            max_line_length = std::max(max_line_length, line_length);
            block_width = std::max(block_width, static_cast<float>(max_line_length) * glyph_width);
            block_height += static_cast<float>(line_count) * glyph_height;
        }

        const auto window_size = renderer->window_size();
        const float screen_width = static_cast<float>(window_size.x);
        const float screen_height = static_cast<float>(window_size.y);
        const bool right_aligned =
            corner == DebugScreen::Corner::TopRight || corner == DebugScreen::Corner::BottomRight;
        const bool bottom_aligned =
            corner == DebugScreen::Corner::BottomLeft || corner == DebugScreen::Corner::BottomRight;
        const float left = right_aligned ? screen_width - margin - block_width : margin;
        float y = bottom_aligned ? screen_height - margin - block_height : margin;

        for (const auto& [key, value] : entries) {
            const string text = key + ": " + value;
            float x = left;
            for (const char character : text) {
                if (character == '\n') {
                    x = left;
                    y += glyph_height;
                    continue;
                }
                const auto glyph = static_cast<unsigned char>(character);
                const float u0 = static_cast<float>(glyph % 16) / 16.0f;
                const float v0 = static_cast<float>(glyph >> 4) / 16.0f;
                const float u1 = u0 + 1.0f / 16.0f;
                const float v1 = v0 + 1.0f / 16.0f;
                const float x1 = x + glyph_width;
                const float y1 = y + glyph_height;
                background_vertices.insert(background_vertices.end(), {
                    x, y, 0.0f, 0.3f, 0.3f, 0.3f, 0.0f, 0.0f,
                    x1, y, 0.0f, 0.3f, 0.3f, 0.3f, 0.0f, 0.0f,
                    x1, y1, 0.0f, 0.3f, 0.3f, 0.3f, 0.0f, 0.0f,
                    x, y, 0.0f, 0.3f, 0.3f, 0.3f, 0.0f, 0.0f,
                    x1, y1, 0.0f, 0.3f, 0.3f, 0.3f, 0.0f, 0.0f,
                    x, y1, 0.0f, 0.3f, 0.3f, 0.3f, 0.0f, 0.0f,
                });
                shadow_vertices.insert(shadow_vertices.end(), {
                    x + shadow_offset, y + shadow_offset, 0.0f, 0.0f, 0.0f, 0.0f, u0, v0,
                    x1 + shadow_offset, y + shadow_offset, 0.0f, 0.0f, 0.0f, 0.0f, u1, v0,
                    x1 + shadow_offset, y1 + shadow_offset, 0.0f, 0.0f, 0.0f, 0.0f, u1, v1,
                    x + shadow_offset, y + shadow_offset, 0.0f, 0.0f, 0.0f, 0.0f, u0, v0,
                    x1 + shadow_offset, y1 + shadow_offset, 0.0f, 0.0f, 0.0f, 0.0f, u1, v1,
                    x + shadow_offset, y1 + shadow_offset, 0.0f, 0.0f, 0.0f, 0.0f, u0, v1,
                });
                text_vertices.insert(text_vertices.end(), {
                    x, y, 0.0f, 1.0f, 1.0f, 1.0f, u0, v0,
                    x1, y, 0.0f, 1.0f, 1.0f, 1.0f, u1, v0,
                    x1, y1, 0.0f, 1.0f, 1.0f, 1.0f, u1, v1,
                    x, y, 0.0f, 1.0f, 1.0f, 1.0f, u0, v0,
                    x1, y1, 0.0f, 1.0f, 1.0f, 1.0f, u1, v1,
                    x, y1, 0.0f, 1.0f, 1.0f, 1.0f, u0, v1,
                });
                x = x1;
            }
            y += glyph_height;
        }
        }

        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        shaderProgram.use();
        glUniform2f(screen_size_location, static_cast<float>(renderer->window_size().x),
            static_cast<float>(renderer->window_size().y));
        vao.bind();
        vbo.bind();
        texture.bind();
        vbo.data(std::span<const float>(background_vertices), GL_DYNAMIC_DRAW);
        glUniform1i(textured_location, GL_FALSE);
        glUniform1f(opacity_location, 0.5f);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(background_vertices.size() / 8));
        vbo.data(std::span<const float>(shadow_vertices), GL_DYNAMIC_DRAW);
        glUniform1i(textured_location, GL_TRUE);
        glUniform1f(opacity_location, 1.0f);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(shadow_vertices.size() / 8));
        vbo.data(std::span<const float>(text_vertices), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(text_vertices.size() / 8));
        glDisable(GL_BLEND);
    }
    void quit() override {}

    bool order_loop_does_go_after(const BaseRenderer* oth) const override { return true; }

    ResourceHandler* resource_handler = nullptr;
} REGISTER_RENDERER_MODULE(
    UI_RendererGl,
    {
        if (!parent->application->getmod<ResourceHandler>().has_loaded) return false;
        return true;
    },
    { return renderer->backend == "OpenGL" && renderer->shader_backend == "GLSL"; },
    {
        retval->resource_handler = &parent->application->getmod<ResourceHandler>();
        retval->debug_screen = &parent->application->load_subapp<DebugScreen>();
    });
