#include <algorithm>
#include <chrono>
#include <opengl.hpp>
#include "base/fps_handler.hpp"
#include "base/resources.hpp"
#include <renderer/renderer.hpp>
#include <base/controls.hpp>
#include <ui/debug_screen.hpp>
#include <sstream>

#include <cmath>
#include <functional>
#include <map>
#include <shared_mutex>
#include <unordered_map>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <span>
#include <vector>

struct WorldRendererGl : BaseRenderer {
    tClass;

    /// pos(3) + light(1) + tileUV(2) + tileRect(4)
    static constexpr size_t FLOATS_PER_VERTEX = 10;

    opengl::ShaderProgram shader_program;
    opengl::Texture texture;

    /// Chunk-local vertices, converted and uploaded once per mesh.
    struct GpuMesh {
        World::MeshData::Mesh source;  // keeps the mesh alive so the pointer key can't be reused
        opengl::VBO vbo;
        opengl::VAO vao;
        GLsizei vertex_count = 0;
    };
    std::unordered_map<const void*, std::unique_ptr<GpuMesh>> gpu_meshes;

    GpuMesh& gpu_mesh_for(const World::MeshData::Mesh& mesh) {
        auto& slot = gpu_meshes[mesh.get()];
        if (slot) return *slot;

        slot = std::make_unique<GpuMesh>();
        slot->source = mesh;
        slot->vertex_count = static_cast<GLsizei>(mesh->size());

        std::vector<float> data;
        data.reserve(mesh->size() * FLOATS_PER_VERTEX);
        for (const auto& v : *mesh) {
            data.insert(data.end(), {
                                        static_cast<float>(v.x),
                                        static_cast<float>(v.y),
                                        static_cast<float>(v.z),
                                        v.lighting / 65535.0f,
                                        static_cast<float>(v.repeat_u),
                                        static_cast<float>(v.repeat_v),
                                        static_cast<float>(v.u),
                                        static_cast<float>(v.v),
                                        static_cast<float>(v.u_size),
                                        static_cast<float>(v.v_size),
                                    });
        }

        constexpr GLsizei stride = FLOATS_PER_VERTEX * sizeof(float);
        slot->vao.create();
        slot->vao.bind();
        slot->vbo.create();
        slot->vbo.bind();
        slot->vbo.data(std::span<const float>(data), GL_STATIC_DRAW);
        slot->vao.vertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, 0);  // aPos (chunk-local)
        slot->vao.enableAttribute(0);
        slot->vao.vertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, stride, 3 * sizeof(float));  // aLight
        slot->vao.enableAttribute(1);
        slot->vao.vertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, 4 * sizeof(float));  // tile repeat
        slot->vao.enableAttribute(2);
        slot->vao.vertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, 6 * sizeof(float));  // tile rect
        slot->vao.enableAttribute(3);
        return *slot;
    }

    const ResourceHandler::Resources::Atlas* atlas = nullptr;
    GLint view_projection_location = -1;
    GLint atlas_size_location = -1;
    GLint chunk_offset_location = -1;
    GLint chunk_scale_location = -1;
    GLint fog_color_location = -1;
    GLint fog_start_location = -1;
    GLint fog_end_location = -1;

    /// Used for both glClearColor and the fog, so distant terrain fades *into the sky*.
    glm::vec3 sky_color{1.f, 1.f, 1.f};
    /// Fog is 0% at/below this distance (blocks). The far end is derived each frame from the farthest mesh.
    float fog_start = 32.0f;

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
    ///  * a chunk is replaced by its finer children only once ALL 8 children are published;
    ///  * a finer chunk is hidden while a coarser ancestor is still standing in for its region.
    void rebuild_draw_list() {
        auto& data = world->mesh_data;
        std::shared_lock lock(data.meshes_mutex);
        const auto& meshes = data.meshes;
        draw_list_version = data.version.load();
        draw_list.clear();
        std::unordered_set<const void*> live;
        for (const auto& level : meshes)
            for (const auto& [pos, mesh] : level)
                if (mesh) live.insert(mesh.get());
        std::erase_if(gpu_meshes, [&](const auto& kv) { return !live.contains(kv.first); });

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
                if (!mesh || mesh->empty()) continue;
                if (children_ready(lod, pos)) continue;          // finer chunks take over
                if (claimed(lod + 1, parent_of(pos))) continue;  // a coarser chunk still covers this region
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

        atlas = resource_handler->resources.get_atlas("assets/textures/block");
        if (!atlas) throw std::runtime_error("Block texture atlas is not loaded");
        const auto& image = atlas->image;
        glTexImage2D(
            GL_TEXTURE_2D, 0, GL_RGBA, image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.bytes().data());

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
        chunk_offset_location = shader_program.get_uniform_location("chunkOffset", true);
        chunk_scale_location = shader_program.get_uniform_location("chunkScale", true);
        fog_color_location = shader_program.get_uniform_location("fogColor", true);
        fog_start_location = shader_program.get_uniform_location("fogStart", true);
        fog_end_location = shader_program.get_uniform_location("fogEnd", true);
    }

    struct Timestamp {
        Timestamp() { start = std::chrono::high_resolution_clock::now(); }
        std::chrono::high_resolution_clock::time_point start, end;
        // 16.6 ~= 60fps
        inline double millis() {
            return (double)(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()) / 1e3;
        }
        // 0.016 ~= 60fps
        inline double second() { return (millis() * 0.001); }
        inline double fps() { return 1.0 / second(); }
        inline void stop() { end = std::chrono::high_resolution_clock::now(); }
    };

    struct LoopedTimestamp {
        std::vector<Timestamp> start_stop_pairs{};
        inline void start_new() { start_stop_pairs.emplace_back(); }
        inline void end_new() { start_stop_pairs.back().stop(); }
        inline std::string stats() {
            if (start_stop_pairs.empty()) return "empty";
            std::stringstream retval;
            // in ms
            double total = 0.0;
            double min = -1.0, max = -1.0;

            for (auto& ts : start_stop_pairs) {
                double millis = ts.millis();
                total += millis;
                if (millis < min || min < 0.0) min = millis;
                if (millis > max) max = millis;
            }

            retval << "count=" << start_stop_pairs.size();
            retval << ",total=" << total;
            retval << ",avg=" << total / start_stop_pairs.size();
            retval << ",range=" << max - min;

            return retval.str();
        }
    };

    void loop() override {
        std::map<std::string, Timestamp> times{};
        std::map<std::string, LoopedTimestamp> ltimes{};
        times["loop"] = {};

        size_t chunk_count = 0;

        Camera camera = [&] {
            std::shared_lock __(world->camera_mutex);
            return world->render_camera;
        }();

        const auto window_size = renderer->window_size();
        ensure_framebuffer(window_size.x, window_size.y);  // use real pixel size if HiDPI differs

        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, fbo_w, fbo_h);
        glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);  // requires GL 4.5 / ARB_clip_control

        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_GREATER);
        glClearDepth(0.0);
        glClearColor(sky_color.r, sky_color.g, sky_color.b, 1.0f);
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

        if (world->mesh_data.version.load() != draw_list_version) rebuild_draw_list();

        // Fog reaches 100% at the farthest mesh we have, so it always hides the edge of the loaded world.
        // (Independent of what is on screen, so it doesn't pump when you turn around.)
        double farthest = 0.0;
        for (const auto& item : draw_list) {
            const double extent = static_cast<double>(CHUNK_SIZE) * std::ldexp(1.0, static_cast<int>(item.lod));
            const double dx = (item.pos.x + 0.5) * extent - camera.x;
            const double dy = (item.pos.y + 0.5) * extent - camera.y;
            const double dz = (item.pos.z + 0.5) * extent - camera.z;
            farthest = std::max(farthest, std::sqrt(dx * dx + dy * dy + dz * dz));
        }
        const float fog_end = static_cast<float>(std::max(farthest, 4.0 * static_cast<double>(fog_start)));

        shader_program.use();
        texture.bind();
        glUniformMatrix4fv(view_projection_location, 1, GL_FALSE, glm::value_ptr(view_projection));
        glUniform2f(
            atlas_size_location, static_cast<float>(atlas->image.width), static_cast<float>(atlas->image.height));
        glUniform3f(fog_color_location, sky_color.r, sky_color.g, sky_color.b);
        glUniform1f(fog_start_location, fog_start);
        glUniform1f(fog_end_location, fog_end);

        size_t triangle_count = 0;
        times["draw calls"] = {};
        for (const auto& item : draw_list) {
            if (!world->chunk_in_fov(item.pos, item.lod, aspect)) continue;

            const double scale = std::ldexp(1.0, static_cast<int>(item.lod));
            const double extent = static_cast<double>(CHUNK_SIZE) * scale;
            // Camera-relative origin computed in double, then narrowed, so precision stays good far from 0,0,0.
            glUniform3f(chunk_offset_location, static_cast<float>(item.pos.x * extent - camera.x),
                static_cast<float>(item.pos.y * extent - camera.y), static_cast<float>(item.pos.z * extent - camera.z));
            glUniform1f(chunk_scale_location, static_cast<float>(scale / static_cast<double>(CHUNK_SIZE)));

            const GpuMesh& gpu = gpu_mesh_for(item.mesh);
            gpu.vao.bind();
            glDrawArrays(GL_TRIANGLES, 0, gpu.vertex_count);

            triangle_count += gpu.vertex_count / 3;
            ++chunk_count;
        }
        times["draw calls"].stop();

        // TODO: (Chore) Move `fbo` to the src/opengl.hpp and make it a proper class with destructors
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, fbo_w, fbo_h, 0, 0, fbo_w, fbo_h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glClipControl(GL_LOWER_LEFT, GL_NEGATIVE_ONE_TO_ONE);

        times["loop"].stop();

        std::string times_str = "Times:";
        for (auto& [name, time] : times) {
            times_str = times_str + "\n" + name + ": " + std::to_string(time.millis()) + "ms";
        }
        for (auto& [name, time] : ltimes) {
            times_str = times_str + "\n" + name + ":" + time.stats();
        }

        if (debug_screen) {
            debug_screen->show("Triangle count", std::to_string(triangle_count));
            debug_screen->show("FPS", std::to_string(1.0 / fps_handler->dt));
            debug_screen->show("Chunks drawn", std::to_string(chunk_count) + " / " + std::to_string(draw_list.size()));
            debug_screen->show("Fog", std::to_string(fog_start) + " -> " + std::to_string(fog_end));
            debug_screen->show("Times", times_str);
        }

        // glEnable(GL_CULL_FACE);
        // glCullFace(GL_BACK);
        // glFrontFace(GL_CCW);
    }
    void quit() override {}

    FpsHandler* fps_handler = nullptr;
    World* world = nullptr;
    ResourceHandler* resource_handler = nullptr;
    DebugScreen* debug_screen = nullptr;
} REGISTER_RENDERER_MODULE(
    WorldRendererGl,
    {
        if (!parent->application->get_subapp_ptr<World>()) return false;
        if (!parent->application->getmod<ResourceHandler>().has_loaded) return false;
        return true;
    },
    { return renderer->backend == "OpenGL" && renderer->shader_backend == "GLSL"; },
    {
        retval->fps_handler = &parent->application->getmod<FpsHandler>();
        retval->resource_handler = &parent->application->getmod<ResourceHandler>();
        retval->world = parent->application->get_subapp_ptr<World>();
        retval->debug_screen = &parent->application->load_subapp<DebugScreen>();
    });
