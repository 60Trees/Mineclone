#include <opengl.hpp>
#include "base/fps_handler.hpp"
#include "base/resources.hpp"
#include <renderer/glrenderer.hpp>
#include <base/controls.hpp>
#include <world/worldrenderer.hpp>

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <span>
#include <vector>

struct TestRenderer : MainWorldRenderer::Renderer {
    tClass;

    opengl::VBO vbo;
    opengl::VAO vao;
    opengl::ShaderProgram shader_program;
    opengl::Texture texture;

    const ResourceHandler::Resources::Atlas* atlas = nullptr;
    GLint view_projection_location = -1;
    GLint atlas_size_location = -1;

    constexpr static std::string_view vertex_shader_source = R"(
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
    constexpr static std::string_view fragment_shader_source = R"(
        out vec4 FragColor;
        in vec3 ourColor;
        in vec2 TexCoord;
        uniform sampler2D ourTexture;

        void main() {
            FragColor = texture(ourTexture, TexCoord) * vec4(ourColor, 1.0);
        })";

    void init() override {
        texture.create();
        texture.bind();
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        atlas = parent->application->getmod<ResourceHandler>().resources.get_atlas("assets/minecraft/textures/block");
        if (!atlas) throw std::runtime_error("Block texture atlas is not loaded");
        const auto& image = atlas->image;
        glTexImage2D(
            GL_TEXTURE_2D, 0, GL_RGBA, image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.bytes().data());

        vbo.create();
        vbo.bind();
        vbo.data(nullptr, 0, GL_DYNAMIC_DRAW);

        opengl::Shader vertex_shader(GL_VERTEX_SHADER);
        vertex_shader.create();
        vertex_shader.source_from_strview(vertex_shader_source);
        vertex_shader.compile();
        opengl::Shader fragment_shader(GL_FRAGMENT_SHADER);
        fragment_shader.create();
        fragment_shader.source_from_strview(fragment_shader_source);
        fragment_shader.compile();

        shader_program.create();
        shader_program.attach(vertex_shader);
        shader_program.attach(fragment_shader);
        shader_program.link();
        shader_program.use();
        view_projection_location = shader_program.get_uniform_location("viewProjection", true);
        atlas_size_location = shader_program.get_uniform_location("atlasSize", true);

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
        glEnable(GL_DEPTH_TEST);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        size_t vertex_count = 0;

        const auto& camera = parent->world->camera;
        const auto window_size = parent->application->get_subapp<MainRenderer>().window_size();
        const float aspect = static_cast<float>(window_size.x) / static_cast<float>(window_size.y);
        const float pitch = glm::radians(camera.pitch);
        const float yaw = glm::radians(camera.yaw);
        const glm::vec3 direction{
            std::cos(pitch) * std::cos(yaw),
            std::sin(pitch),
            std::cos(pitch) * std::sin(yaw),
        };
        const glm::mat4 view_projection =
            glm::perspective(glm::radians(70.0f), aspect, 0.05f, 4096.0f) *
            glm::lookAt(glm::vec3(camera.x, camera.y, camera.z), glm::vec3(camera.x, camera.y, camera.z) + direction,
                glm::vec3(0.0f, 1.0f, 0.0f));

        std::vector<float> vertices;
        {
            auto& data = parent->world->mesh_data;
            std::shared_lock meshes_lock(data.meshes_mutex);
            for (PosLodLevel lod = 0; lod < data.meshes.size(); ++lod) {
                std::shared_lock levels_lock(parent->world->levels_mutex);
                for (const auto& [position, mesh] : data.meshes[lod]) {
                    if (lod > 0) {
                        const Vec3i child_origin{position.x * 2, position.y * 2, position.z * 2};
                        bool covered = false;
                        if (lod - 1 < data.meshes.size()) {
                            const auto& higher_meshes = data.meshes[lod - 1];
                            for (int x = 0; x < 2 && !covered; ++x)
                                for (int y = 0; y < 2 && !covered; ++y)
                                    for (int z = 0; z < 2; ++z)
                                        covered &= higher_meshes.contains(
                                            {child_origin.x + x, child_origin.y + y, child_origin.z + z});
                        }
                        if (covered) continue;
                    }

                    if (position.x < 0 || position.y < 0 || position.z < 0) {
                        run_once console.log << "Rendering chunk LOD=" << lod
                                             << " at negative coordinates: " << position.x << ", " << position.y << ", "
                                             << position.z;
                    }

                    const float scale = std::ldexp(1.0f, static_cast<int>(lod));
                    const float chunk_x = static_cast<float>(position.x) * static_cast<float>(CHUNK_SIZE) * scale;
                    const float chunk_y = static_cast<float>(position.y) * static_cast<float>(CHUNK_SIZE) * scale;
                    const float chunk_z = static_cast<float>(position.z) * static_cast<float>(CHUNK_SIZE) * scale;
                    vertex_count += mesh.size();
                    for (const auto& vertex : mesh) {
                        vertices.insert(vertices.end(), {
                                                            chunk_x + vertex.x / 16.0f * scale,
                                                            chunk_y + vertex.y / 16.0f * scale,
                                                            chunk_z + vertex.z / 16.0f * scale,
                                                            vertex.lighting / 65535.0f,
                                                            vertex.lighting / 65535.0f,
                                                            vertex.lighting / 65535.0f,
                                                            static_cast<float>(vertex.u),
                                                            static_cast<float>(vertex.v),
                                                        });
                    }
                }
            }
        }

        shader_program.use();
        texture.bind();
        glUniformMatrix4fv(view_projection_location, 1, GL_FALSE, glm::value_ptr(view_projection));
        glUniform2f(
            atlas_size_location, static_cast<float>(atlas->image.width), static_cast<float>(atlas->image.height));
        if (!vertices.empty()) {
            vao.bind();
            vbo.bind();
            vbo.data(std::span<const float>(vertices), GL_DYNAMIC_DRAW);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size() / 8));
        }

        run_limited(0.5) console.info << "Triangle count: " << vertex_count / 3 << "\nFPS: "
                                      << (long double)1.0 / (long double)parent->application->getmod<FpsHandler>().dt
                                      << "\nCamera pos: " << camera.x << ',' << camera.y << ',' << camera.z;
    }

    void quit() override {}
} REGISTER_MOD(TestRenderer, MainWorldRenderer);
