/*
 * @author 60Trees_ (github.com/60Trees)
 * @version 1.0.0
 */

#pragma once

#include <SDL3/SDL.h>

#include <glm/glm.hpp>

#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include "logger.hpp"

#if defined(__EMSCRIPTEN__)
#    include <GLES3/gl3.h>
#else
#    include <glad/gl.h>
#endif

void set_opengl_version();
std::string_view load_asset(const std::string& path);

namespace opengl {
    extern logger::Logger gl_log;
    extern logger::Logger gl_err;

    inline std::string_view get_string(GLenum name) {
        return std::string_view(
            reinterpret_cast<const char*>(glGetString(GL_VENDOR)));
    }

#ifdef DO_GL_DEBUG
    inline void debug_callback(
        GLenum source, GLenum type, GLuint id, GLenum severity,
        GLsizei /*length*/, const GLchar* message, const void* /*userParam*/
    ) {
        const char* source_str = "UNKNOWN";

        switch (source) {
            case GL_DEBUG_SOURCE_API:
                source_str = "API";
                break;

            case GL_DEBUG_SOURCE_WINDOW_SYSTEM:
                source_str = "WINDOW SYSTEM";
                break;

            case GL_DEBUG_SOURCE_SHADER_COMPILER:
                source_str = "SHADER COMPILER";
                break;

            case GL_DEBUG_SOURCE_THIRD_PARTY:
                source_str = "THIRD PARTY";
                break;

            case GL_DEBUG_SOURCE_APPLICATION:
                source_str = "APPLICATION";
                break;

            case GL_DEBUG_SOURCE_OTHER:
                source_str = "OTHER";
                break;
        }

        const char* type_str = "UNKNOWN";

        switch (type) {
            case GL_DEBUG_TYPE_ERROR:
                type_str = "ERROR";
                break;

            case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
                type_str = "DEPRECATED BEHAVIOR";
                break;

            case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
                type_str = "UNDEFINED BEHAVIOR";
                break;

            case GL_DEBUG_TYPE_PORTABILITY:
                type_str = "PORTABILITY";
                break;

            case GL_DEBUG_TYPE_PERFORMANCE:
                type_str = "PERFORMANCE";
                break;

            case GL_DEBUG_TYPE_MARKER:
                type_str = "MARKER";
                break;

            case GL_DEBUG_TYPE_PUSH_GROUP:
                type_str = "PUSH GROUP";
                break;

            case GL_DEBUG_TYPE_POP_GROUP:
                type_str = "POP GROUP";
                break;

            case GL_DEBUG_TYPE_OTHER:
                type_str = "OTHER";
                break;
        }

        const char* severity_str = "UNKNOWN";

        switch (severity) {
            case GL_DEBUG_SEVERITY_HIGH:
                severity_str = "HIGH";
                break;

            case GL_DEBUG_SEVERITY_MEDIUM:
                severity_str = "MEDIUM";
                break;

            case GL_DEBUG_SEVERITY_LOW:
                severity_str = "LOW";
                break;

            case GL_DEBUG_SEVERITY_NOTIFICATION:
                severity_str = "NOTIFICATION";
                break;
        }

        gl_err << "Debug callback\n"
               << "- Source:   " << source_str << '\n'
               << "- Type:     " << type_str << '\n'
               << "- Severity: " << severity_str << '\n'
               << "- ID:       " << id << '\n'
               << "- Message:  " << message;

        // Stop in the debugger on serious OpenGL errors.
#    if defined(__GNUC__) || defined(__clang__)
        if (severity == GL_DEBUG_SEVERITY_HIGH) {
            __builtin_trap();
        }
#    endif
    }

    inline void enable_debugging() {
        GLint flags = 0;

        glGetIntegerv(GL_CONTEXT_FLAGS, &flags);

        if (!(flags & GL_CONTEXT_FLAG_DEBUG_BIT)) {
            gl_err << "WARNING: OpenGL debug context was not created.";

            return;
        }

        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);

        glDebugMessageCallback(debug_callback, nullptr);

        // Enable all messages except notifications.
        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE,
            GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr, GL_FALSE);

        gl_log << "OpenGL debug output enabled.";
    }
