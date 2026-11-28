// SynthyBoy - MPC overlay GPU helper: the few EGL / OpenGL ES 2 / GBM declarations the helper uses, and a loader that finds the functions with dlopen / dlsym at run time.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Why dlopen and no link-time libraries: the build image (Debian 12) has no Mesa headers or libraries and the repository must not carry the device's binaries. The ABIs used here
// are stable (Khronos registry values, Mesa's gbm.h); the helper is linked against libc / libdl only, and on the device it opens libEGL.so.1, libgbm.so.1 and libGLESv2.so.2 of the
// firmware itself. A missing library or function is a clean `error` message to the plugin (which then keeps the CPU path), not a crash at start.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

#include <dlfcn.h>

// ---- EGL
typedef void* EGLDisplay;
typedef void* EGLConfig;
typedef void* EGLContext;
typedef void* EGLSurface;
typedef void* EGLNativeDisplayType;
typedef void* EGLNativeWindowType;
typedef int32_t EGLint;
typedef unsigned int EGLBoolean;
typedef unsigned int EGLenum;
#define EGL_NO_DISPLAY ((EGLDisplay) 0)
#define EGL_NO_CONTEXT ((EGLContext) 0)
#define EGL_NO_SURFACE ((EGLSurface) 0)
#define EGL_NONE 0x3038
#define EGL_ALPHA_SIZE 0x3021
#define EGL_BLUE_SIZE 0x3022
#define EGL_GREEN_SIZE 0x3023
#define EGL_RED_SIZE 0x3024
#define EGL_SAMPLES 0x3031
#define EGL_SURFACE_TYPE 0x3033
#define EGL_NATIVE_VISUAL_ID 0x302E
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_WINDOW_BIT 0x0004
#define EGL_OPENGL_ES2_BIT 0x0004
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#define EGL_OPENGL_ES_API 0x30A0
#define EGL_PLATFORM_GBM_KHR 0x31D7

// ---- GLES2
typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef float GLfloat;
typedef unsigned char GLubyte;
typedef char GLchar;
typedef unsigned int GLbitfield;
typedef unsigned char GLboolean;
typedef ptrdiff_t GLsizeiptr;
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_TRIANGLES 4
#define GL_TRIANGLE_STRIP 5
#define GL_BLEND 0x0BE2
#define GL_ONE 1
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_FLOAT 0x1406
#define GL_UNSIGNED_BYTE 0x1401
#define GL_RGBA 0x1908
#define GL_ARRAY_BUFFER 0x8892
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_RENDERER 0x1F01
#define GL_EXTENSIONS 0x1F03
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_LINEAR 0x2601
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_ALPHA 0x1906
#define GL_TEXTURE0 0x84C0
#define GL_FRAMEBUFFER 0x8D40
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_SAMPLES 0x80A9
#define GL_SCISSOR_TEST 0x0C11

// ---- GBM (Mesa gbm.h)
struct gbm_device;
struct gbm_bo;
struct gbm_surface;
union gbm_bo_handle { void* ptr; int32_t s32; uint32_t u32; int64_t s64; uint64_t u64; };
#define GBM_FORMAT_ARGB8888 0x34325241u   // fourcc 'AR24'
#define GBM_BO_USE_SCANOUT (1u << 0)
#define GBM_BO_USE_RENDERING (1u << 2)

