// SynthyBoy - MPC overlay: the GPU helper process (synthyboy-overlay-gpu). Draws the overlay scenes (gpu/scene.h) with OpenGL ES on the GPU of the device and puts the finished
// buffer on the display plane without a copy. Started by the plugin (gpu/helper_transport_linux.h); never run by hand.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// What it does, in order:
//   hello      the plugin sends the display descriptor (the MPC application's own open file description, so this process has the application's rights on plane 38) and the memfd
//              of the shared block with SCM_RIGHTS on descriptor 3 (the socket); the helper opens /dev/dri/renderD128 (GBM), EGL, a GLES2 context without a surface, the shaders
//   ready      answer with the GL renderer and the start-up time
//   show       the region (landscape canvas w x h, plane position on the panel, rotation): a GBM surface of the panel's portrait size h x w (ARGB8888, scanout | rendering)
//   scene      the plugin wrote a scene under the seqlock: read it, draw it (the rotation is done in the vertex shader), glFinish, swap, take the finished buffer object, import it as a
//              framebuffer on the display descriptor (PRIME_FD_TO_HANDLE + ADDFB2, cached per buffer object), legacy SETPLANE on plane 38 (it waits for the vblank), release the
//              buffer that was on screen before, answer `frame` with the timings
//   hide       plane off, framebuffers removed, GEM handles closed, surface destroyed; answer `hidden`
//   quit / end of the socket / SIGTERM: the same clean-up, then exit
// Every DRM object this process makes on the shared descriptor is in the ledger of the shared block, so that the plugin can remove it when this process is killed.
// Never touched: client capabilities, mode sets, atomic commits, planes other than the one in the hello. Ordinary scheduling (no real time), never on the isolated audio cores
// (the affinity is inherited from the plugin's overlay thread).
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <drm/drm.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_mode.h>

#include "../gpu/scene.h"
#include "gl_api.h"

using namespace synthyboy_mpc::overlay::gpu;

