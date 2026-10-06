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