namespace sbgpu {

/** The functions, as pointers. Filled by Api::load (). */
struct Api
{
#define SB_EGL_FUNCS(X) \
    X (EGLDisplay, eglGetPlatformDisplay, (EGLenum platform, void* native, const intptr_t* attribs)) \
    X (EGLDisplay, eglGetDisplay, (EGLNativeDisplayType d)) \
    X (EGLBoolean, eglInitialize, (EGLDisplay d, EGLint* major, EGLint* minor)) \
    X (EGLBoolean, eglTerminate, (EGLDisplay d)) \
    X (EGLBoolean, eglBindAPI, (EGLenum api)) \
    X (EGLBoolean, eglGetConfigs, (EGLDisplay d, EGLConfig* cfgs, EGLint size, EGLint* num)) \
    X (EGLBoolean, eglGetConfigAttrib, (EGLDisplay d, EGLConfig c, EGLint attr, EGLint* value)) \
    X (EGLContext, eglCreateContext, (EGLDisplay d, EGLConfig c, EGLContext share, const EGLint* attribs)) \
    X (EGLBoolean, eglDestroyContext, (EGLDisplay d, EGLContext c)) \
    X (EGLSurface, eglCreateWindowSurface, (EGLDisplay d, EGLConfig c, EGLNativeWindowType w, const EGLint* attribs)) \
    X (EGLBoolean, eglDestroySurface, (EGLDisplay d, EGLSurface s)) \
    X (EGLBoolean, eglMakeCurrent, (EGLDisplay d, EGLSurface draw, EGLSurface read, EGLContext c)) \
    X (EGLBoolean, eglSwapBuffers, (EGLDisplay d, EGLSurface s)) \
    X (EGLBoolean, eglSwapInterval, (EGLDisplay d, EGLint interval)) \
    X (EGLBoolean, eglQuerySurface, (EGLDisplay d, EGLSurface s, EGLint attr, EGLint* value)) \
    X (const char*, eglQueryString, (EGLDisplay d, EGLint name)) \
    X (void*, eglGetProcAddress, (const char* name)) \
    X (EGLint, eglGetError, (void))
#define SB_GL_FUNCS(X) \
    X (GLenum, glGetError, (void)) \
    X (const GLubyte*, glGetString, (GLenum name)) \
    X (GLuint, glCreateShader, (GLenum type)) \
    X (void, glShaderSource, (GLuint s, GLsizei count, const GLchar* const* str, const GLint* len)) \
    X (void, glCompileShader, (GLuint s)) \
    X (void, glGetShaderiv, (GLuint s, GLenum pname, GLint* p)) \
    X (void, glGetShaderInfoLog, (GLuint s, GLsizei max, GLsizei* len, GLchar* log)) \
    X (GLuint, glCreateProgram, (void)) \
    X (void, glAttachShader, (GLuint p, GLuint s)) \
    X (void, glBindAttribLocation, (GLuint p, GLuint idx, const GLchar* name)) \
    X (void, glLinkProgram, (GLuint p)) \
    X (void, glGetProgramiv, (GLuint p, GLenum pname, GLint* v)) \
    X (void, glGetProgramInfoLog, (GLuint p, GLsizei max, GLsizei* len, GLchar* log)) \
    X (void, glUseProgram, (GLuint p)) \
    X (GLint, glGetUniformLocation, (GLuint p, const GLchar* name)) \
    X (void, glUniform1f, (GLint loc, GLfloat v)) \
    X (void, glUniform3f, (GLint loc, GLfloat a, GLfloat b, GLfloat c)) \
    X (void, glUniform4f, (GLint loc, GLfloat a, GLfloat b, GLfloat c, GLfloat d)) \
    X (void, glGenBuffers, (GLsizei n, GLuint* b)) \
    X (void, glBindBuffer, (GLenum target, GLuint b)) \
    X (void, glBufferData, (GLenum target, GLsizeiptr size, const void* data, GLenum usage)) \
    X (void, glEnableVertexAttribArray, (GLuint i)) \
    X (void, glVertexAttribPointer, (GLuint i, GLint size, GLenum type, GLboolean norm, GLsizei stride, const void* ptr)) \
    X (void, glViewport, (GLint x, GLint y, GLsizei w, GLsizei h)) \
    X (void, glClearColor, (GLfloat r, GLfloat g, GLfloat b, GLfloat a)) \
    X (void, glClear, (GLbitfield mask)) \
    X (void, glEnable, (GLenum cap)) \
    X (void, glDisable, (GLenum cap)) \
    X (void, glScissor, (GLint x, GLint y, GLsizei w, GLsizei h)) \
    X (void, glBlendFunc, (GLenum s, GLenum d)) \
    X (void, glDrawArrays, (GLenum mode, GLint first, GLsizei count)) \
    X (void, glFinish, (void)) \
    X (void, glReadPixels, (GLint x, GLint y, GLsizei w, GLsizei h, GLenum fmt, GLenum type, void* data)) \
    X (void, glGenTextures, (GLsizei n, GLuint* t)) \
    X (void, glDeleteTextures, (GLsizei n, const GLuint* t)) \
    X (void, glBindTexture, (GLenum target, GLuint t)) \
    X (void, glTexImage2D, (GLenum target, GLint level, GLint internalFormat, GLsizei w, GLsizei h, GLint border, GLenum format, GLenum type, const void* data)) \
    X (void, glTexParameteri, (GLenum target, GLenum pname, GLint param)) \
    X (void, glActiveTexture, (GLenum unit)) \
    X (void, glUniform1i, (GLint loc, GLint v)) \
    X (void, glUniform2f, (GLint loc, GLfloat a, GLfloat b)) \
    X (void, glCopyTexSubImage2D, (GLenum target, GLint level, GLint xoff, GLint yoff, GLint x, GLint y, GLsizei w, GLsizei h)) \
    X (void, glGenFramebuffers, (GLsizei n, GLuint* f)) \
    X (void, glDeleteFramebuffers, (GLsizei n, const GLuint* f)) \
    X (void, glBindFramebuffer, (GLenum target, GLuint f)) \
    X (void, glFramebufferTexture2D, (GLenum target, GLenum attachment, GLenum textarget, GLuint tex, GLint level)) \
    X (GLenum, glCheckFramebufferStatus, (GLenum target)) \
    X (void, glPixelStorei, (GLenum pname, GLint param)) \
    X (void, glGetIntegerv, (GLenum pname, GLint* v))
#define SB_GBM_FUNCS(X) \
    X (struct gbm_device*, gbm_create_device, (int fd)) \
    X (void, gbm_device_destroy, (struct gbm_device* g)) \
    X (struct gbm_surface*, gbm_surface_create, (struct gbm_device* g, uint32_t w, uint32_t h, uint32_t format, uint32_t flags)) \
    X (void, gbm_surface_destroy, (struct gbm_surface* s)) \
    X (struct gbm_bo*, gbm_surface_lock_front_buffer, (struct gbm_surface* s)) \
    X (void, gbm_surface_release_buffer, (struct gbm_surface* s, struct gbm_bo* bo)) \
    X (int, gbm_bo_get_fd, (struct gbm_bo* bo)) \
    X (union gbm_bo_handle, gbm_bo_get_handle, (struct gbm_bo* bo)) \
    X (uint32_t, gbm_bo_get_stride, (struct gbm_bo* bo)) \
    X (uint32_t, gbm_bo_get_width, (struct gbm_bo* bo)) \
    X (uint32_t, gbm_bo_get_height, (struct gbm_bo* bo))
#define SB_DECLARE(ret, name, args) ret (*name) args = nullptr;
    SB_EGL_FUNCS (SB_DECLARE)
    SB_GL_FUNCS (SB_DECLARE)
    SB_GBM_FUNCS (SB_DECLARE)
#undef SB_DECLARE

