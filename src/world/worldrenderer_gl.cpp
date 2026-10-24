#include <opengl.hpp>
#include "base/fps_handler.hpp"
#include "base/resources.hpp"
#include <renderer/glrenderer.hpp>
#include <base/controls.hpp>
#include <world/worldrenderer.hpp>

#include <cmath>
#include <functional>
#include <shared_mutex>
#include <unordered_map>
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
    GLint camera_position_location = -1;

    GLuint fbo = 0, color_rb = 0, depth_rb = 0;
    int fbo_w = 0, fbo_h = 0;

    void ensure_framebuffer(int w, int h) {
        if (fbo && w == fbo_w && h == fbo_h) return;
        if (fbo) {
            glDeleteFramebuffers(1, &fbo);
            glDeleteRenderbuffers(1, &color_rb);
            glDeleteRenderbuffers(1, &depth_rb);
        }
        fbo_w = w;
        fbo_h = h;
        glGenFramebuffers(1, &fbo);
        glGenRenderbuffers(1, &color_rb);
        glGenRenderbuffers(1, &depth_rb);
        glBindRenderbuffer(GL_RENDERBUFFER, color_rb);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, w, h);
        glBindRenderbuffer(GL_RENDERBUFFER, depth_rb);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT32F, w, h);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color_rb);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_rb);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("Render framebuffer incomplete");
    }

    struct DrawItem {
        Vec3i pos;
        PosLodLevel lod;
        World::MeshData::Mesh mesh;
    };
    /// The meshes that should actually be drawn (LODs already resolved). Only rebuilt when the set of
    /// published meshes changes; per frame we just frustum-cull it.
    std::vector<DrawItem> draw_list;
    uint64_t draw_list_version = UINT64_MAX;

    /// Picks, per region of the world, exactly one LOD to draw:
    ///  * a chunk is replaced by its finer children only once ALL 8 children are published (the old code hid it
    ///    as soon as ANY child existed, which punched holes while the finer LOD was still loading);
    ///  * a finer chunk is hidden while a coarser ancestor is still standing in for its region (otherwise a
    ///    half-loaded fine group is drawn on top of the coarse chunk -> the LOD overlap).
    void rebuild_draw_list() {
        auto& data = parent->world->mesh_data;
        std::shared_lock lock(data.meshes_mutex);
        const auto& meshes = data.meshes;
        draw_list_version = data.version.load();
        draw_list.clear();

        const auto has = [&](PosLodLevel lod, const Vec3i& p) {
            return lod < meshes.size() && meshes[lod].contains(p);
        };
        const auto children_ready = [&](PosLodLevel lod, const Vec3i& p) {
            if (lod == 0) return false;
            for (int i = 0; i < 8; ++i)
                if (!has(lod - 1, Vec3i{p.x * 2 + (i & 1), p.y * 2 + ((i >> 1) & 1), p.z * 2 + ((i >> 2) & 1)}))
                    return false;
            return true;
        };
        const auto parent_of = [](const Vec3i& p) { return Vec3i{p.x >> 1, p.y >> 1, p.z >> 1}; };

        // claimed(lod, p): the region of (lod, p) is drawn by that chunk or by one of its ancestors.
        std::vector<std::unordered_map<Vec3i, bool, Vec3i::Vec3iHash>> memo(meshes.size());
        const std::function<bool(PosLodLevel, const Vec3i&)> claimed = [&](PosLodLevel lod, const Vec3i& p) -> bool {
            if (lod >= meshes.size()) return false;
            if (const auto it = memo[lod].find(p); it != memo[lod].end()) return it->second;
            const bool result = (has(lod, p) && !children_ready(lod, p)) || claimed(lod + 1, parent_of(p));
            memo[lod].emplace(p, result);
            return result;
        };

        for (PosLodLevel lod = 0; lod < meshes.size(); ++lod)
            for (const auto& [pos, mesh] : meshes[lod]) {
                if (children_ready(lod, pos)) continue;          // finer chunks take over
                if (claimed(lod + 1, parent_of(pos))) continue;  // a coarser chunk still covers this region
                if (!mesh || mesh->empty()) continue;            // nothing to draw (but it did count as coverage)
                draw_list.push_back({pos, lod, mesh});
            }
    }

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

#ifdef __APPLE__
        opengl::gl_err << "You might have some issues with macos. Fuck macos. It sucks";
