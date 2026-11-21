#include <renderer/renderer.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <opengl.hpp>
#include <base/resources.hpp>
#include <world/client/main.hpp>
#include <world/client/mesh/main.hpp>

#include <stdexcept>
#include <string>
#include <unordered_map>
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
            uniform vec3 chunk_offset;
            uniform float chunk_scale;
            out float vertex_light;
            out vec2 vertex_texcoord;
            void main() {
                gl_Position = view_projection * vec4(chunk_offset + position * chunk_scale, 1.0);
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
            uniform vec3 chunk_offset;
            uniform float chunk_scale;
            out float vertex_light;
            out vec2 vertex_texcoord;
            void main() {
                gl_Position = view_projection * vec4(chunk_offset + position * chunk_scale, 1.0);
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
    GLint view_projection_location = -1;
    GLint chunk_offset_location = -1;
    GLint chunk_scale_location = -1;
    GLint block_atlas_location = -1;

    /// @brief One persistent GPU buffer per chunk mesh, uploaded once when the mesh changes.
    struct GpuMesh {
        GLuint vao = 0;
        GLuint vbo = 0;
        GLsizei count = 0;
        uint64_t revision = 0;
    };
    std::unordered_map<ChunkIndex, GpuMesh> gpu_meshes;

    void upload_mesh(const ChunkIndex& index, const ChunkMesh& mesh) {
        GpuMesh& gpu = gpu_meshes[index];
        if (gpu.vao && gpu.revision == mesh.revision) return;

        if (!gpu.vao) {
            glGenVertexArrays(1, &gpu.vao);
            glGenBuffers(1, &gpu.vbo);
            glBindVertexArray(gpu.vao);
            glBindBuffer(GL_ARRAY_BUFFER, gpu.vbo);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(
                1, 1, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<const void*>(3 * sizeof(float)));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(
                2, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<const void*>(4 * sizeof(float)));
            glEnableVertexAttribArray(2);
            glBindVertexArray(0);
        }

        std::vector<float> vertices;
        vertices.reserve(mesh.vertices.size() * 6);
        for (const auto& vertex : mesh.vertices) {
            vertices.push_back(static_cast<float>(vertex.x));
            vertices.push_back(static_cast<float>(vertex.y));
            vertices.push_back(static_cast<float>(vertex.z));
            vertices.push_back(static_cast<float>(vertex.lighting) / 65535.0f);
            vertices.push_back(vertex.u);
            vertices.push_back(vertex.v);
        }

        glBindVertexArray(gpu.vao);
        glBindBuffer(GL_ARRAY_BUFFER, gpu.vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(),
            GL_STATIC_DRAW);
        glBindVertexArray(0);

        gpu.count = static_cast<GLsizei>(mesh.vertices.size());
        gpu.revision = mesh.revision;
    }

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
        view_projection_location = glGetUniformLocation(program, "view_projection");
        chunk_offset_location = glGetUniformLocation(program, "chunk_offset");
        chunk_scale_location = glGetUniformLocation(program, "chunk_scale");
        block_atlas_location = glGetUniformLocation(program, "block_atlas");
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
        glUniform1i(block_atlas_location, 0);
        const auto view_projection = projection * view;
        glUniformMatrix4fv(view_projection_location, 1, GL_FALSE, glm::value_ptr(view_projection));

        // Snapshot each LOD's camera chunk once per frame, for both the view-distance
        // test and the LOD selection rules.
        std::vector<ChunkPos> camera_chunks(world->lod_count);
        for (LodLevel lod = 0; lod < world->lod_count; ++lod)
            camera_chunks[lod] = world->get_camera_pos(lod, false);

        std::shared_lock chunks_lock(world->chunks_mutex);
        std::shared_lock meshes_lock(meshes->chunk_meshes_mutex);
        const auto& lods = world->host->lod_descriptors;

        // A chunk can "cover" the area of another only when it exists, is allowed to
        // render, and has already been meshed. The mesh may legitimately be empty (all
        // air / all solid chunks have no visible faces); what matters is that meshing
        // has finished, otherwise hiding the coarser chunk would leave a hole.
        const auto ready_to_cover = [&](const ChunkIndex& index) {
            const auto chunk_it = world->chunks.find(index);
            if (chunk_it == world->chunks.end() || !chunk_it->second || !chunk_it->second->should_render)
                return false;

            return meshes->chunk_meshes.contains(index);
        };

        // Only the LOD directly below is checked: it owns the 2x2x2 block of chunks that
        // exactly covers this one. The result is memoized because a chunk and its parent
        // both ask the same question in the same frame.
        std::unordered_map<ChunkIndex, bool> children_ready_cache;
        const auto children_ready = [&](const ChunkIndex& parent) {
            if (parent.lod == 0) return false;  // LOD 0 has no finer level

            const auto cached = children_ready_cache.find(parent);
            if (cached != children_ready_cache.end()) return cached->second;

            bool ready = true;
            for (const auto& child : Chunk::get_child_chunks_lod_specific(lods, parent, parent.lod - 1)) {
                if (!ready_to_cover(child)) {
                    ready = false;
                    break;
                }
            }
            children_ready_cache.emplace(parent, ready);
            return ready;
        };

        // Only the LOD directly above is checked. The parent hides this chunk when the
        // parent itself would render, i.e. it exists/has a mesh and its own children are
        // not all ready (a complete parent is replaced by the level below instead).
        const auto parent_covers = [&](const ChunkIndex& index) {
            if (index.lod + 1 >= world->lod_count) return false;  // biggest LOD has no parent

            const auto parent = Chunk::get_parent_chunk(lods, index);
            if (!parent || !ready_to_cover(*parent)) return false;
            return !children_ready(*parent);
        };

        // Exactly one level should be visible in any region:
        //  - if the level below is complete, this chunk is redundant;
        //  - if the level above is incomplete (so it renders), this chunk is redundant.
        const auto should_draw = [&](const ChunkIndex& index) {
            if (children_ready(index)) return false;
            return !parent_covers(index);
        };

        for (const auto& [index, mesh] : meshes->chunk_meshes) {
            if (mesh.vertices.empty()) continue;

            const auto chunk_it = world->chunks.find(index);
            if (chunk_it == world->chunks.end() || !chunk_it->second || !chunk_it->second->should_render) continue;

            // Never draw outside the view distance, even if the loader has not caught up
            // with the camera yet.
            if (index.lod >= camera_chunks.size()) continue;
            const auto& camera_chunk = camera_chunks[index.lod];
            const auto view_distance = world->view_distance(index.lod);
            if (std::abs(index.pos.x - camera_chunk.x) > static_cast<int>(view_distance.x) ||
                std::abs(index.pos.y - camera_chunk.y) > static_cast<int>(view_distance.y) ||
                std::abs(index.pos.z - camera_chunk.z) > static_cast<int>(view_distance.x))
                continue;

            if (!should_draw(index)) continue;
            const auto width = static_cast<float>(world->host->lod_descriptors.at(index.lod).width);
            const auto scale = static_cast<float>(world->host->lod_descriptors.at(index.lod).scale);
            const auto origin_x = static_cast<float>(static_cast<double>(index.pos.x) * width * scale - camera.x);
            const auto origin_y = static_cast<float>(static_cast<double>(index.pos.y) * width * scale - camera.y);
            const auto origin_z = static_cast<float>(static_cast<double>(index.pos.z) * width * scale - camera.z);

            upload_mesh(index, mesh);
            const GpuMesh& gpu = gpu_meshes.at(index);
            if (gpu.count == 0) continue;

            glBindVertexArray(gpu.vao);
            glUniform3f(chunk_offset_location, origin_x, origin_y, origin_z);
            glUniform1f(chunk_scale_location, scale);
            glDrawArrays(GL_TRIANGLES, 0, gpu.count);
        }
        glBindVertexArray(0);

        // Free GPU buffers for chunks whose meshes no longer exist.
        for (auto it = gpu_meshes.begin(); it != gpu_meshes.end();) {
            if (meshes->chunk_meshes.contains(it->first)) {
                ++it;
                continue;
            }
            if (it->second.vbo) glDeleteBuffers(1, &it->second.vbo);
            if (it->second.vao) glDeleteVertexArrays(1, &it->second.vao);
            it = gpu_meshes.erase(it);
        }
    }

    void quit() override {
        for (auto& [index, gpu] : gpu_meshes) {
            if (gpu.vbo) glDeleteBuffers(1, &gpu.vbo);
            if (gpu.vao) glDeleteVertexArrays(1, &gpu.vao);
        }
        gpu_meshes.clear();
        if (program) glDeleteProgram(program);
        if (block_atlas) glDeleteTextures(1, &block_atlas);
        if (vao) glDeleteVertexArrays(1, &vao);
        if (vbo) glDeleteBuffers(1, &vbo);
        program = vao = vbo = block_atlas = 0;
    }
};

REGISTER_RENDERER_MODULE(WorldGlRenderer, { return true; },
    { return renderer->backend == "OpenGL" && renderer->shader_backend == "GLSL"; }, {})
