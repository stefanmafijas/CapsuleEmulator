#pragma once
#include <SDL.h>
#include <SDL_opengl.h>

// OpenGL entry points past 1.1, loaded with gl::load() once a context is current.
#define GL_FUNCS(X)                                                   \
    X(PFNGLGENFRAMEBUFFERSPROC, GenFramebuffers)                      \
    X(PFNGLDELETEFRAMEBUFFERSPROC, DeleteFramebuffers)                \
    X(PFNGLBINDFRAMEBUFFERPROC, BindFramebuffer)                      \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, FramebufferTexture2D)            \
    X(PFNGLGENRENDERBUFFERSPROC, GenRenderbuffers)                    \
    X(PFNGLDELETERENDERBUFFERSPROC, DeleteRenderbuffers)              \
    X(PFNGLBINDRENDERBUFFERPROC, BindRenderbuffer)                    \
    X(PFNGLRENDERBUFFERSTORAGEPROC, RenderbufferStorage)              \
    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, FramebufferRenderbuffer)      \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, CheckFramebufferStatus)        \
    X(PFNGLGENERATEMIPMAPPROC, GenerateMipmap)                        \
    X(PFNGLBINDBUFFERPROC, BindBuffer)

namespace gl {
#define X(type, name) inline type name = nullptr;
GL_FUNCS(X)
#undef X

inline bool load() {
    bool ok = true;
#define X(type, name) ok &= (name = (type)SDL_GL_GetProcAddress("gl" #name)) != nullptr;
    GL_FUNCS(X)
#undef X
    return ok;
}
} // namespace gl