#endif

        {
            opengl::Shader vertex_shader(GL_VERTEX_SHADER);
            vertex_shader.create();
            vertex_shader.source_from_path("assets/vert.glsl");
            vertex_shader.compile();

            opengl::Shader fragment_shader(GL_FRAGMENT_SHADER);
            fragment_shader.create();
            fragment_shader.source_from_path("assets/frag.glsl");
            fragment_shader.compile();

            shader_program.create();
            shader_program.attach(vertex_shader);
            shader_program.attach(fragment_shader);
            shader_program.link();
            shader_program.use();
        }

        view_projection_location = shader_program.get_uniform_location("viewProjection", true);
        atlas_size_location = shader_program.get_uniform_location("atlasSize", true);
        camera_position_location = shader_program.get_uniform_location("cameraPosition", true);

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
        auto start = std::chrono::high_resolution_clock::now();
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_GREATER);
        glClearDepth(0.0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        size_t chunk_count = 0;

        Camera camera = [&] {
            std::shared_lock __(parent->world->camera_mutex);
            return parent->world->render_camera;
        }();

        const auto window_size = parent->application->get_subapp<MainRenderer>().window_size();
        ensure_framebuffer(window_size.x, window_size.y);  // use real pixel size if HiDPI differs

        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, fbo_w, fbo_h);
        glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);  // requires GL 4.5 / ARB_clip_control

        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_GREATER);
        glClearDepth(0.0);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        const float aspect = static_cast<float>(window_size.x) / static_cast<float>(window_size.y);
        const float pitch = glm::radians(camera.pitch);
        const float yaw = glm::radians(camera.yaw);
        const glm::vec3 direction{
            std::cos(pitch) * std::cos(yaw),
            std::sin(pitch),
            std::cos(pitch) * std::sin(yaw),
        };
        const float near_plane = 0.05f;
        glm::mat4 reverse_infinite_perspective{0.0f};
        const float tan_half_fov = std::tan(glm::radians(70.0f) * 0.5f);

        reverse_infinite_perspective[0][0] = 1.0f / (aspect * tan_half_fov);
        reverse_infinite_perspective[1][1] = 1.0f / tan_half_fov;
        reverse_infinite_perspective[2][2] = 0.0f;
        reverse_infinite_perspective[2][3] = -1.0f;
        reverse_infinite_perspective[3][2] = near_plane;

        const glm::mat4 view_projection =
            reverse_infinite_perspective * glm::lookAt(glm::vec3(0.0f), direction, glm::vec3(0.0f, 1.0f, 0.0f));

        if (parent->world->mesh_data.version.load() != draw_list_version) rebuild_draw_list();

        std::vector<float> vertices;
        for (const auto& item : draw_list) {
            if (!parent->world->chunk_in_fov(item.pos, item.lod, aspect)) continue;

            ++chunk_count;
            const double scale = std::ldexp(1.0, static_cast<int>(item.lod));
            const double extent = static_cast<double>(CHUNK_SIZE) * scale;
            const double origin_x = item.pos.x * extent - camera.x;
            const double origin_y = item.pos.y * extent - camera.y;
            const double origin_z = item.pos.z * extent - camera.z;
            const double vertex_scale = scale / static_cast<double>(CHUNK_SIZE);

            for (const auto& vertex : *item.mesh) {
                const float light = vertex.lighting / 65535.0f;
                vertices.insert(vertices.end(), {
                                                    static_cast<float>(origin_x + vertex.x * vertex_scale),
                                                    static_cast<float>(origin_y + vertex.y * vertex_scale),
                                                    static_cast<float>(origin_z + vertex.z * vertex_scale),
                                                    light,
                                                    light,
                                                    light,
                                                    static_cast<float>(vertex.u),
                                                    static_cast<float>(vertex.v),
                                                });
            }
        }

        shader_program.use();
        texture.bind();
        glUniformMatrix4fv(view_projection_location, 1, GL_FALSE, glm::value_ptr(view_projection));
        glUniform2f(
            atlas_size_location, static_cast<float>(atlas->image.width), static_cast<float>(atlas->image.height));
        glUniform3f(camera_position_location, 0.0f, 0.0f, 0.0f);  // vertices are camera-relative
        if (!vertices.empty()) {
            vao.bind();
            vbo.bind();
            vbo.data(std::span<const float>(vertices), GL_DYNAMIC_DRAW);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size() / 8));
        }

        // TODO: (Chore) Move `fbo` to the src/opengl.hpp and make it a proper class with destructors
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, fbo_w, fbo_h, 0, 0, fbo_w, fbo_h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glClipControl(GL_LOWER_LEFT, GL_NEGATIVE_ONE_TO_ONE);

        auto end = std::chrono::high_resolution_clock::now();

        run_limited(0.5) console.info
            << "Triangle count: " << vertices.size() / 3
            << "\nFPS: " << (long double)1.0 / (long double)parent->application->getmod<FpsHandler>().dt
            << " (Rendering FPS: "
            << 1.0 / (0.001 * static_cast<double>(
                                  std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count()))
            << ")"
            << "\nCamera pos: " << camera.x << ',' << camera.y << ',' << camera.z;

        // glEnable(GL_CULL_FACE);
        // glCullFace(GL_BACK);
        // glFrontFace(GL_CCW);
    }

    void quit() override {}
} REGISTER_MOD(TestRenderer, MainWorldRenderer);
