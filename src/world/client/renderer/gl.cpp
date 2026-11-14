#include <renderer/renderer.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <opengl.hpp>
#include <base/resources.hpp>
#include <world/client/main.hpp>
#include <world/client/mesh/main.hpp>

#include <stdexcept>
#include <string>
#include <vector>
#include <cmath>

namespace {
    GLuint compile_shader(GLenum type, const char* source) {
        const GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);

        GLint success = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            GLint length = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
            std::string log(static_cast<size_t>(length), '\0');
            glGetShaderInfoLog(shader, length, nullptr, log.data());
            glDeleteShader(shader);
            throw std::runtime_error("World renderer shader compilation failed: " + log);
        }
        return shader;
    }

    GLuint make_program() {
#ifdef __EMSCRIPTEN__
        constexpr const char* vertex_source = R"(#version 300 es
            precision highp float;
            layout(location = 0) in vec3 position;
            layout(location = 1) in float light;
            layout(location = 2) in vec2 texcoord;
            uniform mat4 view_projection;
            out float vertex_light;
            out vec2 vertex_texcoord;
            void main() {
                gl_Position = view_projection * vec4(position, 1.0);
                vertex_light = light;
                vertex_texcoord = texcoord;
            })";
        constexpr const char* fragment_source = R"(#version 300 es
            precision mediump float;
            in float vertex_light;
            in vec2 vertex_texcoord;
            uniform sampler2D block_atlas;
            out vec4 color;
            void main() { color = vec4(texture(block_atlas, vertex_texcoord).rgb * vertex_light, 1.0); })";
#else
        constexpr const char* vertex_source = R"(#version 430 core
            layout(location = 0) in vec3 position;
            layout(location = 1) in float light;
            layout(location = 2) in vec2 texcoord;
            uniform mat4 view_projection;
            out float vertex_light;
            out vec2 vertex_texcoord;
            void main() {
                gl_Position = view_projection * vec4(position, 1.0);
                vertex_light = light;
                vertex_texcoord = texcoord;
            })";
        constexpr const char* fragment_source = R"(#version 430 core
            in float vertex_light;
            in vec2 vertex_texcoord;
            uniform sampler2D block_atlas;
            out vec4 color;
            void main() { color = vec4(texture(block_atlas, vertex_texcoord).rgb * vertex_light, 1.0); })";
#endif

        const GLuint vertex = compile_shader(GL_VERTEX_SHADER, vertex_source);
        const GLuint fragment = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
        const GLuint program = glCreateProgram();
        glAttachShader(program, vertex);
        glAttachShader(program, fragment);
        glLinkProgram(program);
        glDeleteShader(vertex);
        glDeleteShader(fragment);

        GLint success = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &success);
        if (!success) {
            GLint length = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
            std::string log(static_cast<size_t>(length), '\0');
            glGetProgramInfoLog(program, length, nullptr, log.data());
            glDeleteProgram(program);
            throw std::runtime_error("World renderer shader link failed: " + log);
        }
        return program;
    }
}

struct WorldGlRenderer : BaseRenderer {
    tClass;
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint program = 0;
    GLuint block_atlas = 0;