namespace {

volatile sig_atomic_t g_stop = 0;
void onSignal (int) { g_stop = 1; }

double nowMs()
{
    timespec t;
    clock_gettime (CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

constexpr int kSock = 3;

struct Vtx { float x, y, d0, d1; };
struct Draw
{
    int prim = 0, first = 0, count = 0, kind = 0;   // kind: 0 solid, 1 feathered line (d0 = signed distance), 2 disc (d0, d1 = offset from the centre)
    float colour[4] = {};                           // premultiplied
    float half = 0.0f;                              // half width of a line, radius of a disc
    bool clear = false;                             // a glClear with `colour`
};

class Helper
{
public:
    int run()
    {
        startMs_ = nowMs();
        if (! receiveHello())
            return 1;
        std::string error;
        if (! initGl (error))
        {
            sendError (error);
            cleanupAll();
            return 1;
        }
        char text[32];
        std::snprintf (text, sizeof text, "%s", reinterpret_cast<const char*> (api_.glGetString (GL_RENDERER)));
        send (makeMsg (kMsgReady, static_cast<uint32_t> (nowMs() - startMs_ + 0.5), 0, 0, 0, 0, 0, text));
        std::fprintf (stderr, "helper %d: ready in %.1f ms, renderer %s, drm fd %d\n", static_cast<int> (getpid()), nowMs() - startMs_, text, drmFd_);
        loop();
        cleanupAll();
        return exitCode_;
    }

private:
    // ---------------------------------------------------------------------------------------------------------------------------------------------------------
    // the channel
    bool send (const Msg& m) { return ::send (kSock, &m, sizeof m, MSG_NOSIGNAL) == static_cast<ssize_t> (sizeof m); }
    void sendError (const std::string& what)
    {
        std::fprintf (stderr, "helper %d: error: %s\n", static_cast<int> (getpid()), what.c_str());
        send (makeMsg (kMsgError, 0, 0, 0, 0, 0, 0, what.c_str()));
        exitCode_ = 1;
    }

    bool receiveHello()
    {
        pollfd p { kSock, POLLIN, 0 };
        if (::poll (&p, 1, 8000) <= 0)
        {
            std::fprintf (stderr, "helper: no hello\n");
            return false;
        }
        Msg m;
        iovec io { &m, sizeof m };
        alignas (cmsghdr) char ctl[CMSG_SPACE (sizeof (int) * 4)];
        msghdr mh;
        std::memset (&mh, 0, sizeof mh);
        mh.msg_iov = &io;
        mh.msg_iovlen = 1;
        mh.msg_control = ctl;
        mh.msg_controllen = sizeof ctl;
        if (::recvmsg (kSock, &mh, MSG_CMSG_CLOEXEC) != static_cast<ssize_t> (sizeof m) || m.magic != kMsgMagic || m.type != kMsgHello)
        {
            std::fprintf (stderr, "helper: bad hello\n");
            return false;
        }
        int fds[4];
        int n = 0;
        for (cmsghdr* c = CMSG_FIRSTHDR (&mh); c; c = CMSG_NXTHDR (&mh, c))
            if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS)
            {
                n = static_cast<int> ((c->cmsg_len - CMSG_LEN (0)) / sizeof (int));
                if (n > 4)
                    n = 4;
                std::memcpy (fds, CMSG_DATA (c), sizeof (int) * static_cast<size_t> (n));
            }
        if (n < 2 || m.a != kProtocolVersion)
        {
            sendError (n < 2 ? "hello without descriptors" : "protocol version mismatch");
            return false;
        }
        drmFd_ = fds[0];
        crtc_ = m.b;
        plane_ = m.c;
        flags_ = m.d;
        void* mem = ::mmap (nullptr, sizeof (SharedBlock), PROT_READ | PROT_WRITE, MAP_SHARED, fds[1], 0);
        ::close (fds[1]);
        if (mem == MAP_FAILED)
        {
            sendError (std::string ("mmap of the shared block: ") + std::strerror (errno));
            return false;
        }
        block_ = static_cast<SharedBlock*> (mem);
        if (! blockValid (block_))
        {
            sendError ("the shared block is not valid");
            return false;
        }
        return true;
    }

    // ---------------------------------------------------------------------------------------------------------------------------------------------------------
    // GL
    static constexpr const char* kVs =
        "attribute vec2 aPos; attribute vec2 aD; varying vec2 vD; uniform vec3 uRowX; uniform vec3 uRowY;\n"
        "void main() { vD = aD; gl_Position = vec4 (dot (uRowX.xy, aPos) + uRowX.z, dot (uRowY.xy, aPos) + uRowY.z, 0.0, 1.0); }\n";
    // premultiplied output: kind 0 solid, kind 1 feathered line (coverage 1 px edge), kind 2 disc (coverage from the distance to the centre)
    static constexpr const char* kFs =
        "precision mediump float; varying vec2 vD; uniform float uKind; uniform vec4 uC0; uniform float uHalf;\n"
        "void main() {\n"
        "  float cov = 1.0;\n"
        "  if (uKind > 1.5) cov = clamp (uHalf + 0.5 - length (vD), 0.0, 1.0);\n"
        "  else if (uKind > 0.5) cov = clamp (uHalf + 0.5 - abs (vD.x), 0.0, 1.0);\n"
        "  gl_FragColor = uC0 * cov;\n"
        "}\n";

    GLuint compile (GLenum type, const char* src, std::string& error)
    {
        GLuint s = api_.glCreateShader (type);
        api_.glShaderSource (s, 1, &src, nullptr);
        api_.glCompileShader (s);
        GLint ok = 0;
        api_.glGetShaderiv (s, GL_COMPILE_STATUS, &ok);
        if (! ok)
        {
            char log[512] = {};
            api_.glGetShaderInfoLog (s, sizeof log, nullptr, log);
            error = std::string ("shader: ") + log;
            return 0;
        }
        return s;
    }

    bool initGl (std::string& error)
    {
        if (! api_.load (error))
            return false;
        gbmFd_ = ::open ("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
        if (gbmFd_ < 0)
        {
            error = std::string ("open renderD128: ") + std::strerror (errno);
            return false;
        }
        gbm_ = api_.gbm_create_device (gbmFd_);
        if (! gbm_)
        {
            error = "gbm_create_device failed";
            return false;
        }
        dpy_ = EGL_NO_DISPLAY;
        if (api_.eglGetPlatformDisplay)
            dpy_ = api_.eglGetPlatformDisplay (EGL_PLATFORM_GBM_KHR, gbm_, nullptr);
        if (dpy_ == EGL_NO_DISPLAY)
            dpy_ = api_.eglGetDisplay (reinterpret_cast<EGLNativeDisplayType> (gbm_));
        EGLint major = 0, minor = 0;
        if (dpy_ == EGL_NO_DISPLAY || ! api_.eglInitialize (dpy_, &major, &minor))
        {
            error = "eglInitialize failed";
            return false;
        }
        api_.eglBindAPI (EGL_OPENGL_ES_API);
        EGLint n = 0;
        api_.eglGetConfigs (dpy_, nullptr, 0, &n);
        std::vector<EGLConfig> all (static_cast<size_t> (n > 0 ? n : 1));
        api_.eglGetConfigs (dpy_, all.data(), n, &n);
        cfg_ = nullptr;
        for (int i = 0; i < n && ! cfg_; ++i)
        {
            EGLint rt = 0, st = 0, r = 0, g = 0, b = 0, a = 0, s = 0, vis = 0;
            api_.eglGetConfigAttrib (dpy_, all[static_cast<size_t> (i)], EGL_RENDERABLE_TYPE, &rt);
            api_.eglGetConfigAttrib (dpy_, all[static_cast<size_t> (i)], EGL_SURFACE_TYPE, &st);
            api_.eglGetConfigAttrib (dpy_, all[static_cast<size_t> (i)], EGL_RED_SIZE, &r);
            api_.eglGetConfigAttrib (dpy_, all[static_cast<size_t> (i)], EGL_GREEN_SIZE, &g);
            api_.eglGetConfigAttrib (dpy_, all[static_cast<size_t> (i)], EGL_BLUE_SIZE, &b);
            api_.eglGetConfigAttrib (dpy_, all[static_cast<size_t> (i)], EGL_ALPHA_SIZE, &a);
            api_.eglGetConfigAttrib (dpy_, all[static_cast<size_t> (i)], EGL_SAMPLES, &s);
            api_.eglGetConfigAttrib (dpy_, all[static_cast<size_t> (i)], EGL_NATIVE_VISUAL_ID, &vis);
            if ((rt & EGL_OPENGL_ES2_BIT) && (st & EGL_WINDOW_BIT) && r == 8 && g == 8 && b == 8 && a == 8 && s == 0 && static_cast<uint32_t> (vis) == GBM_FORMAT_ARGB8888)
                cfg_ = all[static_cast<size_t> (i)];
        }
        if (! cfg_)
        {
            error = "no EGL config (ES2, window, ARGB8888)";
            return false;
        }
        const EGLint ca[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
        ctx_ = api_.eglCreateContext (dpy_, cfg_, EGL_NO_CONTEXT, ca);
        if (ctx_ == EGL_NO_CONTEXT)
        {
            error = "eglCreateContext failed";
            return false;
        }
        // without a surface (EGL_KHR_surfaceless_context): the shaders can be made before any region is known
        if (! api_.eglMakeCurrent (dpy_, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx_))
        {
            error = "eglMakeCurrent without a surface failed (no surfaceless context)";
            return false;
        }
        GLuint vs = compile (GL_VERTEX_SHADER, kVs, error), fs = compile (GL_FRAGMENT_SHADER, kFs, error);
        if (! vs || ! fs)
            return false;
        prog_ = api_.glCreateProgram();
        api_.glAttachShader (prog_, vs);
        api_.glAttachShader (prog_, fs);
        api_.glBindAttribLocation (prog_, 0, "aPos");
        api_.glBindAttribLocation (prog_, 1, "aD");
        api_.glLinkProgram (prog_);
        GLint ok = 0;
        api_.glGetProgramiv (prog_, GL_LINK_STATUS, &ok);
        if (! ok)
        {
            char log[512] = {};
            api_.glGetProgramInfoLog (prog_, sizeof log, nullptr, log);
            error = std::string ("link: ") + log;
            return false;
        }
        api_.glUseProgram (prog_);
        uRowX_ = api_.glGetUniformLocation (prog_, "uRowX");
        uRowY_ = api_.glGetUniformLocation (prog_, "uRowY");
        uKind_ = api_.glGetUniformLocation (prog_, "uKind");
        uC0_ = api_.glGetUniformLocation (prog_, "uC0");
        uHalf_ = api_.glGetUniformLocation (prog_, "uHalf");
        api_.glGenBuffers (1, &vbo_);
        api_.glBindBuffer (GL_ARRAY_BUFFER, vbo_);
        api_.glEnableVertexAttribArray (0);
        api_.glEnableVertexAttribArray (1);
        api_.glVertexAttribPointer (0, 2, GL_FLOAT, 0, sizeof (Vtx), reinterpret_cast<const void*> (0));
        api_.glVertexAttribPointer (1, 2, GL_FLOAT, 0, sizeof (Vtx), reinterpret_cast<const void*> (8));
        api_.glEnable (GL_BLEND);
        api_.glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        verts_.reserve (8192);
        draws_.reserve (64);
        return api_.glGetError() == 0;
    }

    // ---------------------------------------------------------------------------------------------------------------------------------------------------------
    // DRM (on the shared descriptor)
    bool planeSet (uint32_t fb)
    {
        drm_mode_set_plane s;
        std::memset (&s, 0, sizeof s);
        s.plane_id = plane_;
        s.crtc_id = crtc_;
        s.fb_id = fb;
        s.crtc_x = panelX_;
        s.crtc_y = panelY_;
        s.crtc_w = static_cast<uint32_t> (bw_);
        s.crtc_h = static_cast<uint32_t> (bh_);
        s.src_w = static_cast<uint32_t> (bw_) << 16;
        s.src_h = static_cast<uint32_t> (bh_) << 16;
        if (ioctl (drmFd_, DRM_IOCTL_MODE_SETPLANE, &s) < 0)
            return false;
        planeOn_ = true;
        return true;
    }
    void planeOff()
    {
        if (drmFd_ < 0)
            return;
        drm_mode_set_plane s;
        std::memset (&s, 0, sizeof s);
        s.plane_id = plane_;
        ioctl (drmFd_, DRM_IOCTL_MODE_SETPLANE, &s);
        planeOn_ = false;
    }

    struct Import { uint32_t key = 0, handle = 0, fb = 0; };

    /** The framebuffer of a finished buffer object: imported once (dma-buf -> GEM handle on the display descriptor -> ADDFB2), then cached by the buffer object. 0 on failure. */
    uint32_t framebufferFor (struct gbm_bo* bo, std::string& error)
    {
        const uint32_t key = api_.gbm_bo_get_handle (bo).u32;
        for (const Import& i : imports_)
            if (i.fb && i.key == key)
                return i.fb;
        int slot = -1;
        for (int i = 0; i < kMaxLedger; ++i)
            if (! imports_[i].fb && ! imports_[i].handle) { slot = i; break; }
        if (slot < 0)
        {
            error = "too many buffer objects";
            return 0;
        }
        const int dfd = api_.gbm_bo_get_fd (bo);
        if (dfd < 0)
        {
            error = "gbm_bo_get_fd failed";
            return 0;
        }
        drm_prime_handle ph;
        std::memset (&ph, 0, sizeof ph);
        ph.fd = dfd;
        const int rc = ioctl (drmFd_, DRM_IOCTL_PRIME_FD_TO_HANDLE, &ph);
        const int perr = errno;
        ::close (dfd);
        if (rc < 0)
        {
            error = std::string ("PRIME_FD_TO_HANDLE: ") + std::strerror (perr);
            return 0;
        }
        imports_[slot].key = key;
        imports_[slot].handle = ph.handle;
        ledgerSet (block_, slot, ph.handle, 0);
        drm_mode_fb_cmd2 f;
        std::memset (&f, 0, sizeof f);
        f.width = api_.gbm_bo_get_width (bo);
        f.height = api_.gbm_bo_get_height (bo);
        f.pixel_format = DRM_FORMAT_ARGB8888;
        f.handles[0] = ph.handle;
        f.pitches[0] = api_.gbm_bo_get_stride (bo);
        if (ioctl (drmFd_, DRM_IOCTL_MODE_ADDFB2, &f) < 0)
        {
            error = std::string ("ADDFB2: ") + std::strerror (errno);
            drm_gem_close gc;
            std::memset (&gc, 0, sizeof gc);
            gc.handle = ph.handle;
            ioctl (drmFd_, DRM_IOCTL_GEM_CLOSE, &gc);
            imports_[slot] = Import();
            ledgerSet (block_, slot, 0, 0);
            return 0;
        }
        imports_[slot].fb = f.fb_id;
        ledgerSet (block_, slot, ph.handle, f.fb_id);
        return f.fb_id;
    }

    void dropImports()
    {
        for (int i = 0; i < kMaxLedger; ++i)
        {
            Import& im = imports_[i];
            if (im.fb)
            {
                uint32_t id = im.fb;
                ioctl (drmFd_, DRM_IOCTL_MODE_RMFB, &id);
            }
            if (im.handle)
            {
                drm_gem_close gc;
                std::memset (&gc, 0, sizeof gc);
                gc.handle = im.handle;
                ioctl (drmFd_, DRM_IOCTL_GEM_CLOSE, &gc);
            }
            if (im.fb || im.handle)
                ledgerSet (block_, i, 0, 0);
            im = Import();
        }
    }

    // ---------------------------------------------------------------------------------------------------------------------------------------------------------
    // the surface of the shown region
    bool createSurface (std::string& error)
    {
        gsurf_ = api_.gbm_surface_create (gbm_, static_cast<uint32_t> (bw_), static_cast<uint32_t> (bh_), GBM_FORMAT_ARGB8888, GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT);
        if (! gsurf_)
        {
            error = "gbm_surface_create failed";
            return false;
        }
        esurf_ = api_.eglCreateWindowSurface (dpy_, cfg_, reinterpret_cast<EGLNativeWindowType> (gsurf_), nullptr);
        if (esurf_ == EGL_NO_SURFACE)
        {
            error = "eglCreateWindowSurface failed";
            return false;
        }
        if (! api_.eglMakeCurrent (dpy_, esurf_, esurf_, ctx_))
        {
            error = "eglMakeCurrent failed";
            return false;
        }
        api_.eglSwapInterval (dpy_, 0);
        api_.glViewport (0, 0, bw_, bh_);
        return true;
    }

    /** Plane off, framebuffers and handles gone, the buffer objects released, the surface destroyed. */
    void teardownSurface()
    {
        if (planeOn_)
            planeOff();
        dropImports();
        if (prevBo_ && gsurf_)
            api_.gbm_surface_release_buffer (gsurf_, prevBo_);
        prevBo_ = nullptr;
        if (dpy_ != EGL_NO_DISPLAY)
            api_.eglMakeCurrent (dpy_, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx_);
        if (esurf_ != EGL_NO_SURFACE)
            api_.eglDestroySurface (dpy_, esurf_);
        esurf_ = EGL_NO_SURFACE;
        if (gsurf_)
            api_.gbm_surface_destroy (gsurf_);
        gsurf_ = nullptr;
        shown_ = false;
    }

    void setTransform()
    {
        const float W = static_cast<float> (w_), H = static_cast<float> (h_);
        if (rot_ == 90)         // buffer pixel (bx, by) = (y, W - x)
        {
            api_.glUniform3f (uRowX_, 0.0f, 2.0f / H, -1.0f);
            api_.glUniform3f (uRowY_, 2.0f / W, 0.0f, -1.0f);
        }
        else                    // buffer pixel (bx, by) = (H - y, x)
        {
            api_.glUniform3f (uRowX_, 0.0f, -2.0f / H, 1.0f);
            api_.glUniform3f (uRowY_, -2.0f / W, 0.0f, 1.0f);
        }
    }

    // ---------------------------------------------------------------------------------------------------------------------------------------------------------
    // the scene -> vertices
    static void premul (uint32_t rgba, float* c)
    {
        const float a = static_cast<float> (rgba & 255u) / 255.0f;
        c[0] = static_cast<float> ((rgba >> 24) & 255u) / 255.0f * a;
        c[1] = static_cast<float> ((rgba >> 16) & 255u) / 255.0f * a;
        c[2] = static_cast<float> ((rgba >> 8) & 255u) / 255.0f * a;
        c[3] = a;
    }
    void add (float x, float y, float d0 = 0.0f, float d1 = 0.0f) { verts_.push_back ({ x, y, d0, d1 }); }
    Draw& begin (int prim, int kind, uint32_t rgba, float half)
    {
        draws_.emplace_back();
        Draw& d = draws_.back();
        d.prim = prim;
        d.kind = kind;
        d.first = static_cast<int> (verts_.size());
        d.half = half;
        premul (rgba, d.colour);
        return d;
    }
    void end() { draws_.back().count = static_cast<int> (verts_.size()) - draws_.back().first; }
    void quad (float x0, float y0, float x1, float y1, float d0a, float d0b, float d1a, float d1b)
    {
        // corners (x0,y0) (x1,y0) (x0,y1) (x1,y1); d0 runs from d0a (left) to d0b (right), d1 from d1a (top) to d1b (bottom)
        add (x0, y0, d0a, d1a); add (x1, y0, d0b, d1a); add (x0, y1, d0a, d1b);
        add (x1, y0, d0b, d1a); add (x1, y1, d0b, d1b); add (x0, y1, d0a, d1b);
    }

    void buildScene (const SceneData& s)
    {
        verts_.clear();
        draws_.clear();
        // the polyline, continued horizontally to the two canvas edges
        pts_.clear();
        const int n = static_cast<int> (s.numPoints);
        if (n >= 2)
        {
            const float W = static_cast<float> (s.w);
            pts_.push_back ({ 0.0f, s.y[0] });
            for (int i = 0; i < n; ++i)
                pts_.push_back ({ static_cast<float> (i) / static_cast<float> (n - 1) * (W - 1.0f) + 0.5f, s.y[i] });
            pts_.push_back ({ W, s.y[n - 1] });
        }
        for (uint32_t k = 0; k < s.numOps && k < static_cast<uint32_t> (kMaxOps); ++k)
        {
            const SceneOp& o = s.ops[k];
            switch (o.op)
            {
                case kOpClear:
                {
                    draws_.emplace_back();
                    Draw& d = draws_.back();
                    d.clear = true;
                    premul (o.rgba, d.colour);
                    break;
                }
                case kOpRect:
                    begin (GL_TRIANGLES, 0, o.rgba, 0.0f);
                    quad (o.f[0], o.f[1], o.f[2], o.f[3], 0, 0, 0, 0);
                    end();
                    break;
                case kOpVLine:
                {
                    const float half = o.f[3] * 0.5f, e = half + 1.0f;
                    begin (GL_TRIANGLES, 1, o.rgba, half);
                    quad (o.f[0] - e, o.f[1], o.f[0] + e, o.f[2], -e, e, 0, 0);
                    end();
                    break;
                }
                case kOpDisc:
                {
                    const float r = o.f[2], e = r + 1.0f;
                    begin (GL_TRIANGLES, 2, o.rgba, r);
                    quad (o.f[0] - e, o.f[1] - e, o.f[0] + e, o.f[1] + e, -e, e, -e, e);
                    end();
                    break;
                }
                case kOpFill:
                    if (pts_.size() >= 2)
                    {
                        begin (GL_TRIANGLES, 0, o.rgba, 0.0f);
                        const float b = o.f[0];
                        for (size_t i = 0; i + 1 < pts_.size(); ++i)
                        {
                            const Pt a = pts_[i], c = pts_[i + 1];
                            const float da = a.y - b, dc = c.y - b;
                            if (da * dc >= 0.0f)
                            {
                                add (a.x, a.y); add (a.x, b); add (c.x, c.y);
                                add (a.x, b); add (c.x, c.y); add (c.x, b);
                            }
                            else
                            {
                                const float t = da / (da - dc), xm = a.x + t * (c.x - a.x);   // the curve crosses the baseline at xm
                                add (a.x, a.y); add (a.x, b); add (xm, b);
                                add (xm, b); add (c.x, c.y); add (c.x, b);
                            }
                        }
                        end();
                    }
                    break;
                case kOpStroke:
                    if (pts_.size() >= 2)
                    {
                        const float half = o.f[0], ext = half + 1.0f;
                        begin (GL_TRIANGLE_STRIP, 1, o.rgba, half);
                        const int m = static_cast<int> (pts_.size());
                        for (int i = 0; i < m; ++i)
                        {
                            float n1x = 0, n1y = 0, n2x = 0, n2y = 0;
                            if (i > 0)
                            {
                                const float dx = pts_[static_cast<size_t> (i)].x - pts_[static_cast<size_t> (i - 1)].x, dy = pts_[static_cast<size_t> (i)].y - pts_[static_cast<size_t> (i - 1)].y;
                                const float l = std::sqrt (dx * dx + dy * dy);
                                if (l > 1e-6f) { n1x = -dy / l; n1y = dx / l; }
                            }
                            if (i < m - 1)
                            {
                                const float dx = pts_[static_cast<size_t> (i + 1)].x - pts_[static_cast<size_t> (i)].x, dy = pts_[static_cast<size_t> (i + 1)].y - pts_[static_cast<size_t> (i)].y;
                                const float l = std::sqrt (dx * dx + dy * dy);
                                if (l > 1e-6f) { n2x = -dy / l; n2y = dx / l; }
                            }
                            if (i == 0) { n1x = n2x; n1y = n2y; }
                            if (i == m - 1) { n2x = n1x; n2y = n1y; }
                            float mx = n1x + n2x, my = n1y + n2y, ml = std::sqrt (mx * mx + my * my);
                            if (ml < 1e-4f) { mx = n1x; my = n1y; ml = 1.0f; }
                            mx /= ml;
                            my /= ml;
                            float cosHalf = mx * n2x + my * n2y;
                            if (cosHalf < 0.4f)
                                cosHalf = 0.4f;     // miter limit 2.5
                            const float k2 = ext / cosHalf;
                            add (pts_[static_cast<size_t> (i)].x + mx * k2, pts_[static_cast<size_t> (i)].y + my * k2, ext, 0.0f);
                            add (pts_[static_cast<size_t> (i)].x - mx * k2, pts_[static_cast<size_t> (i)].y - my * k2, -ext, 0.0f);
                        }
                        end();
                    }
                    break;
                default:
                    break;
            }
        }
    }

    // ---------------------------------------------------------------------------------------------------------------------------------------------------------
    // one picture
    bool renderAndPresent()
    {
        const double t0 = nowMs();
        SceneData& s = scene_;
        uint32_t seq = 0;
        if (! readScene (block_, s, seq))
            return false;    // the writer was busy for a whole round of retries: the next kick
        if (s.w != w_ || s.h != h_)
            return false;    // a scene of the region before this one: the right one is on its way
        buildScene (s);
        api_.glBindBuffer (GL_ARRAY_BUFFER, vbo_);
        api_.glBufferData (GL_ARRAY_BUFFER, static_cast<GLsizeiptr> (verts_.size() * sizeof (Vtx)), verts_.data(), GL_DYNAMIC_DRAW);
        setTransform();
        for (const Draw& d : draws_)
        {
            if (d.clear)
            {
                api_.glClearColor (d.colour[0], d.colour[1], d.colour[2], d.colour[3]);
                api_.glClear (GL_COLOR_BUFFER_BIT);
                continue;
            }
            api_.glUniform1f (uKind_, static_cast<float> (d.kind));
            api_.glUniform4f (uC0_, d.colour[0], d.colour[1], d.colour[2], d.colour[3]);
            api_.glUniform1f (uHalf_, d.half);
            api_.glDrawArrays (static_cast<GLenum> (d.prim), d.first, d.count);
        }
        api_.glFinish();    // the buffer must be finished before the display reads it
        maybeDump (s, seq);
        if (! api_.eglSwapBuffers (dpy_, esurf_))
        {
            sendError ("eglSwapBuffers failed");
            return false;
        }
        struct gbm_bo* bo = api_.gbm_surface_lock_front_buffer (gsurf_);
        if (! bo)
        {
            sendError ("gbm_surface_lock_front_buffer failed");
            return false;
        }
        std::string error;
        const uint32_t fb = framebufferFor (bo, error);
        if (! fb)
        {
            sendError (error);
            api_.gbm_surface_release_buffer (gsurf_, bo);
            return false;
        }
        const double t1 = nowMs();
        if (! planeSet (fb))
        {
            sendError (std::string ("SETPLANE: ") + std::strerror (errno));
            api_.gbm_surface_release_buffer (gsurf_, bo);
            return false;
        }
        const double t2 = nowMs();
        if (prevBo_)
            api_.gbm_surface_release_buffer (gsurf_, prevBo_);   // the previous picture is off the screen now (SETPLANE waited for the vblank)
        prevBo_ = bo;
        ++frames_;
        // statistics: SETPLANE longer than 20 ms is late; frames in a row (< 25 ms apart) are one vblank apart
        if (t2 - t1 > 20.0)
            ++late_;
        drm_wait_vblank vb;
        std::memset (&vb, 0, sizeof vb);
        vb.request.type = _DRM_VBLANK_RELATIVE;
        if (ioctl (drmFd_, DRM_IOCTL_WAIT_VBLANK, &vb) == 0)
        {
            const uint32_t vseq = vb.reply.sequence;
            if (haveVblank_ && t2 - lastFrameMs_ < 25.0 && vseq - lastVblank_ > 1)
                skipped_ += vseq - lastVblank_ - 1;
            lastVblank_ = vseq;
            haveVblank_ = true;
        }
        lastFrameMs_ = t2;
        send (makeMsg (kMsgFrame, static_cast<uint32_t> (frames_), seq, static_cast<uint32_t> ((t1 - t0) * 1000.0), static_cast<uint32_t> ((t2 - t1) * 1000.0), static_cast<uint32_t> (skipped_),
                       static_cast<uint32_t> (late_)));
        return true;
    }

    /** Debug (flag from the hello): while /tmp/synthyboy-overlay-gpu-dump exists, the finished frame is read back (glReadPixels, rows as GL returns them: bottom up) and written to
        /tmp/synthyboy-overlay-gpu-frame.raw (RGBA, premultiplied, bw x bh) with a text file of its numbers, once a second at most. */
    void maybeDump (const SceneData& s, uint32_t seq)
    {
        if (! (flags_ & kFlagDump))
            return;
        const double t = nowMs();
        if (t - lastDumpCheckMs_ < 1000.0)
            return;
        lastDumpCheckMs_ = t;
        if (::access ("/tmp/synthyboy-overlay-gpu-dump", F_OK) != 0)
            return;
        std::vector<uint8_t> px (static_cast<size_t> (bw_) * static_cast<size_t> (bh_) * 4);
        api_.glReadPixels (0, 0, bw_, bh_, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        if (FILE* f = std::fopen ("/tmp/synthyboy-overlay-gpu-frame.raw.tmp", "wb"))
        {
            std::fwrite (px.data(), 1, px.size(), f);
            std::fclose (f);
            std::rename ("/tmp/synthyboy-overlay-gpu-frame.raw.tmp", "/tmp/synthyboy-overlay-gpu-frame.raw");
        }
        if (FILE* f = std::fopen ("/tmp/synthyboy-overlay-gpu-frame.txt.tmp", "w"))
        {
            std::fprintf (f, "view %s\nkind %u\ncanvas %d %d\nbuffer %d %d\nrotation %d\nseq %u\nrows bottom-up (glReadPixels), RGBA premultiplied\n", viewName_, s.kind, s.w, s.h, bw_, bh_, rot_, seq);
            std::fclose (f);
            std::rename ("/tmp/synthyboy-overlay-gpu-frame.txt.tmp", "/tmp/synthyboy-overlay-gpu-frame.txt");
        }
    }

    // ---------------------------------------------------------------------------------------------------------------------------------------------------------
    // the loop
    void onShow (const Msg& m)
    {
        const int w = static_cast<int> (m.a), h = static_cast<int> (m.b), px = static_cast<int> (m.c), py = static_cast<int> (m.d), rot = static_cast<int> (m.e);
        std::snprintf (viewName_, sizeof viewName_, "%.31s", m.text);
        const bool same = shown_ && w == w_ && h == h_ && px == panelX_ && py == panelY_ && rot == rot_;
        if (same)
            return;
        if (shown_)
            teardownSurface();
        w_ = w;
        h_ = h;
        panelX_ = px;
        panelY_ = py;
        rot_ = rot == 270 ? 270 : 90;
        bw_ = h;    // the panel is portrait: the buffer is the canvas turned
        bh_ = w;
        if (w < 2 || h < 2 || w > 2048 || h > 2048)
        {
            sendError ("region out of range");
            return;
        }
        std::string error;
        if (! createSurface (error))
        {
            sendError (error);
            return;
        }
        shown_ = true;
        dirty_ = false;
        send (makeMsg (kMsgShown, static_cast<uint32_t> (frames_)));
    }

    void onHide()
    {
        if (shown_)
            teardownSurface();
        haveVblank_ = false;
        dirty_ = false;
        send (makeMsg (kMsgHidden, static_cast<uint32_t> (frames_)));
    }

    /** Reads every pending message. false: the helper should end (quit, closed socket). */
    bool drain()
    {
        for (;;)
        {
            Msg m;
            const ssize_t n = ::recv (kSock, &m, sizeof m, MSG_DONTWAIT);
            if (n == 0)
                return false;   // the plugin (or the application) went away
            if (n < 0)
                return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
            if (n != static_cast<ssize_t> (sizeof m) || m.magic != kMsgMagic)
            {
                sendError ("bad message from the plugin");
                return false;
            }
            switch (m.type)
            {
                case kMsgShow: onShow (m); break;
                case kMsgScene: dirty_ = true; break;
                case kMsgHide: onHide(); break;
                case kMsgQuit: return false;
                default: break;
            }
            if (exitCode_ != 0)
                return false;
        }
    }

    void loop()
    {
        while (! g_stop && exitCode_ == 0)
        {
            pollfd p { kSock, POLLIN, 0 };
            const int rc = ::poll (&p, 1, dirty_ ? 5 : 1000);   // a scene that could not be read is tried again in 5 ms
            if (rc < 0 && errno != EINTR)
                break;
            if (g_stop)
                break;
            if (rc > 0 && (p.revents & (POLLIN | POLLHUP | POLLERR)))
                if (! drain())
                    break;
            if (dirty_ && shown_)
            {
                if (renderAndPresent())
                    dirty_ = false;
                else if (exitCode_ != 0)
                    break;
            }
        }
    }

    void cleanupAll()
    {
        if (shown_ || planeOn_ || esurf_ != EGL_NO_SURFACE || gsurf_)
            teardownSurface();
        if (dpy_ != EGL_NO_DISPLAY)
        {
            api_.eglMakeCurrent (dpy_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (ctx_ != EGL_NO_CONTEXT)
                api_.eglDestroyContext (dpy_, ctx_);
            api_.eglTerminate (dpy_);
            dpy_ = EGL_NO_DISPLAY;
        }
        if (gbm_)
            api_.gbm_device_destroy (gbm_);
        gbm_ = nullptr;
        if (gbmFd_ >= 0)
            ::close (gbmFd_);
        gbmFd_ = -1;
        if (drmFd_ >= 0)
            ::close (drmFd_);   // our copy only; the application keeps the file description
        drmFd_ = -1;
    }

    struct Pt { float x, y; };

    sbgpu::Api api_;
    SharedBlock* block_ = nullptr;
    int drmFd_ = -1, gbmFd_ = -1;
    uint32_t crtc_ = 0, plane_ = 0, flags_ = 0;
    struct gbm_device* gbm_ = nullptr;
    struct gbm_surface* gsurf_ = nullptr;
    struct gbm_bo* prevBo_ = nullptr;
    EGLDisplay dpy_ = EGL_NO_DISPLAY;
    EGLConfig cfg_ = nullptr;
    EGLContext ctx_ = EGL_NO_CONTEXT;
    EGLSurface esurf_ = EGL_NO_SURFACE;
    GLuint prog_ = 0, vbo_ = 0;
    GLint uRowX_ = -1, uRowY_ = -1, uKind_ = -1, uC0_ = -1, uHalf_ = -1;
    Import imports_[kMaxLedger];
    std::vector<Vtx> verts_;
    std::vector<Draw> draws_;
    std::vector<Pt> pts_;
    SceneData scene_ {};
    bool shown_ = false, dirty_ = false, planeOn_ = false, haveVblank_ = false;
    int w_ = 0, h_ = 0, bw_ = 0, bh_ = 0, panelX_ = 0, panelY_ = 0, rot_ = 90, exitCode_ = 0;
    char viewName_[32] = {};
    unsigned long frames_ = 0, late_ = 0, skipped_ = 0;
    uint32_t lastVblank_ = 0;
    double startMs_ = 0.0, lastFrameMs_ = 0.0, lastDumpCheckMs_ = 0.0;
};

} // namespace

int main()
{
    struct sigaction sa;
    std::memset (&sa, 0, sizeof sa);
    sa.sa_handler = onSignal;   // no SA_RESTART: a signal interrupts poll
    sigaction (SIGTERM, &sa, nullptr);
    sigaction (SIGINT, &sa, nullptr);
    sigaction (SIGHUP, &sa, nullptr);
    signal (SIGPIPE, SIG_IGN);
    setvbuf (stderr, nullptr, _IOLBF, 0);
    Helper h;
    return h.run();
}
