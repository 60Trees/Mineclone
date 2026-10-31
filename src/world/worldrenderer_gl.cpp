#include <algorithm>
#include <chrono>
#include <opengl.hpp>
#include "base/fps_handler.hpp"
#include "base/resources.hpp"
#include <renderer/glrenderer.hpp>
#include <base/controls.hpp>
#include <sstream>
#include <world/worldrenderer.hpp>

#include <cmath>
#include <cstddef>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>

struct WorldRendererImpl : MainWorldRenderer::Renderer {
    tClass;

    opengl::ShaderProgram shader_program;
    opengl::Texture texture;

    const ResourceHandler::Resources::Atlas* atlas = nullptr;
    GLint view_projection_location = -1;
    GLint atlas_size_location = -1;
    GLint chunk_offset_location = -1;
    GLint chunk_scale_location = -1;
    GLint fog_color_location = -1;
    GLint fog_start_location = -1;
    GLint fog_end_location = -1;

    /// Used for both glClearColor and the fog, so distant terrain fades *into the sky* instead of into a
    /// colour that looks identical to "nothing there". Set to black if you want the old look.
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

    // ------------------------------------------------------------------------------------------------
    // GPU-resident chunk meshes
    // ------------------------------------------------------------------------------------------------

    /// One chunk mesh living on the GPU. The raw `BlockVertex` array is uploaded exactly once (GL_STATIC_DRAW)
    /// and never touched again; all per-frame positioning happens through uniforms in the vertex shader.
    struct GpuMesh {
        GLuint vao = 0;
        GLuint vbo = 0;
        GLsizei vertex_count = 0;
        /// Keeps the CPU mesh alive while cached, so its address (the cache key) can't be reused by another
        /// mesh. It also lets us detect "no longer published" via use_count() == 1.
        World::MeshData::Mesh source;

        GpuMesh() = default;
        GpuMesh(const GpuMesh&) = delete;
        GpuMesh& operator=(const GpuMesh&) = delete;
        ~GpuMesh() {
            if (vao) glDeleteVertexArrays(1, &vao);
            if (vbo) glDeleteBuffers(1, &vbo);
        }
    };

    std::unordered_map<const void*, std::unique_ptr<GpuMesh>> gpu_cache;

    GpuMesh* upload(const World::MeshData::Mesh& mesh) {
        auto& slot = gpu_cache[mesh.get()];
        if (slot) return slot.get();

        slot = std::make_unique<GpuMesh>();
        slot->source = mesh;
        slot->vertex_count = static_cast<GLsizei>(mesh->size());

        glGenVertexArrays(1, &slot->vao);
        glGenBuffers(1, &slot->vbo);
        glBindVertexArray(slot->vao);
        glBindBuffer(GL_ARRAY_BUFFER, slot->vbo);
        glBufferData(
            GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh->size() * sizeof(BlockVertex)), mesh->data(), GL_STATIC_DRAW);

        constexpr GLsizei stride = sizeof(BlockVertex);
        const auto at = [](size_t byte_offset) { return reinterpret_cast<const void*>(byte_offset); };