    void load_block_atlas() {
        if (block_atlas) return;
        auto& resources = renderer->application->getmod<ResourceHandler>();
        if (!resources.has_loaded) return;
        const auto* atlas = resources.resources.get_atlas("assets/textures/block");
        if (!atlas) throw std::runtime_error("Missing block texture atlas");
        glGenTextures(1, &block_atlas);
        glBindTexture(GL_TEXTURE_2D, block_atlas);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, atlas->image.width, atlas->image.height, 0, GL_RGBA,
            GL_UNSIGNED_BYTE, atlas->image.bytes().data());
    }

    void init() override {
        program = make_program();
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<const void*>(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
            reinterpret_cast<const void*>(4 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glBindVertexArray(0);

    }

    void loop() override {
        auto* world = renderer->application->get_subapp_ptr<WorldClient>();
        if (!world) return;
        auto* meshes = world->find_mod<WorldMeshHandler>();
        if (!meshes) return;
        load_block_atlas();
        if (!block_atlas) return;

        glClearColor(0.08f, 0.12f, 0.18f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        Camera camera;
        {
            std::scoped_lock lock(world->host->camera_mutex);
            camera = world->host->camera;
        }

        const auto size = renderer->window_size();
        const float aspect = size.y == 0 ? 1.0f : static_cast<float>(size.x) / static_cast<float>(size.y);
        const auto projection = glm::perspective(glm::radians(70.0f), aspect, 0.05f, 4096.0f);
        const auto direction = glm::vec3(
            std::cos(glm::radians(static_cast<float>(camera.pitch))) *
                std::cos(glm::radians(static_cast<float>(camera.yaw))),
            std::sin(glm::radians(static_cast<float>(camera.pitch))),
            std::cos(glm::radians(static_cast<float>(camera.pitch))) *
                std::sin(glm::radians(static_cast<float>(camera.yaw))));
        const auto view = glm::lookAt(
            glm::vec3(0.0f), direction, {0, 1, 0});

        glEnable(GL_DEPTH_TEST);
        glUseProgram(program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, block_atlas);
        glUniform1i(glGetUniformLocation(program, "block_atlas"), 0);
        const auto view_projection = projection * view;
        glUniformMatrix4fv(
            glGetUniformLocation(program, "view_projection"), 1, GL_FALSE, glm::value_ptr(view_projection));
        glBindVertexArray(vao);

        std::shared_lock chunks_lock(world->chunks_mutex);
        std::shared_lock meshes_lock(meshes->chunk_meshes_mutex);
        const auto has_mesh = [&](ChunkIndex index) {
            return meshes->chunk_meshes.contains(index);
        };
        const auto floor_div = [](int value, int divisor) {
            return value >= 0 ? value / divisor : (value - divisor + 1) / divisor;
        };
        const auto finer_complete = [&](ChunkIndex coarse) {
            if (coarse.lod == 0) return false;
            const auto finer_lod = coarse.lod - 1;
            for (int x = 0; x < 2; ++x)
                for (int y = 0; y < 2; ++y)
                    for (int z = 0; z < 2; ++z)
                        if (!has_mesh({{coarse.pos.x * 2 + x, coarse.pos.y * 2 + y, coarse.pos.z * 2 + z},
                            finer_lod}))
                            return false;
            return true;
        };
        const auto should_draw = [&](ChunkIndex index) {
            if (index.lod > 0 && finer_complete(index)) return false;
            for (LodLevel ancestor_lod = index.lod + 1; ancestor_lod < world->lod_count; ++ancestor_lod) {
                const auto scale = 1u << (ancestor_lod - index.lod);
                const ChunkIndex ancestor{{floor_div(index.pos.x, static_cast<int>(scale)),
                    floor_div(index.pos.y, static_cast<int>(scale)), floor_div(index.pos.z, static_cast<int>(scale))},
                    ancestor_lod};
                if (has_mesh(ancestor) && !finer_complete(ancestor)) return false;
            }
            return true;
        };
        for (const auto& [index, mesh] : meshes->chunk_meshes) {
            if (!world->chunks.contains(index) || !world->chunks.at(index) || !should_draw(index)) continue;
            const auto width = static_cast<float>(world->host->lod_descriptors.at(index.lod).width);
            const auto scale = static_cast<float>(world->host->lod_descriptors.at(index.lod).size);
            const double origin_x = static_cast<double>(index.pos.x) * width * scale - camera.x;
            const double origin_y = static_cast<double>(index.pos.y) * width * scale - camera.y;
            const double origin_z = static_cast<double>(index.pos.z) * width * scale - camera.z;

            std::vector<float> vertices;
            vertices.reserve(mesh.vertices.size() * 6);
            for (const auto& vertex : mesh.vertices) {
                vertices.push_back(static_cast<float>(origin_x + vertex.x * scale));
                vertices.push_back(static_cast<float>(origin_y + vertex.y * scale));
                vertices.push_back(static_cast<float>(origin_z + vertex.z * scale));
                vertices.push_back(static_cast<float>(vertex.lighting) / 65535.0f);
                vertices.push_back(vertex.u);
                vertices.push_back(vertex.v);
            }
            if (vertices.empty()) continue;
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(),
                GL_STREAM_DRAW);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh.vertices.size()));
        }
        glBindVertexArray(0);
    }

    void quit() override {
        if (program) glDeleteProgram(program);
        if (vbo) glDeleteBuffers(1, &vbo);
        if (block_atlas) glDeleteTextures(1, &block_atlas);
        if (vao) glDeleteVertexArrays(1, &vao);
        program = vao = vbo = block_atlas = 0;
    }
};

REGISTER_RENDERER_MODULE(WorldGlRenderer, { return true; },
    { return renderer->backend == "OpenGL" && renderer->shader_backend == "GLSL"; }, {})
