#include <opengl.hpp>
#include "base/resources.hpp"
#include <renderer/glrenderer.hpp>
#include <structures/assets/models/block.hpp>
#include <base/controls.hpp>
#include <world/worldrenderer.hpp>

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <span>
#include <vector>

using namespace std;

struct TestRenderer : MainWorldRenderer::Renderer {
    tClass;

    opengl::VBO vbo;
    opengl::VAO vao;
    opengl::VBO debug_vbo;
    opengl::VAO debug_vao;

    opengl::ShaderProgram shaderProgram;
    opengl::Texture texture;

    struct RenderChunk {
        Vec3i position;
        std::vector<BlockVertex> mesh;
    };

    const ResourceHandler::Resources::Atlas* atlas = nullptr;
    GLint view_projection_location = -1;
    GLint atlas_size_location = -1;

    constexpr static std::array<float, 32> debug_square = {
        -0.5f, 0.01f, -0.5f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f,
         0.5f, 0.01f, -0.5f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f,
         0.5f, 0.01f,  0.5f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f,
        -0.5f, 0.01f,  0.5f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f,
    };

    constexpr static std::string_view vertexShaderSource = R"(
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec3 aColor;
        layout (location = 2) in vec2 aTexCoord;

        out vec3 ourColor;
        out vec2 TexCoord;
        uniform mat4 viewProjection;
        uniform vec2 atlasSize;

        void main() {
            gl_Position = viewProjection * vec4(aPos, 1.0);
            ourColor = aColor;
            TexCoord = aTexCoord / atlasSize;
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
        {  // Texture stuff
            texture.create();
            texture.bind();
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

            atlas =
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
        vbo.data(nullptr, 0, GL_DYNAMIC_DRAW);

        debug_vbo.create();
        debug_vbo.bind();
        debug_vbo.data(std::span<const float>(debug_square), GL_STATIC_DRAW);

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

        shaderProgram.use();
        view_projection_location = shaderProgram.get_uniform_location("viewProjection", true);
        atlas_size_location = shaderProgram.get_uniform_location("atlasSize", true);

        vao.create();
        vao.bind();
        vao.vertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), 0);
        vao.enableAttribute(0);
        vao.vertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), 3 * sizeof(float));
        vao.enableAttribute(1);
        vao.vertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), 6 * sizeof(float));
        vao.enableAttribute(2);

        debug_vao.create();
        debug_vao.bind();
        debug_vbo.bind();
        debug_vao.vertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), 0);
        debug_vao.enableAttribute(0);
        debug_vao.vertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), 3 * sizeof(float));
        debug_vao.enableAttribute(1);
        debug_vao.vertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), 6 * sizeof(float));
        debug_vao.enableAttribute(2);
    }

    void loop() override {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glEnable(GL_DEPTH_TEST);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        const auto& camera = parent->world->camera;
        const auto window_size = parent->application->get_subapp<MainRenderer>().window_size();
        const float aspect = static_cast<float>(window_size.x) / static_cast<float>(window_size.y);
        const float pitch = glm::radians(static_cast<float>(camera.pitch));
        const float yaw = glm::radians(static_cast<float>(camera.yaw));
        const glm::vec3 direction{
            std::cos(pitch) * std::cos(yaw),
            std::sin(pitch),
            std::cos(pitch) * std::sin(yaw),
        };
        const glm::mat4 view_projection = glm::perspective(glm::radians(70.0f), aspect, 0.05f, 4096.0f) *
            glm::lookAt(glm::vec3(camera.x, camera.y, camera.z),
                glm::vec3(camera.x, camera.y, camera.z) + direction, glm::vec3(0.0f, 1.0f, 0.0f));

        std::vector<RenderChunk> render_chunks;
        {
            std::shared_lock chunks_lock(parent->world->chunks_mutex);
            render_chunks.reserve(parent->world->chunks.size());
            for (auto& [position, chunk] : parent->world->chunks) {
                std::unique_lock chunk_lock(chunk.mutex);
                if (!chunk.mesh.empty()) render_chunks.push_back({position, std::move(chunk.mesh)});
            }
        }

        const glm::dvec3 camera_position{camera.x, camera.y, camera.z};
        std::sort(render_chunks.begin(), render_chunks.end(), [&](const RenderChunk& lhs, const RenderChunk& rhs) {
            const glm::dvec3 lhs_center{
                lhs.position.x * CHUNK_SIZE + CHUNK_SIZE / 2.0,
                lhs.position.y * CHUNK_SIZE + CHUNK_SIZE / 2.0,
                lhs.position.z * CHUNK_SIZE + CHUNK_SIZE / 2.0,
            };
            const glm::dvec3 rhs_center{
                rhs.position.x * CHUNK_SIZE + CHUNK_SIZE / 2.0,
                rhs.position.y * CHUNK_SIZE + CHUNK_SIZE / 2.0,
                rhs.position.z * CHUNK_SIZE + CHUNK_SIZE / 2.0,
            };
            return glm::dot(lhs_center - camera_position, lhs_center - camera_position) >
                glm::dot(rhs_center - camera_position, rhs_center - camera_position);
        });

        std::vector<float> vertices;
        for (const auto& render_chunk : render_chunks) {
            const float chunk_x = static_cast<float>(render_chunk.position.x * CHUNK_SIZE);
            const float chunk_y = static_cast<float>(render_chunk.position.y * CHUNK_SIZE);
            const float chunk_z = static_cast<float>(render_chunk.position.z * CHUNK_SIZE);
            for (const auto& vertex : render_chunk.mesh) {
                vertices.insert(vertices.end(), {
                    chunk_x + vertex.x, chunk_y + vertex.y, chunk_z + vertex.z,
                    vertex.lighting / 65535.0f, vertex.lighting / 65535.0f, vertex.lighting / 65535.0f,
                    static_cast<float>(vertex.u0), static_cast<float>(vertex.v1),
                });
            }
        }

        shaderProgram.use();
        texture.bind();
        glUniformMatrix4fv(view_projection_location, 1, GL_FALSE, glm::value_ptr(view_projection));
        glUniform2f(atlas_size_location, static_cast<float>(atlas->image.width), static_cast<float>(atlas->image.height));
        if (!vertices.empty()) {
            vao.bind();
            vbo.bind();
            vbo.data(std::span<const float>(vertices), GL_DYNAMIC_DRAW);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size() / 8));
        }

        // Removable origin marker for camera and chunk-coordinate debugging.
        debug_vao.bind();
        debug_vbo.bind();
        glDrawArrays(GL_LINE_LOOP, 0, 4);

        for (auto& render_chunk : render_chunks) {
            std::shared_lock chunks_lock(parent->world->chunks_mutex);
            auto chunk = parent->world->chunks.find(render_chunk.position);
            if (chunk == parent->world->chunks.end()) continue;
            std::unique_lock chunk_lock(chunk->second.mutex);
            if (chunk->second.mesh.empty()) chunk->second.mesh = std::move(render_chunk.mesh);
        }
    }
    void quit() override {}
} REGISTER_MOD(TestRenderer, MainWorldRenderer);