        // The integer data goes up as-is; the GPU converts to float while fetching. Nothing is pre-multiplied.
        // 0: chunk-local position, 0..256 (16 units per block)
        glVertexAttribPointer(0, 3, GL_UNSIGNED_SHORT, GL_FALSE, stride, at(offsetof(BlockVertex, x)));
        glEnableVertexAttribArray(0);
        // 1: lighting, normalized 0..65535 -> 0..1
        glVertexAttribPointer(1, 1, GL_UNSIGNED_SHORT, GL_TRUE, stride, at(offsetof(BlockVertex, lighting)));
        glEnableVertexAttribArray(1);
        // 2: tile repeat count at this corner
        glVertexAttribPointer(2, 2, GL_UNSIGNED_BYTE, GL_FALSE, stride, at(offsetof(BlockVertex, repeat_u)));
        glEnableVertexAttribArray(2);
        // 3: tile rect in the atlas (u, v, u_size, v_size) in pixels -- these four uint16s are contiguous
        glVertexAttribPointer(3, 4, GL_UNSIGNED_SHORT, GL_FALSE, stride, at(offsetof(BlockVertex, u)));
        glEnableVertexAttribArray(3);

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        return slot.get();
    }

    struct DrawItem {
        Vec3i pos;
        PosLodLevel lod;
        GpuMesh* gpu;
    };
    /// The meshes that should actually be drawn (LODs already resolved). Only rebuilt when the set of
    /// published meshes changes; per frame we just frustum-cull it.
    std::vector<DrawItem> draw_list;
    uint64_t draw_list_version = UINT64_MAX;

    /// Time `meshes_mutex` was held by the last rebuild (this is what can stall the mesher/loader threads),
    /// and the total time of the last rebuild including GPU uploads.
    double last_snapshot_ms = 0.0;
    double last_rebuild_ms = 0.0;

    /// Picks, per region of the world, exactly one LOD to draw:
    ///  * a chunk is replaced by its finer children only once ALL 8 children are published;
    ///  * a finer chunk is hidden while a coarser ancestor is still standing in for its region.
    /// New meshes are uploaded to the GPU here (render thread, so the GL context is current); meshes that are
    /// no longer published get their GPU buffers freed.
    ///
    /// `meshes_mutex` is held ONLY while copying shared_ptrs into a flat snapshot. Everything else (building
    /// lookups, resolving LODs, uploading) happens outside the lock, so the renderer can't starve
    /// publish_mesh()/drop_mesh() -- which the loader calls while holding its own level lock.
    void rebuild_draw_list() {
        Timestamp total_time;

        struct Entry {
            Vec3i pos;
            PosLodLevel lod;
            World::MeshData::Mesh mesh;
        };
        std::vector<Entry> snapshot;
        size_t lod_count = 0;

        {
            Timestamp lock_time;
            auto& data = parent->world->mesh_data;
            std::shared_lock lock(data.meshes_mutex);
            draw_list_version = data.version.load();
            lod_count = data.meshes.size();

            size_t total = 0;
            for (const auto& level : data.meshes) total += level.size();
            snapshot.reserve(total);
            for (PosLodLevel lod = 0; lod < lod_count; ++lod)
                for (const auto& [pos, mesh] : data.meshes[lod]) snapshot.push_back({pos, lod, mesh});

            lock_time.stop();
            last_snapshot_ms = lock_time.millis();
        }

        // ---- lock released: from here on we only touch our private snapshot ----

        std::vector<std::unordered_set<Vec3i, Vec3i::Vec3iHash>> present(lod_count);
        for (const auto& e : snapshot) present[e.lod].insert(e.pos);

        const auto has = [&](PosLodLevel lod, const Vec3i& p) { return lod < lod_count && present[lod].contains(p); };
        const auto children_ready = [&](PosLodLevel lod, const Vec3i& p) {
            if (lod == 0) return false;
            for (int i = 0; i < 8; ++i)
                if (has(lod - 1, Vec3i{p.x * 2 + (i & 1), p.y * 2 + ((i >> 1) & 1), p.z * 2 + ((i >> 2) & 1)}))
                    return true;  // was: return false on the first missing child
            return false;         // was: return true
        };
        const auto parent_of = [](const Vec3i& p) { return Vec3i{p.x >> 1, p.y >> 1, p.z >> 1}; };

        // claimed(lod, p): the region of (lod, p) is drawn by that chunk or by one of its ancestors.
        std::vector<std::unordered_map<Vec3i, bool, Vec3i::Vec3iHash>> memo(lod_count);
        const std::function<bool(PosLodLevel, const Vec3i&)> claimed = [&](PosLodLevel lod, const Vec3i& p) -> bool {
            if (lod >= lod_count) return false;
            if (const auto it = memo[lod].find(p); it != memo[lod].end()) return it->second;
            const bool result = (has(lod, p) && !children_ready(lod, p)) || claimed(lod + 1, parent_of(p));
            memo[lod].emplace(p, result);
            return result;
        };

        std::vector<const Entry*> visible;
        for (const auto& e : snapshot) {
            if (!e.mesh || e.mesh->empty()) continue;            // nothing to draw (but it counted as coverage)
            if (children_ready(e.lod, e.pos)) continue;          // finer chunks take over
            if (claimed(e.lod + 1, parent_of(e.pos))) continue;  // a coarser chunk still covers this region
            visible.push_back(&e);
        }

        // Free GPU copies of meshes that were no longer published when we took the snapshot. Anything that was
        // published is referenced by the snapshot as well as the cache, so only use_count()==1 is dead.
        for (auto it = gpu_cache.begin(); it != gpu_cache.end();) {
            if (it->second->source.use_count() == 1)
                it = gpu_cache.erase(it);
            else
                ++it;
        }

        draw_list.clear();
        draw_list.reserve(visible.size());
        for (const Entry* e : visible) draw_list.push_back({e->pos, e->lod, upload(e->mesh)});

        total_time.stop();
        last_rebuild_ms = total_time.millis();
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
        // Vertex layouts live in each GpuMesh's own VAO (see upload()).
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

    void loop() override {
        Timestamp loop_time;
        Timestamp submit_time;

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

        if (parent->world->mesh_data.version.load() != draw_list_version) rebuild_draw_list();

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

        size_t chunk_count = 0;
        size_t vertex_count = 0;

        // One draw call per chunk. The vertex buffers are static; the only thing that changes per call is the
        // camera-relative origin (computed in double on the CPU, then rounded to float once) and the scale.
        for (const auto& item : draw_list) {
            if (!parent->world->chunk_in_fov(item.pos, item.lod, aspect)) continue;

            const double scale = std::ldexp(1.0, static_cast<int>(item.lod));
            const double extent = static_cast<double>(CHUNK_SIZE) * scale;
            const double origin_x = item.pos.x * extent - camera.x;
            const double origin_y = item.pos.y * extent - camera.y;
            const double origin_z = item.pos.z * extent - camera.z;

            glUniform3f(chunk_offset_location, static_cast<float>(origin_x), static_cast<float>(origin_y),
                static_cast<float>(origin_z));
            // world units per raw vertex unit (raw coords are 16 per block).
            glUniform1f(chunk_scale_location, static_cast<float>(scale / static_cast<double>(CHUNK_SIZE)));

            glBindVertexArray(item.gpu->vao);
            glDrawArrays(GL_TRIANGLES, 0, item.gpu->vertex_count);

            ++chunk_count;
            vertex_count += static_cast<size_t>(item.gpu->vertex_count);
        }
        glBindVertexArray(0);
        submit_time.stop();  // CPU time to *submit* the draws; the GPU runs asynchronously

        // TODO: (Chore) Move `fbo` to the src/opengl.hpp and make it a proper class with destructors
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, fbo_w, fbo_h, 0, 0, fbo_w, fbo_h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glClipControl(GL_LOWER_LEFT, GL_NEGATIVE_ONE_TO_ONE);

        loop_time.stop();

        const size_t triangle_count = vertex_count / 3;

        run_limited(0.5) console.info << "Triangle count: " << triangle_count << "\nFPS: "
                                      << (long double)1.0 / (long double)parent->application->getmod<FpsHandler>().dt
                                      << "\nChunks drawn: " << chunk_count << " / " << draw_list.size()
                                      << "\nFog: " << fog_start << " -> " << fog_end
                                      << "\nLast rebuild: " << last_rebuild_ms << "ms (mesh lock held "
                                      << last_snapshot_ms << "ms)"
                                      << "\nTimes:\nloop: " << loop_time.millis()
                                      << "ms\ndraw submit: " << submit_time.millis() << "ms";

        // glEnable(GL_CULL_FACE);
        // glCullFace(GL_BACK);
        // glFrontFace(GL_CCW);
    }

    void quit() override {
        draw_list.clear();
        gpu_cache.clear();  // frees every VAO/VBO while the GL context is still alive
        if (fbo) {
            glDeleteFramebuffers(1, &fbo);
            glDeleteRenderbuffers(1, &color_rb);
            glDeleteRenderbuffers(1, &depth_rb);
            fbo = color_rb = depth_rb = 0;
        }
    }
} REGISTER_MOD(WorldRendererImpl, MainWorldRenderer);
