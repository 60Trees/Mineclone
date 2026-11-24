// <AI>
#include <SDL3/SDL.h>
#include <iostream>
#include <triolib.hpp>

#include <opengl.hpp>

namespace opengl {
    logger::Logger gl_log = console.title(
                       BLUE "OPEN" GREEN "GL", console.generate_from_ostream(std::cout)),
                   gl_err = console.title(
                       RED "OPEN" YELLOW "GL", console.generate_from_ostream(std::cerr));
}  // namespace opengl

void set_opengl_version() {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, GL_VERSION_MAJOR);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, GL_VERSION_MINOR);

    // SDL defaults to a 16-bit depth buffer. With a perspective projection almost all
    // of the 16-bit range is spent in the first few centimetres in front of the camera,
    // so distant geometry only gets a handful of distinct depth values. Ask for 24 bits
    // before the window (and therefore the GL context) is created.
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    opengl::gl_log << "Requesting OpenGL " BLUE << GL_VERSION_MAJOR << "."
                   << GL_VERSION_MINOR << RESET;

#ifndef __EMSCRIPTEN__
    SDL_GL_SetAttribute(
        SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

#    ifdef DO_GL_DEBUG
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
#    endif
#endif
}
// </AI>