    /** Opens one library: the loader's own search first (the soname, then the plain name), then the places a firmware keeps its Mesa libraries (the directory in
        SYNTHYBOY_OVERLAY_GL_DIR first when it is set). `error` lists what was tried when nothing opens. */
    static void* openLibrary (const char* soname, const char* plain, std::string& error)
    {
        static const char* const kDirs[] = { "/usr/lib/", "/usr/lib64/", "/lib/", "/usr/local/lib/", "/usr/lib/aarch64-linux-gnu/", "/usr/lib/arm-linux-gnueabihf/", "/lib/aarch64-linux-gnu/", "/lib/arm-linux-gnueabihf/" };
        std::string tried;
        auto attempt = [&] (const std::string& path) -> void*
        {
            void* h = dlopen (path.c_str(), RTLD_NOW | RTLD_GLOBAL);
            if (! h)
                tried += (tried.empty() ? "" : "; ") + path;
            return h;
        };
        if (const char* env = std::getenv ("SYNTHYBOY_OVERLAY_GL_DIR"))
            if (*env)
            {
                const std::string dir = std::string (env) + "/";
                if (void* h = attempt (dir + soname)) return h;
                if (void* h = attempt (dir + plain)) return h;
            }
        if (void* h = attempt (soname)) return h;
        if (void* h = attempt (plain)) return h;
        for (const char* dir : kDirs)
        {
            if (void* h = attempt (std::string (dir) + soname)) return h;
            if (void* h = attempt (std::string (dir) + plain)) return h;
        }
        error = std::string (soname) + " not found (tried " + tried + ")";
        return nullptr;
    }

    /** Opens the three libraries and looks every function up. false: `error` names the first thing that is missing. */
    bool load (std::string& error)
    {
        void* egl = openLibrary ("libEGL.so.1", "libEGL.so", error);
        if (! egl)
            return false;
        void* gbm = openLibrary ("libgbm.so.1", "libgbm.so", error);
        if (! gbm)
            return false;
        void* gles = openLibrary ("libGLESv2.so.2", "libGLESv2.so", error);
        if (! gles)
            return false;
#define SB_LOAD(lib, ret, name, args) \
        name = reinterpret_cast<ret (*) args> (dlsym (lib, #name)); \
        if (! name && std::string (#name) != "eglGetPlatformDisplay") { error = std::string ("missing function ") + #name; return false; }
#define SB_LOAD_EGL(ret, name, args) SB_LOAD (egl, ret, name, args)
#define SB_LOAD_GL(ret, name, args) SB_LOAD (gles, ret, name, args)
#define SB_LOAD_GBM(ret, name, args) SB_LOAD (gbm, ret, name, args)
        SB_EGL_FUNCS (SB_LOAD_EGL)
        SB_GL_FUNCS (SB_LOAD_GL)
        SB_GBM_FUNCS (SB_LOAD_GBM)
#undef SB_LOAD
#undef SB_LOAD_EGL
#undef SB_LOAD_GL
#undef SB_LOAD_GBM
        return true;
    }
};

} // namespace sbgpu