#endif

    struct ShaderUniform {
        GLint id;
        inline GLint init(const char* name) {
            return id = glGetUniformLocation(id, name);
        }
        inline operator GLint() const { return id; }
        inline operator GLint&() { return id; }
    };

    struct VBO {
        GLuint id = 0;
        bool initialized = false;
        inline operator GLuint() const { return id; }
        inline operator GLuint&() { return id; }
        inline operator bool() const { return initialized; }

        inline void create() {
            if (initialized) return;

            glGenBuffers(1, &id);

            initialized = true;
        }

        inline void bind() const { glBindBuffer(GL_ARRAY_BUFFER, id); }

        inline void destroy() {
            if (!initialized) return;

            glDeleteBuffers(1, &id);

            id = 0;
            initialized = false;
        }

        template <typename T>
        inline void data(
            std::span<const T> data_to_upload, GLenum render_mode) {
            glBufferData(GL_ARRAY_BUFFER,
                static_cast<GLsizeiptr>(data_to_upload.size() * sizeof(T)),
                data_to_upload.data(), render_mode);
        }

        inline void data(const void* data, size_t size, GLenum render_mode) {
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(size), data,
                render_mode);
        }

        inline ~VBO() { destroy(); }
    };

    struct Shader {
        GLuint id = 0;
        bool initialized = false;
        inline operator GLuint() const { return id; }
        inline operator GLuint&() { return id; }
        inline operator bool() const { return initialized; }

        GLenum type;

        inline explicit Shader(GLenum type) : type(type) {}

        inline void create() {
            if (initialized) return;

            id = glCreateShader(type);

            if (id == 0) {
                throw std::runtime_error("Failed to create OpenGL shader");
            }

            initialized = true;
        }

        static constexpr std::string_view preamble =
#ifdef __EMSCRIPTEN__
            "#version 300 es\nprecision mediump float;\n";
#else
            "#version 430 core\n";
#endif

        inline void source_from_strview(
            std::string_view source, bool raw = false) {
            if (raw) {
                const GLchar* str = source.data();
                const GLint length = static_cast<GLint>(source.size());
                glShaderSource(id, 1, &str, &length);
                return;
            }

            std::string new_source;
            new_source.reserve(preamble.size() + source.size());
            new_source.append(preamble);
            new_source.append(source);

            source_from_strview(new_source, true);
        }

        /// If it's not raw then it prepends the `preamble` string
        /// so that the shader is cross-compatible
        inline void source_from_path(
            const std::string& path, bool raw = false) {
            source_from_strview(load_asset(path), raw);
        }

        inline void compile() {
            glCompileShader(id);

            GLint success = GL_FALSE;

            glGetShaderiv(id, GL_COMPILE_STATUS, &success);

            if (success) return;

            GLint log_length = 0;

            glGetShaderiv(id, GL_INFO_LOG_LENGTH, &log_length);

            std::string info_log(
                static_cast<size_t>(std::max(log_length, 1)), '\0');

            glGetShaderInfoLog(id, log_length, nullptr, info_log.data());

            gl_err << "Shader compilation failed:\n" << info_log;

            throw std::logic_error("Shader compilation failed");
        }

        inline void destroy() {
            if (!initialized) return;

            glDeleteShader(id);

            id = 0;
            initialized = false;
        }

        inline ~Shader() { destroy(); }
    };

    struct ShaderProgram {
        GLuint id = 0;
        bool initialized = false;
        inline operator GLuint() const { return id; }
        inline operator GLuint&() { return id; }
        inline operator bool() const { return initialized; }

        inline void create() {
            if (initialized) return;

            id = glCreateProgram();

            if (id == 0) {
                throw std::runtime_error(
                    "Failed to create OpenGL shader program");
            }

            initialized = true;
        }

        inline void attach(const Shader& shader) {
            glAttachShader(id, shader.id);
        }

        inline void attach(GLuint shaderid) { glAttachShader(id, shaderid); }

        inline void link() {
            glLinkProgram(id);

            GLint success = GL_FALSE;

            glGetProgramiv(id, GL_LINK_STATUS, &success);

            if (success) return;

            GLint log_length = 0;

            glGetProgramiv(id, GL_INFO_LOG_LENGTH, &log_length);

            std::string info_log(
                static_cast<size_t>(std::max(log_length, 1)), '\0');

            glGetProgramInfoLog(id, log_length, nullptr, info_log.data());

            gl_err << "Shader linking failed:\n" << info_log;

            throw std::logic_error("Shader linking failed");
        }

        inline void use() const { glUseProgram(id); }

        inline void destroy() {
            if (!initialized) return;

            glDeleteProgram(id);

            id = 0;
            initialized = false;
        }

        inline GLint get_uniform_location(
            const std::string& name, bool throw_if_not_found) {
            auto retval = glGetUniformLocation(id, name.c_str());
            if (throw_if_not_found && retval < 0)
                throw std::runtime_error("Uniform " + name + " was not found.");
            return retval;
        }

        inline ~ShaderProgram() { destroy(); }
    };

    struct VAO {
        GLuint id = 0;
        bool initialized = false;
        inline operator GLuint() const { return id; }
        inline operator GLuint&() { return id; }
        inline operator bool() const { return initialized; }

        inline void create() {
            if (initialized) return;

            glGenVertexArrays(1, &id);

            initialized = true;
        }

        inline void bind() const { glBindVertexArray(id); }

        inline void destroy() {
            if (!initialized) return;

            glDeleteVertexArrays(1, &id);

            id = 0;
            initialized = false;
        }

        inline void vertexAttribPointer(GLuint index, GLint count, GLenum type,
            GLboolean normalized, GLsizei stride, size_t offset) {
            glVertexAttribPointer(index, count, type, normalized, stride,
                reinterpret_cast<const void*>(offset));
        }

        inline void enableAttribute(GLuint index) {
            glEnableVertexAttribArray(index);
        }

        inline ~VAO() { destroy(); }
    };

    struct Texture {
        GLuint id = 0;
        bool initialized = false;
        inline operator GLuint() const { return id; }
        inline operator GLuint&() { return id; }
        inline operator bool() const { return initialized; }

        inline void create() {
            if (initialized) return;
            glGenTextures(1, &id);
            initialized = true;
        }
        inline void destroy() {
            if (!initialized) return;
            initialized = false;
        }
        inline void generateMipmap() const { glGenerateMipmap(GL_TEXTURE_2D); }
        inline void bind() const { glBindTexture(GL_TEXTURE_2D, id); }
    };

    inline void handle_error() {
        const auto upload_error = glGetError();
        if (upload_error == GL_NO_ERROR) return;

        gl_err << "Failed to upload texture atlas: " << upload_error;
        throw std::runtime_error(
            "Failed to upload texture atlas: " + std::to_string(upload_error));
    }
}  // namespace opengl
