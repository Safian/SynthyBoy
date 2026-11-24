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
//   pacing     the update of the plane (SETPLANE) is made the moment the vblank of a slot of the pace grid is reached (vblank_pacer.h: every `stride` vblanks, measured and chosen by the
//              plugin, sent in the hello): that is what makes the intervals even on a device whose plane update alternates between one and two vblanks when it is called at any time
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
#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <thread>
#include <string>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <linux/futex.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <drm/drm.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_mode.h>

#include "../gpu_clock.h"
#include "../sdf_font.h"
#include "../gpu/scene.h"
#include "../gpu/stroke_strip.h"
#include "../gpu/cell_cache.h"
#include "../gpu/cell_plan.h"
#include "../gpu/layer_block.h"
#include "../gpu/guard_state.h"
#include "../gpu/guard_trace.h"
#include "../overlay_layout.h"
#include "../vblank_linux.h"
#include "../vblank_pacer.h"
#include "gl_api.h"
#include "../../write_files.h"

using namespace synthyboy_mpc::overlay::gpu;
namespace gpu = synthyboy_mpc::overlay::gpu;
namespace layout = synthyboy_mpc::overlay::layout;
using synthyboy_mpc::overlay::FramePacer;
using synthyboy_mpc::overlay::LinuxVblankClock;
using synthyboy_mpc::overlay::PaceStats;

namespace {

/** Sleeps on the guard flag word of the LayerBlock (futex; the shim wakes it, layer_block.h) and, when the flag is set, writes the eventfd the helper's loop polls: the flag is then read at once, not when a message or a
    timeout happens to wake the loop (g133: 24.9 ms median, 100 ms p90 after the host's flip). At rest the thread sleeps (a timeout of kIdleMs only so that it can notice a stop): the wake-ups at rest are
    counted (`idleWakes`) and are 1 in kIdleMs, none of them wakes the main loop. */
class GuardWatcher
{
public:
    static constexpr int kIdleMs = 10000, kSetMs = 20;
    bool start (LayerBlock* b)
    {
        if (running_ || b == nullptr)
            return running_;
        fd_ = ::eventfd (0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (fd_ < 0)
            return false;
        flag_ = reinterpret_cast<uint32_t*> (&b->guard_plane_off);
        stop_.store (false);
        running_ = true;
        th_ = std::thread ([this] { body(); });
        return true;
    }
    void stop()
    {
        if (! running_)
            return;
        stop_.store (true);
        ::syscall (SYS_futex, flag_, FUTEX_WAKE, 0x7fffffff, nullptr, nullptr, 0);
        th_.join();
        running_ = false;
        ::close (fd_);
        fd_ = -1;
    }
    int fd() const noexcept { return running_ ? fd_ : -1; }
    void drain() noexcept { uint64_t v; (void) ::read (fd_, &v, sizeof v); }
    ~GuardWatcher() { stop(); }
    std::atomic<unsigned long> signals { 0 }, idleWakes { 0 };

private:
    void body()
    {
        while (! stop_.load())
        {
            const uint32_t v = __atomic_load_n (flag_, __ATOMIC_ACQUIRE);
            timespec ts {};
            if (v != 0)
            {
                const uint64_t one = 1;
                if (::write (fd_, &one, sizeof one) > 0)
                    signals.fetch_add (1);
                ts.tv_nsec = kSetMs * 1000000L;    // set: look again soon (the loop clears it; nobody wakes us for that)
            }
            else
                ts.tv_sec = kIdleMs / 1000;
            ::syscall (SYS_futex, flag_, FUTEX_WAIT, v, &ts, nullptr, 0);
            if (v == 0 && __atomic_load_n (flag_, __ATOMIC_ACQUIRE) == 0)
                idleWakes.fetch_add (1);
        }
    }
    uint32_t* flag_ = nullptr;
    int fd_ = -1;
    bool running_ = false;
    std::atomic<bool> stop_ { false };
    std::thread th_;
};

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
struct PaintU { float mode = 0.0f, a = 0.0f, inv = 1.0f; float c[4][4] = {}; };   // the paint: premultiplied colours, mode 0 none / 1 x / 2 y / 3 arc angle
/** One GL draw (or a state change of the frame). kind: 0 solid, 1 feathered line (d0 = signed distance from the axis, d1 = distance along it), 2 disc (d0, d1 = offset from the centre), 3 vertical
    gradient (d0 = 0 .. 1 from colour to colour2), 4 / 5 / 6 ring segment of at most pi / more than pi / the whole ring (d0, d1 = offset from the centre), 7 text (d0, d1 = atlas coordinates),
    8 rounded rectangle (d0, d1 = offset from the centre), 9 band (d0 = coverage), 10 a texture times colour, 12 / 13 the blur of the glow layer (horizontal / vertical). */
struct Draw
{
    int prog = 0;                                   // the program (programFor)
    int clipBox[4] = {};                            // type 4: the scissor box (GL pixels; width 0 = clip off)
    int type = 0;                                   // 0 draw, 1 clear (colour), 2 glow begin (the layer is cleared), 3 nothing (a marker of the end of the glow)
    int prim = 0, first = 0, count = 0, kind = 0;
    int target = -1;                                // -1 the picture, 0 / 1 the two glow layers, 2 / 3 the two cached layers
    int tex = 0;                                    // 0 the font atlas, 1 / 2 the glow layers, 3 the previous picture, 4 / 5 the cached layers
    bool additive = false;                          // blend ONE, ONE (the bloom is added)
    float colour[4] = {};                           // premultiplied
    float colour2[4] = {};                          // premultiplied, the other end of a gradient
    float half = 0.0f;                              // half width of a line, radius of a disc, half width of an arc
    float soft = 1.0f;                              // the feather (px)
    float arcR = 0.0f, arcU[4] = {}, arcE[4] = {};  // an arc: the radius, the unit directions of its two ends (u0x, u0y, u1x, u1y) and the centres of its round caps (offsets from the centre)
    float arcA0 = 0.0f, arcInv = 0.0f;              // the angle of the first end and 1 / sweep
    float round = 0.0f;                             // round caps of an arc / a line
    float len = 0.0f;                               // length of a line
    float textK = 0.0f;                             // 2 * kSpread * size / kEm
    float rr[4] = {};                               // rounded rectangle: hx - r, hy - r, r, outline width
    float texel[2] = {};
    PaintU pt;
    int divisor = 4;
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
        std::fprintf (stderr, "helper %d: ready in %.1f ms, renderer %s, render node %s, drm fd %d, crtc %u plane %u, pace stride %d vblanks (period %.3f ms, %s)\n", static_cast<int> (getpid()), nowMs() - startMs_, text, renderNode_.c_str(), drmFd_, crtc_, plane_, stride_, periodMs_, (flags_ & kFlagVblank) ? "vblank grid" : "timer");
        loop();
        std::fprintf (stderr, "helper %d: leaving (%s), %lu pictures, %lu late SETPLANE calls, %lu skipped vblanks\n",
                      static_cast<int> (getpid()), g_stop ? "signal" : exitCode_ ? "error" : endReason_, frames_, late_, skipped_);
        cleanupAll();
        std::fprintf (stderr, "helper %d: plane off, framebuffers and handles removed\n", static_cast<int> (getpid()));
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
        pipe_ = static_cast<int> (m.e & 0xffu);
        stride_ = static_cast<int> ((m.e >> 8) & 0xffu);
        if (stride_ < 1)
            stride_ = 2;
        periodMs_ = m.f > 0 ? m.f / 1000.0 : 16.67;
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
        clock_ = std::make_unique<LinuxVblankClock> (drmFd_, pipe_);
        pacer_ = std::make_unique<FramePacer> (*clock_, periodMs_, stride_, (flags_ & kFlagVblank) != 0);
        __atomic_store_n (&block_->pace.stride, static_cast<uint32_t> (stride_), __ATOMIC_RELAXED);
        __atomic_store_n (&block_->pace.periodUs, static_cast<uint32_t> (periodMs_ * 1000.0 + 0.5), __ATOMIC_RELAXED);
        __atomic_store_n (&block_->pace.onGrid, (flags_ & kFlagVblank) ? 1u : 0u, __ATOMIC_RELAXED);
        return true;
    }

    // ---------------------------------------------------------------------------------------------------------------------------------------------------------
    // GL
    static constexpr const char* kVs =
        "attribute vec2 aPos; attribute vec2 aD; varying vec2 vD; varying vec2 vP; varying vec2 vC; uniform vec3 uRowX; uniform vec3 uRowY;\n"
        "void main() { vD = aD; vP = aPos; gl_Position = vec4 (dot (uRowX.xy, aPos) + uRowX.z, dot (uRowY.xy, aPos) + uRowY.z, 0.0, 1.0); vC = gl_Position.xy * 0.5 + 0.5; }\n";
    // premultiplied output. kinds: see struct Draw. The coverage functions are the ones of scene.h (edgeCoverage, lineCoverage, rrectCoverage, arcContains, arcParam) and sdf_font.h (coverage).
    // FIVE programs from one source (a fragment shader with every kind in it is slow on the Mali-T760 at its lowest clock: the cost of a fragment grows with the code the compiler keeps):
    // P_SOLID (a plain colour: rectangles, frames), P_BASIC (ink, line, disc, gradient, band), P_SHAPE (arc, rounded rectangle), P_TEXT (distance field text), P_TEX (feedback, glow passes).
    static constexpr int kPrograms = 5;
    static constexpr const char* kFsDefines[kPrograms] = { "#define P_SOLID 1\n", "#define P_BASIC 1\n", "#define P_SHAPE 1\n", "#define P_TEXT 1\n", "#define P_TEX 1\n" };
    static constexpr const char* kFs =
        "precision highp float;\n"
        "varying vec2 vD; varying vec2 vP; varying vec2 vC;\n"
        "uniform float uKind; uniform vec4 uC0; uniform vec4 uC1; uniform float uHalf; uniform float uR; uniform vec4 uU; uniform float uSoft;\n"
        "uniform vec4 uE; uniform vec4 uA; uniform float uLen; uniform float uTextK; uniform vec4 uRR; uniform vec2 uTexel;\n"
        "uniform vec3 uPm; uniform vec4 uP0; uniform vec4 uP1; uniform vec4 uP2; uniform vec4 uP3; uniform sampler2D uTex;\n"
        "#if defined(P_BASIC) || defined(P_SHAPE) || defined(P_TEXT)\n"
        "float edge (float x) { float c = clamp (0.5 + x / uSoft, 0.0, 1.0); return uSoft > 1.0 ? c * c * (3.0 - 2.0 * c) : c; }\n"
        "float lineCov (float d) { float c = edge (uHalf - d); return uSoft > 1.0 ? c : min (c, 2.0 * uHalf); }\n"
        "vec4 pal (float t) { t = clamp (t, 0.0, 1.0) * 3.0; if (t < 1.0) return mix (uP0, uP1, t); if (t < 2.0) return mix (uP1, uP2, t - 1.0); return mix (uP2, uP3, t - 2.0); }\n"
        "vec4 ink (float t) {\n"
        "  if (uPm.x < 0.5) return uC0;\n"
        "  if (uPm.x < 1.5) t = (vP.x - uPm.y) * uPm.z; else if (uPm.x < 2.5) t = (vP.y - uPm.y) * uPm.z;\n"
        "  return pal (t) * uC0.a;\n"
        "}\n"
        "#endif\n"
        "void main() {\n"
        "#if defined(P_SOLID)\n"
        "  gl_FragColor = uC0;\n"
        "#elif defined(P_BASIC)\n"
        "  if (uKind < 0.5) { gl_FragColor = ink (0.0); return; }\n"
        "  if (uKind < 1.5) {\n"
        "    float d = abs (vD.x);\n"
        "    if (uA.z > 0.5) d = length (vec2 (vD.x, max (max (-vD.y, vD.y - uLen), 0.0)));\n"
        "    gl_FragColor = ink (0.0) * lineCov (d); return;\n"
        "  }\n"
        "  if (uKind < 2.5) { gl_FragColor = ink (0.0) * edge (uHalf - length (vD)); return; }\n"
        "  if (uKind < 3.5) { gl_FragColor = mix (uC0, uC1, clamp (vD.x, 0.0, 1.0)); return; }\n"
        "  gl_FragColor = ink (0.0) * clamp (vD.x, 0.0, 1.0);\n"                 // the band: d0 is the coverage
        "#elif defined(P_SHAPE)\n"
        "  if (uKind < 6.5) {\n"   // the ring segment: the ring by the distance from the centre, the ends by cross products of the offset with the directions of the two ends (arcContains of scene.h)
        "    float cov = lineCov (abs (length (vD) - uR));\n"
        "    bool inside = true;\n"
        "    if (uKind < 4.5) inside = uU.x * vD.y - uU.y * vD.x >= 0.0 && vD.x * uU.w - vD.y * uU.z >= 0.0;\n"
        "    else if (uKind < 5.5) inside = ! (uU.z * vD.y - uU.w * vD.x > 0.0 && vD.x * uU.y - vD.y * uU.x > 0.0);\n"
        "    cov = inside ? cov : 0.0;\n"
        "    if (uA.z > 0.5 && uKind < 5.5) { cov = max (cov, lineCov (length (vD - uE.xy))); cov = max (cov, lineCov (length (vD - uE.zw))); }\n"
        "    float t = 0.0;\n"
        "    if (uPm.x > 2.5) { float a = atan (vD.y, vD.x) - uA.x; a -= 6.28318530718 * floor (a / 6.28318530718); t = a * uA.y; }\n"
        "    gl_FragColor = ink (t) * cov; return;\n"
        "  }\n"
        "  vec2 q = abs (vD) - uRR.xy;\n"
        "  float d = length (max (q, 0.0)) + min (max (q.x, q.y), 0.0) - uRR.z;\n"
        "  float cov = edge (-d);\n"
        "  if (uRR.w > 0.0) cov = max (cov - edge (-d - uRR.w), 0.0);\n"
        "  gl_FragColor = ink (0.0) * cov;\n"
        "#elif defined(P_TEXT)\n"
        "  float v = texture2D (uTex, vD).a;\n"
        "  gl_FragColor = ink (0.0) * clamp (0.5 + (v - 0.5) * uTextK, 0.0, 1.0);\n"
        "#else\n"
        "  if (uKind < 10.5) { gl_FragColor = texture2D (uTex, vC) * uC0; return; }\n"
        "  vec2 st = uKind < 12.5 ? vec2 (uTexel.x, 0.0) : vec2 (0.0, uTexel.y);\n"   // the blur: 9 taps from 5 linear samples
        "  vec4 s = texture2D (uTex, vC) * 0.2270270270;\n"
        "  s += (texture2D (uTex, vC + st * 1.3846153846) + texture2D (uTex, vC - st * 1.3846153846)) * 0.3162162162;\n"
        "  s += (texture2D (uTex, vC + st * 3.2307692308) + texture2D (uTex, vC - st * 3.2307692308)) * 0.0702702703;\n"
        "  gl_FragColor = s;\n"
        "#endif\n"
        "}\n";

    static int programFor (int kind, bool painted) noexcept
    {
        if (kind == 0)
            return painted ? 1 : 0;
        if (kind == 1 || kind == 2 || kind == 3 || kind == 9)
            return 1;
        if ((kind >= 4 && kind <= 6) || kind == 8)
            return 2;
        return kind == 7 ? 3 : 4;
    }

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

    // ---------------------------------------------------------------------------------------------------------------------------------------------------------
    // the render node: not "renderD128" by name but every /dev/dri/renderD* in turn (the GPU drivers first), the first one that gives a GBM device, an EGL display and an ES2 ARGB config
    struct Node { std::string path; int rank; };

    static int driverRank (const std::string& path)
    {
        const int fd = ::open (path.c_str(), O_RDWR | O_CLOEXEC);
        if (fd < 0)
            return 99;
        char name[64] = {};
        drm_version v;
        std::memset (&v, 0, sizeof v);
        v.name = name;
        v.name_len = sizeof name - 1;
        const int rc = ioctl (fd, DRM_IOCTL_VERSION, &v);
        ::close (fd);
        if (rc < 0)
            return 50;
        const std::string n (name, v.name_len < sizeof name ? v.name_len : sizeof name - 1);
        for (const char* gpu : { "panthor", "panfrost", "lima", "etnaviv", "v3d", "vc4" })
            if (n.compare (0, std::strlen (gpu), gpu) == 0)
                return 0;
        return n.compare (0, 6, "vgem") == 0 ? 90 : 10;
    }

    bool tryNode (const std::string& path, std::string& error)
    {
        gbmFd_ = ::open (path.c_str(), O_RDWR | O_CLOEXEC);
        if (gbmFd_ < 0)
        {
            error = "open " + path + ": " + std::strerror (errno);
            return false;
        }
        gbm_ = api_.gbm_create_device (gbmFd_);
        if (! gbm_)
        {
            error = path + ": gbm_create_device failed";
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
            error = path + ": eglInitialize failed";
            dpy_ = EGL_NO_DISPLAY;
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
            if ((rt & EGL_OPENGL_ES2_BIT) && (st & EGL_WINDOW_BIT) && r == 8 && g == 8 && b == 8 && a == 8 && s == wantSamples_ && static_cast<uint32_t> (vis) == GBM_FORMAT_ARGB8888)
                cfg_ = all[static_cast<size_t> (i)];
        }
        if (! cfg_)
        {
            error = path + ": no EGL config (ES2, window, ARGB8888)";
            return false;
        }
        return true;
    }

    void dropNode()
    {
        if (dpy_ != EGL_NO_DISPLAY)
            api_.eglTerminate (dpy_);
        dpy_ = EGL_NO_DISPLAY;
        if (gbm_)
            api_.gbm_device_destroy (gbm_);
        gbm_ = nullptr;
        if (gbmFd_ >= 0)
            ::close (gbmFd_);
        gbmFd_ = -1;
    }

    bool openRenderNode (std::string& error)
    {
        std::vector<Node> nodes;
        if (const char* env = std::getenv ("SYNTHYBOY_OVERLAY_RENDER_NODE"))
            if (*env)
                nodes.push_back ({ env, -1 });
        if (DIR* d = opendir ("/dev/dri"))
        {
            while (dirent* e = readdir (d))
                if (std::strncmp (e->d_name, "renderD", 7) == 0)
                {
                    const std::string path = std::string ("/dev/dri/") + e->d_name;
                    nodes.push_back ({ path, driverRank (path) });
                }
            closedir (d);
        }
        std::sort (nodes.begin(), nodes.end(), [] (const Node& a, const Node& b) { return a.rank != b.rank ? a.rank < b.rank : a.path < b.path; });
        if (nodes.empty())
        {
            error = "no render node in /dev/dri";
            return false;
        }
        std::string all;
        for (const Node& n : nodes)
        {
            std::string why;
            if (tryNode (n.path, why))
            {
                renderNode_ = n.path;
                return true;
            }
            all += (all.empty() ? "" : "; ") + why;
            dropNode();
        }
        error = "no usable render node: " + all;
        return false;
    }

    bool initGl (std::string& error)
    {
        if (const char* ms = std::getenv ("SYNTHYBOY_OVERLAY_MSAA"))   // a measurement switch: an EGL configuration with this many samples (0 = none, the default)
            wantSamples_ = std::atoi (ms);
#ifdef SYNTHYBOY_OVERLAY_BENCH
        if (FILE* f = std::fopen ("/tmp/sb-bench-msaa", "r"))   // the measuring build: the file names the number of samples (the helper is restarted to change it)
        {
            int n = 0;
            if (std::fscanf (f, "%d", &n) == 1)
                wantSamples_ = n;
            std::fclose (f);
        }
#endif
        if (! api_.load (error))
            return false;
        if (! openRenderNode (error))
            return false;
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
        GLuint vs = compile (GL_VERTEX_SHADER, kVs, error);
        if (! vs)
            return false;
        for (int pi = 0; pi < kPrograms; ++pi)
        {
            const std::string src = std::string (kFsDefines[pi]) + kFs;
            GLuint fs = compile (GL_FRAGMENT_SHADER, src.c_str(), error);
            if (! fs)
                return false;
            prog_[pi] = api_.glCreateProgram();
            api_.glAttachShader (prog_[pi], vs);
            api_.glAttachShader (prog_[pi], fs);
            api_.glBindAttribLocation (prog_[pi], 0, "aPos");
            api_.glBindAttribLocation (prog_[pi], 1, "aD");
            api_.glLinkProgram (prog_[pi]);
            GLint ok = 0;
            api_.glGetProgramiv (prog_[pi], GL_LINK_STATUS, &ok);
            if (! ok)
            {
                char log[512] = {};
                api_.glGetProgramInfoLog (prog_[pi], sizeof log, nullptr, log);
                error = std::string ("link: ") + log;
                return false;
            }
            api_.glUseProgram (prog_[pi]);
            auto loc = [&] (const char* name) { return api_.glGetUniformLocation (prog_[pi], name); };
            Locs& u = locs_[pi];
            u.rowX = loc ("uRowX"); u.rowY = loc ("uRowY"); u.kind = loc ("uKind"); u.c0 = loc ("uC0"); u.c1 = loc ("uC1"); u.half = loc ("uHalf"); u.r = loc ("uR"); u.u = loc ("uU");
            u.soft = loc ("uSoft"); u.e = loc ("uE"); u.a = loc ("uA"); u.len = loc ("uLen"); u.textK = loc ("uTextK"); u.rr = loc ("uRR"); u.texel = loc ("uTexel"); u.pm = loc ("uPm");
            u.p0 = loc ("uP0"); u.p1 = loc ("uP1"); u.p2 = loc ("uP2"); u.p3 = loc ("uP3"); u.tex = loc ("uTex");
            api_.glUniform1i (u.tex, 0);
        }
        u_ = locs_[0];
        curProg_ = 0;
        // the font atlas (alpha, bilinear): the signed distance field of sdf_font_data.h
        api_.glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
        api_.glGenTextures (1, &atlasTex_);
        api_.glBindTexture (GL_TEXTURE_2D, atlasTex_);
        api_.glTexImage2D (GL_TEXTURE_2D, 0, GL_ALPHA, synthyboy_mpc::overlay::sdf::kAtlasW, synthyboy_mpc::overlay::sdf::kAtlasH, 0, GL_ALPHA, GL_UNSIGNED_BYTE, synthyboy_mpc::overlay::sdf::kAtlas);
        setTexParams();
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
        curFb_ = 0;
        if (drmFd_ < 0)
            return;
        drm_mode_set_plane s;
        std::memset (&s, 0, sizeof s);
        s.plane_id = plane_;
        ioctl (drmFd_, DRM_IOCTL_MODE_SETPLANE, &s);
        planeOn_ = false;
    }
    void ensureLayerBlock()
    {
        if (layerBlock_ != nullptr)
            return;
        const double t = nowMs();
        if (t - lastLayerBlockProbeMs_ < 1000.0)
            return;
        lastLayerBlockProbeMs_ = t;
        layerBlock_ = layer_block_map (layer_block_path(), false);
        if (layerBlock_ != nullptr)
            guardWatcher_.start (layerBlock_);   // the flag is waited for, not polled (wakes the loop through its eventfd)
    }
    LayerFields makePresentFields (uint32_t fb) const noexcept
    {
        LayerFields f {};
        f.helper_pid = static_cast<int32_t> (getpid());
        f.plane_id = plane_;
        f.crtc_id = crtc_;
        f.fb_id = fb;
        f.crtc_x = panelX_;
        f.crtc_y = panelY_;
        f.crtc_w = static_cast<uint32_t> (bw_);
        f.crtc_h = static_cast<uint32_t> (bh_);
        f.src_x = 0u;
        f.src_y = 0u;
        f.src_w = static_cast<uint32_t> (bw_) << 16;
        f.src_h = static_cast<uint32_t> (bh_) << 16;
        f.enabled = 1u;
        f.page_id = viewIndex_;
        if (viewIndex_ >= 0 && viewIndex_ < layout::kNumViews)
        {
            f.expected_marker_l = layout::kViews[viewIndex_].markers[0].rgb;
            f.expected_marker_r = layout::kViews[viewIndex_].markers[1].rgb;
        }
        f.rotation = static_cast<uint32_t> (rot_);
        return f;
    }
    LayerFields makeHideFields() const noexcept
    {
        LayerFields f {};
        f.helper_pid = static_cast<int32_t> (getpid());
        f.plane_id = plane_;
        f.crtc_id = 0u;
        f.fb_id = 0u;
        f.enabled = 0u;
        f.page_id = -1;
        f.expected_marker_l = 0u;
        f.expected_marker_r = 0u;
        f.rotation = static_cast<uint32_t> (rot_);
        return f;
    }
    void releaseSurfaceBo (void* p)
    {
        if (p && gsurf_)
            api_.gbm_surface_release_buffer (gsurf_, static_cast<struct gbm_bo*> (p));
    }
    void hidePlane()
    {
        curFb_ = 0;
        planeOff();
        if (prevBo_)
        {
            api_.gbm_surface_release_buffer (gsurf_, prevBo_);
            prevBo_ = nullptr;
        }
        ensureLayerBlock();
        if (layerBlock_)
        {
            const LayerFields lf = makeHideFields();
            layer_block_publish (layerBlock_, &lf);
        }
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
        feedbackTex_ = makeTexture (bw_, bh_);      // the previous picture, for the Feedback op (copied after every picture that needs it)
        api_.glBindFramebuffer (GL_FRAMEBUFFER, 0);
        prevFeedback_ = false;
        return true;
    }

    /** Plane off, framebuffers and handles gone, the buffer objects released, the surface destroyed. */
    void teardownSurface()
    {
        if (planeOn_)
            hidePlane();
        dropImports();
        if (prevBo_)
        {
            api_.gbm_surface_release_buffer (gsurf_, prevBo_);
            prevBo_ = nullptr;
        }
        if (feedbackTex_ && esurf_ != EGL_NO_SURFACE)
        {
            api_.glBindFramebuffer (GL_FRAMEBUFFER, 0);
            api_.glDeleteTextures (1, &feedbackTex_);
            dropGlow();
            dropLayers();
        }
        feedbackTex_ = 0;
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

    /** The two rows of the transform canvas -> clip space for the rotation: clip x = rx[0] * x + rx[1] * y + rx[2], clip y = ry[0] * x + ry[1] * y + ry[2]. */
    void transformRows (float rx[3], float ry[3]) const
    {
        const float W = static_cast<float> (w_), H = static_cast<float> (h_);
        // the buffer pixel (bx, by) of the canvas pixel (x, y), memory row 0 = the top of the GL framebuffer: 90: (y, W - x); 270: (H - y, x); 0: (x, y); 180: (W - x, H - y)
        if (rot_ == 90)       { rx[0] = 0.0f; rx[1] = 2.0f / H; rx[2] = -1.0f; ry[0] = 2.0f / W; ry[1] = 0.0f; ry[2] = -1.0f; }
        else if (rot_ == 270) { rx[0] = 0.0f; rx[1] = -2.0f / H; rx[2] = 1.0f; ry[0] = -2.0f / W; ry[1] = 0.0f; ry[2] = 1.0f; }
        else if (rot_ == 0)   { rx[0] = 2.0f / W; rx[1] = 0.0f; rx[2] = -1.0f; ry[0] = 0.0f; ry[1] = -2.0f / H; ry[2] = 1.0f; }
        else                  { rx[0] = -2.0f / W; rx[1] = 0.0f; rx[2] = 1.0f; ry[0] = 0.0f; ry[1] = 2.0f / H; ry[2] = -1.0f; }
    }
    void setTransform()
    {
        float rx[3], ry[3];
        transformRows (rx, ry);
        api_.glUniform3f (u_.rowX, rx[0], rx[1], rx[2]);
        api_.glUniform3f (u_.rowY, ry[0], ry[1], ry[2]);
    }
    /** The scissor box (GL pixels of the picture buffer) of a canvas rectangle, rounded outward. */
    void scissorBox (const ClipGeom& g, int box[4]) const
    {
        float rx[3], ry[3];
        transformRows (rx, ry);
        float lox = 1.0e9f, hix = -1.0e9f, loy = 1.0e9f, hiy = -1.0e9f;
        const float xs[2] = { static_cast<float> (g.x0), static_cast<float> (g.x1) }, ys[2] = { static_cast<float> (g.y0), static_cast<float> (g.y1) };
        for (float x : xs)
            for (float y : ys)
            {
                const float px = ((rx[0] * x + rx[1] * y + rx[2]) * 0.5f + 0.5f) * static_cast<float> (bw_), py = ((ry[0] * x + ry[1] * y + ry[2]) * 0.5f + 0.5f) * static_cast<float> (bh_);
                lox = std::min (lox, px); hix = std::max (hix, px); loy = std::min (loy, py); hiy = std::max (hiy, py);
            }
        box[0] = std::max (0, static_cast<int> (std::floor (lox + 0.001f)));
        box[1] = std::max (0, static_cast<int> (std::floor (loy + 0.001f)));
        box[2] = std::min (bw_, static_cast<int> (std::ceil (hix - 0.001f))) - box[0];
        box[3] = std::min (bh_, static_cast<int> (std::ceil (hiy - 0.001f))) - box[1];
    }
    /** The scissor: the clip of the scene (the picture and the cached layers, not the glow layers) and, while a partial picture is painted, the box of the part that is painted (the picture only). */
    void applyScissor()
    {
        const bool pic = curTarget_ < 0;
        const bool clipWant = clipOn_ && (pic || curTarget_ >= 2), frameWant = frameBoxOn_ && pic;
        if (! clipWant && ! frameWant)
        {
            api_.glDisable (GL_SCISSOR_TEST);
            return;
        }
        int x0 = 0, y0 = 0, x1 = bw_, y1 = bh_;
        if (clipWant)
        {
            x0 = std::max (x0, clipBox_[0]); y0 = std::max (y0, clipBox_[1]);
            x1 = std::min (x1, clipBox_[0] + std::max (0, clipBox_[2])); y1 = std::min (y1, clipBox_[1] + std::max (0, clipBox_[3]));
        }
        if (frameWant)
        {
            x0 = std::max (x0, frameBox_[0]); y0 = std::max (y0, frameBox_[1]);
            x1 = std::min (x1, frameBox_[0] + frameBox_[2]); y1 = std::min (y1, frameBox_[1] + frameBox_[3]);
        }
        api_.glEnable (GL_SCISSOR_TEST);
        api_.glScissor (x0, y0, std::max (0, x1 - x0), std::max (0, y1 - y0));
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

    static void premulStyle (uint32_t rgba, float* c) { premul (rgba, c); }

    void setPaintU (PaintU& u, const PaintState& p)
    {
        u.mode = static_cast<float> (p.mode);
        u.a = p.a;
        u.inv = p.b != p.a ? 1.0f / (p.b - p.a) : 1.0f;
        for (int i = 0; i < 4; ++i)
            premul (p.c[i], u.c[i]);
    }

    /** A new draw with the current state (style, paint, target). `paint`: whether the op takes its colour from the paint. */
    Draw& beginDraw (int prim, int kind, uint32_t rgba, float half, bool paint)
    {
        Draw& d = begin (prim, kind, rgba, half);
        d.soft = style_.soft;
        d.target = buildTarget_;
        d.tex = 0;
        if (paint)
            setPaintU (d.pt, paint_);
        d.prog = programFor (kind, paint && paint_.mode != 0);
        return d;
    }

    using Plan = CellPlan;

    void resetBuildState (const SceneData& s)
    {
        style_ = StyleState();
        paint_ = PaintState();
        setCurveOf (s, 0.0f, static_cast<float> (s.numDefault), 0.0f, static_cast<float> (s.w));
    }
    // the current polyline (the whole width until a Curve op says otherwise), continued horizontally to the two edges of its span
    void setCurveOf (const SceneData& s, float firstF, float nF, float x0, float width)
    {
        pts_.clear();
        int first = 0, n = 0;
        if (! curveSpan (firstF, nF, first, n))   // the floats come from shared memory: NaN, a huge value or a sum that overflows must not reach the index
            return;
        pts_.push_back ({ x0, s.y[first] });
        for (int i = 0; i < n; ++i)
            pts_.push_back ({ x0 + static_cast<float> (i) / static_cast<float> (n - 1) * (width - 1.0f) + 0.5f, s.y[first + i] });
        pts_.push_back ({ x0 + width, s.y[first + n - 1] });
    }

    void buildScene (const SceneData& s, const Plan* plan = nullptr)
    {
        verts_.clear();
        draws_.clear();
        pictureTarget_ = -1;
        buildTarget_ = -1;
        sceneFeedback_ = false;
        resetBuildState (s);
        buildOps (s, 0u, s.numOps, plan);
    }

    /** The draws of the ops k0 .. k1 - 1; the ops of a cached cell (plan) are skipped, the layer goes onto the picture where its first cell began. */
    void buildOps (const SceneData& s, uint32_t k0, uint32_t k1, const Plan* plan)
    {
        auto setCurve = [&] (float firstF, float nF, float x0, float width) { setCurveOf (s, firstF, nF, x0, width); };
        // the two rows of a band, continued the same way
        auto rowsOf = [&] (int first, int n, float x0, float width, std::vector<Pt>& out)
        {
            out.clear();
            out.push_back ({ x0, s.y[first] });
            for (int i = 0; i < n; ++i)
                out.push_back ({ x0 + static_cast<float> (i) / static_cast<float> (n - 1) * (width - 1.0f) + 0.5f, s.y[first + i] });
            out.push_back ({ x0 + width, s.y[first + n - 1] });
        };
        for (uint32_t k = k0; k < k1 && k < s.numOps && k < static_cast<uint32_t> (kMaxOps); ++k)
        {
            const SceneOp& o = s.ops[k];
            if (plan != nullptr && k < plan->inCell.size() && plan->inCell[k])
            {
                for (int l = 0; l < 2; ++l)
                    if (plan->blitAt[l] == static_cast<int> (k))
                        addLayerBlit (l, plan->box[l]);
                continue;
            }
            switch (o.op)
            {
                case kOpCellBegin:
                case kOpCellEnd:
                    style_ = StyleState();
                    paint_ = PaintState();
                    break;
                case kOpClip:
                {
                    ClipGeom g;
                    if (! clipGeom (o, g))
                        break;
                    Draw d;
                    d.type = 4;
                    d.target = -1;
                    if (g.on)
                        scissorBox (g, d.clipBox);
                    draws_.push_back (d);
                    break;
                }
                case kOpStyle: applyStyle (o, style_); break;
                case kOpPaint: applyPaint (o, paint_); break;
                case kOpClear:
                {
                    Draw d;
                    d.type = 1;
                    d.target = buildTarget_;
                    premul (o.rgba, d.colour);
                    draws_.push_back (d);
                    break;
                }
                case kOpRect:
                    beginDraw (GL_TRIANGLES, 0, o.rgba, 0.0f, false);
                    quad (o.f[0], o.f[1], o.f[2], o.f[3], 0, 0, 0, 0);
                    end();
                    break;
                case kOpVLine:
                {
                    if (! coordOk (o.f[0]) || ! coordOk (o.f[1]) || ! coordOk (o.f[2]) || ! std::isfinite (o.f[3]) || o.f[3] <= 0.0f || o.f[3] > kMaxStroke)
                        break;
                    const float half = o.f[3] * 0.5f, e = half + style_.soft * 0.5f + 1.0f;
                    beginDraw (GL_TRIANGLES, 1, o.rgba, half, false);
                    quad (o.f[0] - e, o.f[1], o.f[0] + e, o.f[2], -e, e, 0, 0);
                    end();
                    break;
                }
                case kOpDisc:
                {
                    if (! coordOk (o.f[0]) || ! coordOk (o.f[1]) || ! std::isfinite (o.f[2]) || o.f[2] <= 0.0f || o.f[2] > kMaxRadius)
                        break;
                    const float r = o.f[2], e = r + style_.soft * 0.5f + 1.0f;
                    beginDraw (GL_TRIANGLES, 2, o.rgba, r, true);
                    quad (o.f[0] - e, o.f[1] - e, o.f[0] + e, o.f[1] + e, -e, e, -e, e);
                    end();
                    break;
                }
                case kOpLine:
                {
                    LineGeom g;
                    if (! lineGeom (o, g))   // shared memory: validated, a bad op is skipped
                        break;
                    const float dx = g.x1 - g.x0, dy = g.y1 - g.y0, l = std::sqrt (dx * dx + dy * dy), ux = dx / l, uy = dy / l, nx = -uy, ny = ux, e = g.half + style_.soft * 0.5f + 1.0f;
                    const float cap = style_.round ? e : 0.0f;   // round caps: the quad reaches past the ends
                    Draw& d = beginDraw (GL_TRIANGLES, 1, o.rgba, g.half, true);
                    d.len = l;
                    d.round = style_.round ? 1.0f : 0.0f;
                    const float ax = g.x0 - ux * cap, ay = g.y0 - uy * cap, bx = g.x1 + ux * cap, by = g.y1 + uy * cap, ta = -cap, tb = l + cap;
                    // d0 = signed distance from the axis, d1 = the position along it
                    add (ax + nx * e, ay + ny * e, e, ta); add (ax - nx * e, ay - ny * e, -e, ta); add (bx + nx * e, by + ny * e, e, tb);
                    add (bx + nx * e, by + ny * e, e, tb); add (ax - nx * e, ay - ny * e, -e, ta); add (bx - nx * e, by - ny * e, -e, tb);
                    end();
                    break;
                }
                case kOpArc:
                {
                    ArcGeom g;
                    if (! arcGeom (o, g))
                        break;
                    const float e = g.r + g.half + style_.soft * 0.5f + 1.0f;
                    Draw& d = beginDraw (GL_TRIANGLES, 4 + g.mode, o.rgba, g.half, true);
                    d.arcR = g.r;
                    d.arcU[0] = g.u0x; d.arcU[1] = g.u0y; d.arcU[2] = g.u1x; d.arcU[3] = g.u1y;
                    d.arcE[0] = g.r * g.u0x; d.arcE[1] = g.r * g.u0y; d.arcE[2] = g.r * g.u1x; d.arcE[3] = g.r * g.u1y;
                    d.arcA0 = g.a0;
                    d.arcInv = g.sweep > 0.0f ? 1.0f / g.sweep : 0.0f;
                    d.round = style_.round && g.mode != 2 ? 1.0f : 0.0f;
                    quad (g.cx - e, g.cy - e, g.cx + e, g.cy + e, -e, e, -e, e);
                    end();
                    break;
                }
                case kOpFrame:
                {
                    FrameGeom g;
                    if (! frameGeom (o, g))
                        break;
                    beginDraw (GL_TRIANGLES, 0, o.rgba, 0.0f, false);
                    frameRects (g, [&] (int x0, int y0, int x1, int y1) { quad (static_cast<float> (x0), static_cast<float> (y0), static_cast<float> (x1), static_cast<float> (y1), 0, 0, 0, 0); });
                    end();
                    break;
                }
                case kOpRRect:
                {
                    RRectGeom g;
                    if (! rrectGeom (o, g))
                        break;
                    const float e = style_.soft * 0.5f + 1.0f, hx = 0.5f * (g.x1 - g.x0), hy = 0.5f * (g.y1 - g.y0);
                    Draw& d = beginDraw (GL_TRIANGLES, 8, o.rgba, 0.0f, true);
                    d.rr[0] = hx - g.r; d.rr[1] = hy - g.r; d.rr[2] = g.r; d.rr[3] = g.width;
                    quad (g.x0 - e, g.y0 - e, g.x1 + e, g.y1 + e, -hx - e, hx + e, -hy - e, hy + e);
                    end();
                    break;
                }
                case kOpBand:
                {
                    BandGeom g;
                    if (! bandGeom (o, g))
                        break;
                    rowsOf (g.firstA, g.n, g.x0, g.width, bandA_);
                    rowsOf (g.firstB, g.n, g.x0, g.width, bandB_);
                    beginDraw (GL_TRIANGLES, 9, o.rgba, 0.0f, true);
                    // four rows of vertices per column: the coverage ramps up over the first pixel, is flat inside, ramps down over the last (the exact coverage of the strip by a pixel row)
                    auto profile = [] (const Pt& a, const Pt& b, float y[4], float cv[4])
                    {
                        const float lo = std::min (a.y, b.y), hi = std::max (a.y, b.y), half = 0.5f * (hi - lo), c = 0.5f * (hi + lo), inner = std::fabs (half - 0.5f);
                        y[0] = c - half - 0.5f; y[1] = c - inner; y[2] = c + inner; y[3] = c + half + 0.5f;
                        cv[0] = 0.0f; cv[1] = cv[2] = std::min (1.0f, hi - lo); cv[3] = 0.0f;
                    };
                    for (size_t i = 0; i + 1 < bandA_.size(); ++i)
                    {
                        float y0[4], c0[4], y1[4], c1[4];
                        profile (bandA_[i], bandB_[i], y0, c0);
                        profile (bandA_[i + 1], bandB_[i + 1], y1, c1);
                        const float xa = bandA_[i].x, xb = bandA_[i + 1].x;
                        for (int r = 0; r < 3; ++r)
                        {
                            add (xa, y0[r], c0[r]); add (xb, y1[r], c1[r]); add (xa, y0[r + 1], c0[r + 1]);
                            add (xb, y1[r], c1[r]); add (xb, y1[r + 1], c1[r + 1]); add (xa, y0[r + 1], c0[r + 1]);
                        }
                    }
                    end();
                    break;
                }
                case kOpCurve:
                    setCurve (o.f[0], o.f[1], o.f[2], o.f[3]);
                    break;
                case kOpText:
                {
                    TextGeom g;
                    if (! textGeom (o, g))
                        break;
                    const int n = std::min (g.length, static_cast<int> (s.numText) - g.offset);
                    if (n <= 0)
                        break;
                    namespace sd = synthyboy_mpc::overlay::sdf;
                    Draw& d = beginDraw (GL_TRIANGLES, 7, o.rgba, 0.0f, true);
                    d.tex = 0;
                    d.textK = 2.0f * static_cast<float> (sd::kSpread) * g.size / static_cast<float> (sd::kEm);
                    const float iw = 1.0f / static_cast<float> (sd::kAtlasW), ih = 1.0f / static_cast<float> (sd::kAtlasH);
                    sd::layout (s.text + g.offset, n, g.x, g.baseline, g.size, g.tracking, [&] (const sd::Quad& q)
                    {
                        add (q.x0, q.y0, q.u0 * iw, q.v0 * ih); add (q.x1, q.y0, q.u1 * iw, q.v0 * ih); add (q.x0, q.y1, q.u0 * iw, q.v1 * ih);
                        add (q.x1, q.y0, q.u1 * iw, q.v0 * ih); add (q.x1, q.y1, q.u1 * iw, q.v1 * ih); add (q.x0, q.y1, q.u0 * iw, q.v1 * ih);
                    });
                    end();
                    break;
                }
                case kOpFill:
                case kOpFillGradient:
                    if (pts_.size() >= 2)
                    {
                        const bool grad = o.op == kOpFillGradient;
                        Draw& d = beginDraw (GL_TRIANGLES, grad ? 3 : 0, o.rgba, 0.0f, ! grad);
                        float yA = 0.0f, inv = 0.0f;
                        if (grad)
                        {
                            premul (o.rgba2, d.colour2);
                            yA = o.f[1];
                            inv = o.f[2] != o.f[1] ? 1.0f / (o.f[2] - o.f[1]) : 0.0f;
                        }
                        const float b = o.f[0];
                        auto v = [&] (float x, float y) { add (x, y, grad ? (y - yA) * inv : 0.0f); };
                        for (size_t i = 0; i + 1 < pts_.size(); ++i)
                        {
                            const Pt a = pts_[i], c = pts_[i + 1];
                            const float da = a.y - b, dc = c.y - b;
                            if (da * dc >= 0.0f)
                            {
                                v (a.x, a.y); v (a.x, b); v (c.x, c.y);
                                v (a.x, b); v (c.x, c.y); v (c.x, b);
                            }
                            else
                            {
                                const float t = da / (da - dc), xm = a.x + t * (c.x - a.x);   // the curve crosses the baseline at xm
                                v (a.x, a.y); v (a.x, b); v (xm, b);
                                v (xm, b); v (c.x, c.y); v (c.x, b);
                            }
                        }
                        end();
                    }
                    break;
                case kOpStroke:
                    if (pts_.size() >= 2 && std::isfinite (o.f[0]) && o.f[0] > 0.0f && o.f[0] <= kMaxStroke)
                    {
                        const float half = o.f[0];
                        beginDraw (GL_TRIANGLE_STRIP, 1, o.rgba, half, true);
                        synthyboy_mpc::overlay::gpu::buildStrokeStrip (pts_, half, style_.soft, [&] (float x, float y, float d0, float d1) {
                            add (x, y, d0, d1);
                        });
                        end();
                    }
                    break;
                case kOpFeedback:
                {
                    FeedbackGeom g;
                    if (! feedbackGeom (o, g))
                        break;
                    sceneFeedback_ = true;
                    Draw& d = beginDraw (GL_TRIANGLES, 10, 0, 0.0f, false);
                    d.tex = 3;
                    d.colour[0] = d.colour[1] = d.colour[2] = d.colour[3] = g.gain;
                    quad (g.x0, g.y0, g.x1, g.y1, 0, 0, 0, 0);
                    end();
                    break;
                }
                case kOpGlowBegin:
                {
                    int div = 0;
                    if (! glowBeginGeom (o, div) || buildTarget_ == 0)
                        break;
                    Draw d;
                    d.type = 2;
                    d.divisor = div;
                    d.target = 0;
                    draws_.push_back (d);
                    buildTarget_ = 0;
                    glowDivisor_ = div;
                    break;
                }
                case kOpGlowEnd:
                {
                    GlowGeom g;
                    if (buildTarget_ != 0 || ! glowEndGeom (o, g))
                    {
                        buildTarget_ = pictureTarget_;
                        break;
                    }
                    buildTarget_ = pictureTarget_;
                    const float W = static_cast<float> (s.w), H = static_cast<float> (s.h);
                    const int gw = std::max (1, (bw_ + glowDivisor_ - 1) / glowDivisor_), gh = std::max (1, (bh_ + glowDivisor_ - 1) / glowDivisor_);
                    // blur horizontally layer 0 -> 1, vertically 1 -> 0, then add layer 0 to the picture; each a full screen quad
                    for (int pass = 0; pass < 3; ++pass)
                    {
                        Draw& d = beginDraw (GL_TRIANGLES, pass == 0 ? 12 : (pass == 1 ? 13 : 10), 0, 0.0f, false);
                        d.target = pass == 0 ? 1 : (pass == 1 ? 0 : pictureTarget_);
                        d.tex = pass == 0 ? 1 : (pass == 1 ? 2 : 1);
                        d.texel[0] = g.blur / static_cast<float> (gw);
                        d.texel[1] = g.blur / static_cast<float> (gh);
                        if (pass == 2)
                        {
                            premul (o.rgba, d.colour);
                            for (int c = 0; c < 4; ++c)
                                d.colour[c] *= g.gain;
                            d.additive = true;
                        }
                        if (g.rect)   // the passes are as big as the rectangle (a little more for the blur to reach): the cost follows the area
                        {
                            const float m = pass == 2 ? 0.0f : 3.0f * static_cast<float> (glowDivisor_) * g.blur * 4.0f;
                            quad (std::max (0.0f, g.x0 - m), std::max (0.0f, g.y0 - m), std::min (W, g.x1 + m), std::min (H, g.y1 + m), 0, 0, 0, 0);
                        }
                        else
                            quad (0.0f, 0.0f, W, H, 0, 0, 0, 0);
                        end();
                    }
                    break;
                }
                default:
                    break;
            }
        }
    }

    void setTexParams()
    {
        api_.glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        api_.glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        api_.glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        api_.glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    GLuint makeTexture (int w, int h)
    {
        GLuint t = 0;
        api_.glGenTextures (1, &t);
        api_.glBindTexture (GL_TEXTURE_2D, t);
        std::vector<uint8_t> zero (static_cast<size_t> (w) * static_cast<size_t> (h) * 4, 0);
        api_.glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, zero.data());
        setTexParams();
        curTex_ = -1;
        return t;
    }

    /** The two layers of the bloom (1 / divisor of the picture), made when first needed and again when the size changes. false: they cannot be made (the glow is skipped). */
    bool ensureGlow (int divisor)
    {
        const int gw = std::max (1, (bw_ + divisor - 1) / divisor), gh = std::max (1, (bh_ + divisor - 1) / divisor);
        if (glowTex_[0] && gw == glowW_ && gh == glowH_)
            return true;
        dropGlow();
        for (int i = 0; i < 2; ++i)
        {
            glowTex_[i] = makeTexture (gw, gh);
            api_.glGenFramebuffers (1, &glowFbo_[i]);
            api_.glBindFramebuffer (GL_FRAMEBUFFER, glowFbo_[i]);
            api_.glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, glowTex_[i], 0);
            if (api_.glCheckFramebufferStatus (GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            {
                api_.glBindFramebuffer (GL_FRAMEBUFFER, 0);
                dropGlow();
                return false;
            }
        }
        api_.glBindFramebuffer (GL_FRAMEBUFFER, 0);
        glowW_ = gw;
        glowH_ = gh;
        buildTarget_ = -2;
        return true;
    }
    void dropGlow()
    {
        for (int i = 0; i < 2; ++i)
        {
            if (glowFbo_[i]) api_.glDeleteFramebuffers (1, &glowFbo_[i]);
            if (glowTex_[i]) api_.glDeleteTextures (1, &glowTex_[i]);
            glowFbo_[i] = 0;
            glowTex_[i] = 0;
        }
        glowW_ = glowH_ = 0;
    }

    void bindTarget (int target)
    {
        if (target == buildTarget_)
            return;
        buildTarget_ = target;
        curTarget_ = target;
        applyScissor();
        if (target < 0)
        {
            api_.glBindFramebuffer (GL_FRAMEBUFFER, 0);
            api_.glViewport (0, 0, bw_, bh_);
        }
        else if (target >= 2)   // a cached layer: the size of the picture
        {
            api_.glBindFramebuffer (GL_FRAMEBUFFER, layerFbo_[target - 2]);
            api_.glViewport (0, 0, bw_, bh_);
        }
        else
        {
            api_.glBindFramebuffer (GL_FRAMEBUFFER, glowFbo_[target]);
            api_.glViewport (0, 0, glowW_, glowH_);
        }
    }

    /** The uniforms of a draw; only what differs from the draw before is sent (hundreds of draws of a frame share most of their state). */
    void applyUniforms (const Draw& d)
    {
        if (d.prog != curProg_)
        {
            curProg_ = d.prog;
            api_.glUseProgram (prog_[curProg_]);
            u_ = locs_[curProg_];
            setTransform();
            haveLast_ = false;
        }
        const Draw& l = last_;
        const bool f = ! haveLast_;
        auto same4 = [] (const float* a, const float* b) { return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3]; };
        const int want = d.tex;
        if (want != curTex_)
        {
            api_.glBindTexture (GL_TEXTURE_2D, want == 0 ? atlasTex_ : (want == 3 ? feedbackTex_ : (want >= 4 ? layerTex_[want - 4] : glowTex_[want - 1])));
            curTex_ = want;
        }
        if (f || d.additive != l.additive)
            api_.glBlendFunc (GL_ONE, d.additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
        if (f || d.kind != l.kind) api_.glUniform1f (u_.kind, static_cast<float> (d.kind));
        if (f || ! same4 (d.colour, l.colour)) api_.glUniform4f (u_.c0, d.colour[0], d.colour[1], d.colour[2], d.colour[3]);
        if (d.kind == 3) api_.glUniform4f (u_.c1, d.colour2[0], d.colour2[1], d.colour2[2], d.colour2[3]);
        if (f || d.half != l.half) api_.glUniform1f (u_.half, d.half);
        if (f || d.soft != l.soft) api_.glUniform1f (u_.soft, d.soft);
        if (d.kind == 1)
        {
            api_.glUniform1f (u_.len, d.len);
            api_.glUniform4f (u_.a, d.arcA0, d.arcInv, d.round, 0.0f);
        }
        if (d.kind >= 4 && d.kind <= 6)
        {
            api_.glUniform1f (u_.r, d.arcR);
            api_.glUniform4f (u_.u, d.arcU[0], d.arcU[1], d.arcU[2], d.arcU[3]);
            api_.glUniform4f (u_.e, d.arcE[0], d.arcE[1], d.arcE[2], d.arcE[3]);
            api_.glUniform4f (u_.a, d.arcA0, d.arcInv, d.round, 0.0f);
        }
        if (d.kind == 7) api_.glUniform1f (u_.textK, d.textK);
        if (d.kind == 8) api_.glUniform4f (u_.rr, d.rr[0], d.rr[1], d.rr[2], d.rr[3]);
        if (d.kind >= 12) api_.glUniform2f (u_.texel, d.texel[0], d.texel[1]);
        const bool paintSame = ! f && std::memcmp (&d.pt, &l.pt, sizeof d.pt) == 0;
        if (! paintSame)
        {
            api_.glUniform3f (u_.pm, d.pt.mode, d.pt.a, d.pt.inv);
            if (d.pt.mode > 0.5f)
            {
                api_.glUniform4f (u_.p0, d.pt.c[0][0], d.pt.c[0][1], d.pt.c[0][2], d.pt.c[0][3]);
                api_.glUniform4f (u_.p1, d.pt.c[1][0], d.pt.c[1][1], d.pt.c[1][2], d.pt.c[1][3]);
                api_.glUniform4f (u_.p2, d.pt.c[2][0], d.pt.c[2][1], d.pt.c[2][2], d.pt.c[2][3]);
                api_.glUniform4f (u_.p3, d.pt.c[3][0], d.pt.c[3][1], d.pt.c[3][2], d.pt.c[3][3]);
            }
        }
        last_ = d;
        haveLast_ = true;
    }

#ifdef SYNTHYBOY_OVERLAY_BENCH
    // ---------------------------------------------------------------------------------------------------------------------------------------------------------
    // THE MEASURING SCENE (a debug build only: SYNTHYBOY_MPC_OVERLAY_BENCH, never in the product). The helper draws a scene of its own instead of the plugin's, as fast as the pace grid allows, and writes
    // the statistics of every configuration to /tmp/sb-bench-stats.txt. The configuration is the file /tmp/sb-bench.conf (key=value lines, read again when it changes):
    //   id=name  curves=N  points=P (200 .. 1600)  half=0.5 (half width, px)  soft=1  layers=N (full region translucent gradient layers)  glow=0|1|2 (none | a wide faint extra pass per curve | a bloom layer)
    //   feedback=0|1  text=N (strings)  rrects=N  bands=N  stride=1|2 (vblanks per picture)  window=seconds
    struct BenchCfg
    {
        std::string id = "idle";
        int curves = 0, points = 800, layers = 0, glow = 0, feedback = 0, text = 0, rrects = 0, bands = 0, stride = 0;
        float half = 0.5f, soft = 1.0f, window = 6.0f;
    };
    static std::string benchValue (const std::string& text, const char* key, const std::string& def)
    {
        const std::string k = std::string (key) + "=";
        size_t at = 0;
        while ((at = text.find (k, at)) != std::string::npos)
        {
            if (at == 0 || text[at - 1] == '\n' || text[at - 1] == ' ')
            {
                size_t e = text.find_first_of ("\n ", at);
                return text.substr (at + k.size(), (e == std::string::npos ? text.size() : e) - at - k.size());
            }
            ++at;
        }
        return def;
    }
    void benchPoll()
    {
        const double t = nowMs();
        if (t - benchLastPoll_ < 400.0)
            return;
        benchLastPoll_ = t;
        struct stat st;
        if (::stat ("/tmp/sb-bench.conf", &st) != 0 || (st.st_mtime == benchMtime_ && st.st_mtim.tv_nsec == benchMtimeNs_))
        {
            if (benchActive_ && t - benchWindowStart_ >= benchCfg_.window * 1000.0 && ! benchReported_)
                benchReport();
            return;
        }
        benchMtime_ = st.st_mtime;
        benchMtimeNs_ = st.st_mtim.tv_nsec;
        std::string text;
        if (FILE* f = std::fopen ("/tmp/sb-bench.conf", "r"))
        {
            char buf[1024];
            size_t n;
            while ((n = std::fread (buf, 1, sizeof buf, f)) > 0)
                text.append (buf, n);
            std::fclose (f);
        }
        if (benchActive_ && ! benchReported_)
            benchReport();
        BenchCfg c;
        c.id = benchValue (text, "id", "run");
        c.curves = std::atoi (benchValue (text, "curves", "0").c_str());
        c.points = std::min (kMaxCurve, std::max (2, std::atoi (benchValue (text, "points", "800").c_str())));
        c.half = static_cast<float> (std::atof (benchValue (text, "half", "0.5").c_str()));
        c.soft = static_cast<float> (std::atof (benchValue (text, "soft", "1").c_str()));
        c.layers = std::atoi (benchValue (text, "layers", "0").c_str());
        c.glow = std::atoi (benchValue (text, "glow", "0").c_str());
        c.feedback = std::atoi (benchValue (text, "feedback", "0").c_str());
        c.text = std::atoi (benchValue (text, "text", "0").c_str());
        c.rrects = std::atoi (benchValue (text, "rrects", "0").c_str());
        c.bands = std::atoi (benchValue (text, "bands", "0").c_str());
        c.stride = std::atoi (benchValue (text, "stride", "0").c_str());
        c.window = static_cast<float> (std::atof (benchValue (text, "window", "6").c_str()));
        benchCfg_ = c;
        if (c.stride >= 1 && c.stride <= 4 && c.stride != stride_)
        {
            stride_ = c.stride;
            pacer_ = std::make_unique<FramePacer> (*clock_, periodMs_, stride_, (flags_ & kFlagVblank) != 0);
        }
        benchActive_ = true;
        benchReported_ = false;
        benchWindowStart_ = nowMs() + 700.0;   // the first 0.7 s (the new scene's first pictures, the pace grid settling) are not counted
        benchRender_.clear();
        benchGen_.clear(); benchBuild_.clear(); benchSubmit_.clear(); benchFinish_.clear();
        benchSet_.clear();
        benchInterval_.clear();
        benchSkippedStart_ = skipped_;
        benchLateStart_ = late_;
        benchFramesStart_ = frames_;
        benchPhase_ = 0.0f;
    }
    static double benchPercentile (std::vector<float>& v, double q)
    {
        if (v.empty()) return 0.0;
        std::sort (v.begin(), v.end());
        return v[std::min (v.size() - 1, static_cast<size_t> (q * static_cast<double> (v.size())))];
    }
    void benchReport()
    {
        benchReported_ = true;
        if (FILE* f = std::fopen ("/tmp/sb-pace-trace.txt", "w"))
        {
            std::fprintf (f, "# id=%s stride=%d noslot=%d; ms since the first picture: render start, render end, SETPLANE call, SETPLANE return, vblank counter at the return\n", benchCfg_.id.c_str(), stride_, noSlot_ ? 1 : 0);
            const double b = benchTrace_.empty() ? 0.0 : benchTrace_[0][0];
            for (const auto& r : benchTrace_)
                std::fprintf (f, "%.2f %.2f %.2f %.2f %.0f\n", r[0] - b, r[1] - b, r[2] - b, r[3] - b, r[4]);
            std::fclose (f);
        }
        benchTrace_.clear();
        const double secs = std::max (0.001, (nowMs() - benchWindowStart_) / 1000.0);
        double avg = 0.0, avgSet = 0.0, avgInt = 0.0;
        for (float v : benchRender_) avg += v;
        for (float v : benchSet_) avgSet += v;
        for (float v : benchInterval_) avgInt += v;
        const size_t n = benchRender_.size();
        if (n) { avg /= static_cast<double> (n); avgSet /= static_cast<double> (n); }
        if (! benchInterval_.empty()) avgInt /= static_cast<double> (benchInterval_.size());
        auto mean = [] (const std::vector<float>& v) { double a = 0.0; for (float x : v) a += x; return v.empty() ? 0.0 : a / static_cast<double> (v.size()); };
        const double gen = mean (benchGen_), bld = mean (benchBuild_), sub = mean (benchSubmit_), fin = mean (benchFinish_), finP99 = benchPercentile (benchFinish_, 0.99);
        const double p99 = benchPercentile (benchRender_, 0.99), mx = benchRender_.empty() ? 0.0 : benchRender_.back();
        const double ip99 = benchPercentile (benchInterval_, 0.99);
        if (FILE* f = std::fopen ("/tmp/sb-bench-stats.txt", "a"))
        {
            std::fprintf (f, "id=%s frames=%zu seconds=%.2f fps=%.2f render_avg_ms=%.3f render_p99_ms=%.3f render_max_ms=%.3f setplane_avg_ms=%.3f interval_avg_ms=%.3f interval_p99_ms=%.3f skipped_vblanks=%lu late_setplane=%lu stride=%d verts=%zu draws=%zu msaa=%d gen_ms=%.3f build_ms=%.3f submit_ms=%.3f finish_ms=%.3f finish_p99_ms=%.3f\n",
                          benchCfg_.id.c_str(), n, secs, static_cast<double> (n) / secs, avg, p99, mx, avgSet, avgInt, ip99, skipped_ - benchSkippedStart_, late_ - benchLateStart_, stride_, verts_.size(), draws_.size(), wantSamples_, gen, bld, sub, fin, finP99);
            std::fclose (f);
        }
    }
    void benchRecord (double renderMs, double setMs, double intervalMs)
    {
        if (! benchActive_ || benchReported_ || nowMs() < benchWindowStart_)
            return;
        if (benchRender_.size() < 20000)
        {
            benchRender_.push_back (static_cast<float> (renderMs));
            benchSet_.push_back (static_cast<float> (setMs));
            benchInterval_.push_back (static_cast<float> (intervalMs));
        }
    }
    void benchScene (SceneData& sd)
    {
        const BenchCfg& c = benchCfg_;
        SceneBuilder sb (sd);
        sb.begin (kKindUi, 0, w_, h_);
        sb.clear (rgba8 (0x0b, 0x10, 0x26, 255));
        benchPhase_ += 0.07f;
        const float W = static_cast<float> (std::min (w_, 800)), x0 = static_cast<float> ((w_ - static_cast<int> (W)) / 2), H = static_cast<float> (h_);
        std::vector<float> rows (static_cast<size_t> (c.points)), rows2 (static_cast<size_t> (c.points));
        for (int l = 0; l < c.layers; ++l)
        {
            const float flat[2] = { 0.0f, 0.0f };
            sb.curve (flat, 2, 0, w_);
            sb.fillGradient (H, 0.0f, H, rgba8 (0x2e, 0xe6, 0xf0, 18), rgba8 (0xb4, 0x5c, 0xff, 18));
        }
        if (c.feedback)
        {
            sb.rect (x0, 0.0f, x0 + W, H, rgba8 (0x0b, 0x10, 0x26, 255));
            sb.feedback (x0, 0.0f, x0 + W, H, 0.82f);
        }
        for (int r = 0; r < c.rrects; ++r)
        {
            const float rx = 8.0f + static_cast<float> ((r * 37) % std::max (1, w_ - 120)), ry = 8.0f + static_cast<float> ((r * 53) % std::max (1, h_ - 60));
            sb.rrect (rx, ry, rx + 100.0f, ry + 44.0f, 10.0f, rgba8 (0x14, 0x1a, 0x38, 200));
            sb.rrect (rx, ry, rx + 100.0f, ry + 44.0f, 10.0f, rgba8 (0x6c, 0x7b, 0xff, 255), 1.5f);
        }
        auto build = [&] (int i)
        {
            for (int k = 0; k < c.points; ++k)
            {
                const float t = static_cast<float> (k) / static_cast<float> (c.points - 1);
                const float amp = 0.30f * H / static_cast<float> (1 + (i % 4)), cy = 0.5f * H + (static_cast<float> (i % 7) - 3.0f) * 0.04f * H;
                rows[static_cast<size_t> (k)] = cy + amp * std::sin (6.2832f * (t * static_cast<float> (2 + i % 5) + benchPhase_ * 0.1f * static_cast<float> (1 + i % 3))) * (0.6f + 0.4f * std::sin (13.0f * t + benchPhase_ + static_cast<float> (i)));
                rows2[static_cast<size_t> (k)] = rows[static_cast<size_t> (k)] + 6.0f + 4.0f * std::sin (9.0f * t + static_cast<float> (i));
            }
        };
        std::vector<int> firsts;
        auto drawCurves = [&] (bool glowSource, bool again)
        {
            for (int i = 0; i < c.curves; ++i)
            {
                if (again)
                    sb.curveAt (firsts[static_cast<size_t> (i)], c.points, static_cast<int> (x0), static_cast<int> (W));
                else
                {
                    build (i);
                    const int first = sb.curve (rows.data(), c.points, static_cast<int> (x0), static_cast<int> (W));
                    firsts.push_back (first);
                }
                sb.paintX (x0, x0 + W, rgba8 (0x2e, 0xe6, 0xf0, 255), rgba8 (0x6c, 0x7b, 0xff, 255), rgba8 (0xb4, 0x5c, 0xff, 255), rgba8 (0xff, 0x4f, 0xc3, 255));
                if (glowSource)
                {
                    sb.style (1.0f, false);
                    sb.stroke (std::max (c.half, 1.5f), 255);
                }
                else
                {
                    if (c.glow == 1)
                    {
                        sb.style (8.0f, false);
                        sb.stroke (3.0f, 60);
                    }
                    sb.style (c.soft, false);
                    sb.stroke (c.half, 255);
                }
            }
            sb.paintOff();
            sb.style (1.0f, false);
        };
        for (int i = 0; i < c.bands; ++i)
        {
            build (i);
            sb.paintY (0.0f, H, rgba8 (0x2e, 0xe6, 0xf0, 255), rgba8 (0x6c, 0x7b, 0xff, 255), rgba8 (0xb4, 0x5c, 0xff, 255), rgba8 (0xff, 0x4f, 0xc3, 255));
            sb.band (rows.data(), rows2.data(), c.points, static_cast<int> (x0), static_cast<int> (W), 140);
            sb.paintOff();
        }
        if (c.glow == 2 && c.curves > 0)
        {
            sb.glowBegin (4);
            drawCurves (true, false);
            sb.glowEnd (1.6f, 1.0f);
            drawCurves (false, true);
        }
        else
            drawCurves (false, false);
        for (int t = 0; t < c.text; ++t)
            sb.text (12.0f + static_cast<float> ((t * 71) % std::max (1, w_ - 200)), 30.0f + static_cast<float> ((t * 29) % std::max (1, h_ - 40)), 14.0f + static_cast<float> (t % 5) * 4.0f, "Wavetable Position 1.25 Hz", rgba8 (0xe6, 0xe6, 0xf2, 230));
    }
    BenchCfg benchCfg_;
    std::vector<std::array<double, 5>> benchTrace_;
    bool benchActive_ = false, benchReported_ = true;
    double benchLastPoll_ = 0.0, benchWindowStart_ = 0.0, benchLastFrameMs_ = 0.0;
    time_t benchMtime_ = 0;
    long benchMtimeNs_ = 0;
    std::vector<float> benchRender_, benchSet_, benchInterval_, benchGen_, benchBuild_, benchSubmit_, benchFinish_;
    unsigned long benchSkippedStart_ = 0, benchLateStart_ = 0, benchFramesStart_ = 0;
    float benchPhase_ = 0.0f;
#endif


    // ---------------------------------------------------------------------------------------------------------------------------------------------------------
    // the cached layers (scene.h, CellBegin): the cells of a scene are drawn into two textures the size of the picture; only the cells that changed (and the cells over them) are drawn again, in a scissor box
    bool ensureLayer (int l)
    {
        if (layerTex_[l] != 0 && layerW_ == bw_ && layerH_ == bh_)
            return true;
        if (layerW_ != bw_ || layerH_ != bh_)
            dropLayers();
        if (layerTex_[l] != 0)
            return true;
        layerTex_[l] = makeTexture (bw_, bh_);
        api_.glGenFramebuffers (1, &layerFbo_[l]);
        api_.glBindFramebuffer (GL_FRAMEBUFFER, layerFbo_[l]);
        api_.glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, layerTex_[l], 0);
        const bool ok = api_.glCheckFramebufferStatus (GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        api_.glBindFramebuffer (GL_FRAMEBUFFER, 0);
        buildTarget_ = -2;
        curTex_ = -1;
        if (! ok)
        {
            dropLayers();
            return false;
        }
        layerW_ = bw_;
        layerH_ = bh_;
        cache_.forget (l);
        return true;
    }
    void dropLayers()
    {
        for (int l = 0; l < 2; ++l)
        {
            if (layerFbo_[l]) api_.glDeleteFramebuffers (1, &layerFbo_[l]);
            if (layerTex_[l]) api_.glDeleteTextures (1, &layerTex_[l]);
            layerFbo_[l] = 0;
            layerTex_[l] = 0;
            cache_.forget (l);
        }
        layerW_ = layerH_ = 0;
    }
    void addLayerBlit (int l, const int box[4])
    {
        Draw& d = beginDraw (GL_TRIANGLES, 10, 0, 0.0f, false);
        d.tex = 4 + l;
        d.target = -1;
        d.colour[0] = d.colour[1] = d.colour[2] = d.colour[3] = 1.0f;
        quad (static_cast<float> (box[0]), static_cast<float> (box[1]), static_cast<float> (box[2]), static_cast<float> (box[3]), 0, 0, 0, 0);
        end();
    }

    /** The boxes of the picture to paint now (canvas pixels): with the age of the back buffer (EGL_EXT_buffer_age) only what changed since that picture: the cells that were drawn again, the places of the ops that are
        drawn every picture (now and the picture before). false: the whole picture (age unknown, a changed clip, the first pictures, or the boxes cover most of it). */
    bool partialBoxes (std::vector<DamageRect>& out)
    {
        liveBoxes_.clear();
        int clip[4] = {};
        for (const Draw& d : draws_)
        {
            if (d.type == 4)
            {
                for (int i = 0; i < 4; ++i)
                    clip[i] = d.clipBox[i];
                continue;
            }
            if (d.type != 0 || d.count <= 0 || (d.kind == 10 && d.tex >= 4))
                continue;
            float x0 = 1.0e9f, y0 = 1.0e9f, x1 = -1.0e9f, y1 = -1.0e9f;
            for (int i = d.first; i < d.first + d.count && i < static_cast<int> (verts_.size()); ++i)
            {
                x0 = std::min (x0, verts_[static_cast<size_t> (i)].x); x1 = std::max (x1, verts_[static_cast<size_t> (i)].x);
                y0 = std::min (y0, verts_[static_cast<size_t> (i)].y); y1 = std::max (y1, verts_[static_cast<size_t> (i)].y);
            }
            if (x1 >= x0 && y1 >= y0)
                liveBoxes_.push_back ({ static_cast<int> (std::floor (x0)) - 2, static_cast<int> (std::floor (y0)) - 2, static_cast<int> (std::ceil (x1)) + 2, static_cast<int> (std::ceil (y1)) + 2 });
        }
        partialOk_ = ::access ("/tmp/synthyboy-overlay-nopartial", F_OK) != 0;
        EGLint age = 0;
        if (! api_.eglQuerySurface (dpy_, esurf_, 0x313D /* EGL_BUFFER_AGE_EXT */, &age))
            age = 0;
        return partial_.plan (layerDamage_, liveBoxes_, clip, static_cast<int> (age), partialOk_, w_, h_, out);   // gpu/cell_cache.h (tested on the Mac)
    }

    /** Brings the layers up to date: what changed since the last picture (a cell whose hash or rectangle is not the one at its place before, a cell that came or went) is drawn again in the layer, in a scissor box, together with the cells that lie over it. */
    bool renderLayers (const SceneData& s, const Plan& plan)
    {
        lastRedrawn_ = 0;
        lastCached_ = 0;
        layerDamage_.clear();
        for (int l = 0; l < 2; ++l)
        {
            if (! plan.layer[l])
            {
                cache_.forget (l);
                continue;
            }
            if (! ensureLayer (l))
                return false;
            const std::vector<CellKey> keys = LayerCache::keysOf (plan, l);
            std::vector<DamageRect> dmg;
            cache_.damage (l, keys, dmg);
            layerDamage_.insert (layerDamage_.end(), dmg.begin(), dmg.end());
            for (const DamageRect& r : dmg)
            {
                verts_.clear();
                draws_.clear();
                sceneFeedback_ = false;
                const int target = 2 + l;
                pictureTarget_ = target;            // the picture of a cell is the layer (its bloom is added into the layer)
                buildTarget_ = target;
                {
                    Draw c;
                    c.type = 4;
                    c.target = target;
                    ClipGeom g;
                    g.x0 = r.x0; g.y0 = r.y0; g.x1 = r.x1; g.y1 = r.y1; g.on = true;
                    scissorBox (g, c.clipBox);
                    draws_.push_back (c);
                    Draw k;
                    k.type = 1;
                    k.target = target;
                    draws_.push_back (k);
                }
                for (const CellRef& c : plan.cells)
                    if (c.layer == l && c.x0 < r.x1 && r.x0 < c.x1 && c.y0 < r.y1 && r.y0 < c.y1)
                    {
                        resetBuildState (s);
                        buildOps (s, static_cast<uint32_t> (c.begin) + 1u, static_cast<uint32_t> (c.end), nullptr);
                        ++lastRedrawn_;
                    }
                runDraws();
            }
            cache_.commit (l, keys);
        }
        pictureTarget_ = -1;
        for (const CellRef& c : plan.cells)
            lastCached_ += static_cast<uint32_t> (c.end - c.begin - 1);
        api_.glDisable (GL_SCISSOR_TEST);
        buildTarget_ = -3;
        return true;
    }

    /** Uploads the vertices and executes the draws (draws_) in order: the clears, the clips, the glow layers, the polygons. The target of every draw is its own (the picture, a glow layer, a cached layer). */
    void runDraws (bool upload = true)
    {
        if (upload)
        {
            api_.glBindBuffer (GL_ARRAY_BUFFER, vbo_);
            api_.glBufferData (GL_ARRAY_BUFFER, static_cast<GLsizeiptr> (verts_.size() * sizeof (Vtx)), verts_.data(), GL_DYNAMIC_DRAW);
        }
        setTransform();
        curTarget_ = -2;
        buildTarget_ = -3;      // unknown: the first draw binds its target
        curTex_ = -1;
        haveLast_ = false;
        curProg_ = -1;
        clipOn_ = false;
        applyScissor();
        bool glowActive = false;
        for (const Draw& d : draws_)
        {
            if (d.type == 2)
            {
                glowActive = ensureGlow (d.divisor);
                if (glowActive)
                {
                    api_.glClearColor (0.0f, 0.0f, 0.0f, 0.0f);
                    bindTarget (1);          // both layers start empty: a blur limited to a rectangle reads the edges of the second one
                    api_.glClear (GL_COLOR_BUFFER_BIT);
                    bindTarget (0);
                    api_.glClear (GL_COLOR_BUFFER_BIT);
                }
                continue;
            }
            if (d.type == 4)
            {
                bindTarget (d.target);
                clipOn_ = d.clipBox[2] > 0 && d.clipBox[3] > 0;
                for (int i = 0; i < 4; ++i)
                    clipBox_[i] = d.clipBox[i];
                applyScissor();
                continue;
            }
            const bool glowPass = d.kind == 12 || d.kind == 13 || (d.kind == 10 && (d.tex == 1 || d.tex == 2));
            if ((d.target == 0 || d.target == 1 || glowPass) && ! glowActive)
                continue;                       // the glow layer could not be made: its ops and passes are not drawn
            bindTarget (d.target);
            if (d.type == 1)
            {
                api_.glClearColor (d.colour[0], d.colour[1], d.colour[2], d.colour[3]);
                api_.glClear (GL_COLOR_BUFFER_BIT);
                continue;
            }
            applyUniforms (d);
            api_.glDrawArrays (static_cast<GLenum> (d.prim), d.first, d.count);
            if (d.kind == 10 && (d.tex == 1 || d.tex == 2))
                glowActive = false;             // the last pass of a glow
        }
    }

    // ---------------------------------------------------------------------------------------------------------------------------------------------------------
    // one picture
    bool renderAndPresent()
    {
        const double t0 = nowMs();
        noSlot_ = true;   // stride 1: the plane update follows the render at once
        SceneData& s = scene_;
        uint32_t seq = 0;
#ifdef SYNTHYBOY_OVERLAY_BENCH
        benchPoll();
        if (benchActive_)
        {
            benchScene (s);
            seq = static_cast<uint32_t> (frames_ * 2 + 2);
        }
        else
#endif
        {
            if (! readSceneForView (block_, s, seq, viewIndex_, w_, h_))
                return false;    // the writer or SHOW is still catching up
        }
        const double tGen = nowMs();
        Plan plan;
        bool cached = layersOk_ && planCells (s, plan);
        if (cached && ! renderLayers (s, plan))
        {
            cached = false;           // the layers cannot be made: this scene is drawn whole, and the next ones too
            layersOk_ = false;
        }
        if (! cached)
            lastCached_ = lastRedrawn_ = 0;
        buildScene (s, cached ? &plan : nullptr);
        const double tBuilt = nowMs();   // the time from tGen to here: the layers that changed (drawn into their textures), the vertices of the rest
        std::vector<DamageRect> boxes;
        bool partial = false;
        if (cached)
            partial = partialBoxes (boxes);
        else
        {
            partial_.reset();
        }
        api_.glBindBuffer (GL_ARRAY_BUFFER, vbo_);
        api_.glBufferData (GL_ARRAY_BUFFER, static_cast<GLsizeiptr> (verts_.size() * sizeof (Vtx)), verts_.data(), GL_DYNAMIC_DRAW);
        lastPaintedPx_ = 0;
        if (partial)
        {
            for (const DamageRect& b : boxes)
            {
                ClipGeom g;
                g.x0 = b.x0; g.y0 = b.y0; g.x1 = b.x1; g.y1 = b.y1; g.on = true;
                scissorBox (g, frameBox_);
                frameBoxOn_ = true;
                lastPaintedPx_ += static_cast<uint32_t> (std::max (0, frameBox_[2]) * std::max (0, frameBox_[3]));
                runDraws (false);
            }
            frameBoxOn_ = false;
        }
        else
        {
            lastPaintedPx_ = static_cast<uint32_t> (bw_ * bh_);
            runDraws (false);
        }
        bindTarget (-1);
        if (sceneFeedback_ || prevFeedback_)
        {
            api_.glBindTexture (GL_TEXTURE_2D, feedbackTex_);
            curTex_ = 3;
            api_.glCopyTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, 0, 0, bw_, bh_);
        }
        prevFeedback_ = sceneFeedback_;
        const double tSubmitted = nowMs();
        api_.glFinish();    // the buffer must be finished before the display reads it
        const double tFinished = nowMs();
        probeBufferAge();
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
        publishRender (t0, tGen, tBuilt, tSubmitted, tFinished, t1, s);
        if (! (stride_ == 1 && noSlot_))
            pacer_->waitSlot();     // the vblank of the next slot of the pace grid: the plane update follows at once (the intervals of the pictures are then even)

        // Publish page guard state (page_id, expected markers, fb, geometry) BEFORE planeSet(fb).
        // Rationale / Sequence analysis:
        // If published AFTER planeSet returns:
        //   SETPLANE displays the new page's buffer. A host commit landing immediately after SETPLANE
        //   but before layer_block_publish reads the host's new markers, compares them against the OLD
        //   page_id still in LayerBlock -> mismatch -> shim injects plane-off for our overlay plane!
        //   This produces a spurious visual blink right as the new page appears.
        // If published BEFORE planeSet:
        //   LayerBlock is updated with new page_id and markers under seqlock before SETPLANE.
        //   - Any host commit landing after SETPLANE sees new host markers matching new page_id -> no blink.
        //   - Any host commit landing between publish and SETPLANE sees old host markers against new
        //     page_id in LayerBlock -> mismatch -> shim injects plane-off. This turns off the OLD picture
        //     on screen, which is already stale and harmless (the new SETPLANE lands immediately after).
        // Hence publishing before SETPLANE produces the fewest blinks and eliminates spurious turn-offs.
        ensureLayerBlock();
        if (layerBlock_)
        {
            const LayerFields lf = makePresentFields (fb);
            layer_block_publish (layerBlock_, &lf);
        }

        const double tCall = nowMs();
        if (! planeSet (fb))
        {
            sendError (std::string ("SETPLANE: ") + std::strerror (errno));
            api_.gbm_surface_release_buffer (gsurf_, bo);
            return false;
        }
        const double t2 = nowMs();
        pacer_->done();
        curFb_ = fb;
        if (t2 - lastVerifyMs_ >= 1000.0 && ! verifyPlane (fb))      // the display says the plane shows our framebuffer on our CRTC (the plugin must not ask: a GETPLANE waits for the lock the update holds)
        {
            api_.gbm_surface_release_buffer (gsurf_, bo);
            return false;
        }
        publishPace();
        if (prevBo_)
            api_.gbm_surface_release_buffer (gsurf_, prevBo_);   // the previous picture is off the screen now (SETPLANE waited for the vblank)
        prevBo_ = bo;
        ++frames_;
        // statistics: a SETPLANE longer than 1.5 vblanks is late; the vblanks beyond the stride between two pictures of a run are skipped ones
        if (t2 - tCall > 1.5 * periodMs_)
            ++late_;
        {
            const PaceStats& ps = pacer_->stats();
            skipped_ = 0;
            for (int i = stride_ + 1; i < PaceStats::kVb; ++i)
                skipped_ += static_cast<unsigned long> (ps.vb[i]) * static_cast<unsigned long> (i - stride_);
        }
#ifdef SYNTHYBOY_OVERLAY_BENCH
        if (benchActive_ && benchTrace_.size() < 120 && nowMs() >= benchWindowStart_)
        {
            synthyboy_mpc::overlay::VblankStamp vs;
            clock_->last (vs);
            benchTrace_.push_back ({ t0, t1, tCall, t2, static_cast<double> (vs.seq) });
        }
        benchRecord (t1 - t0, t2 - tCall, benchLastFrameMs_ > 0.0 ? t2 - benchLastFrameMs_ : 0.0);
        if (benchActive_ && ! benchReported_ && nowMs() >= benchWindowStart_ && benchGen_.size() < 20000)
        {
            benchGen_.push_back (static_cast<float> (tGen - t0));
            benchBuild_.push_back (static_cast<float> (tBuilt - tGen));
            benchSubmit_.push_back (static_cast<float> (tSubmitted - tBuilt));
            benchFinish_.push_back (static_cast<float> (tFinished - tSubmitted));
        }
        benchLastFrameMs_ = t2;
#endif
        lastFrameMs_ = t2;
        renderBudgetMs_ = std::max (0.9 * renderBudgetMs_, t1 - t0);
        send (makeMsg (kMsgFrame, static_cast<uint32_t> (frames_), seq, static_cast<uint32_t> ((t1 - t0) * 1000.0), static_cast<uint32_t> ((t2 - tCall) * 1000.0), static_cast<uint32_t> (skipped_),
                       static_cast<uint32_t> (late_)));
        return true;
    }

    /** Once a second, right after an update (when nothing of ours holds the display's locks): GETPLANE says our CRTC and our framebuffer. Anything else: another program took the plane; the plugin is
        told with an error that starts with SETPLANE (it moves to the next free plane) and this process ends. */
    bool verifyPlane (uint32_t fb)
    {
        lastVerifyMs_ = nowMs();
        drm_mode_get_plane p;
        std::memset (&p, 0, sizeof p);
        p.plane_id = plane_;
        if (ioctl (drmFd_, DRM_IOCTL_MODE_GETPLANE, &p) < 0)
        {
            sendError (std::string ("SETPLANE: GETPLANE of the plane after the update failed: ") + std::strerror (errno));
            return false;
        }
        if (p.crtc_id == 0 && p.fb_id == 0)
        {
            // switched off, not taken (a mode set of the application switches off every plane it does not name): the same framebuffer goes on again
            if (! planeSet (fb))
            {
                sendError (std::string ("SETPLANE: the plane was switched off and cannot be set again: ") + std::strerror (errno));
                return false;
            }
            ++planeRefreshes_;
            ++planeChecks_;
            return true;
        }
        if (p.crtc_id != crtc_ || p.fb_id != fb)
        {
            sendError ("SETPLANE: plane " + std::to_string (plane_) + " reports crtc " + std::to_string (p.crtc_id) + " fb " + std::to_string (p.fb_id) + ", not ours (taken by another program)");
            planeOn_ = false;   // not ours any more: the clean-up must not switch it off
            return false;
        }
        ++planeChecks_;
        return true;
    }

    /** The statistics of one picture into the shared block (relaxed atomic stores; the helper is the only writer; the plugin subtracts two snapshots). */
    void publishRender (double t0, double tGen, double tBuilt, double tSubmitted, double tFinished, double t1, const SceneData& s) noexcept
    {
        RenderBlock& r = block_->render;
        auto us = [] (double ms) { return static_cast<uint32_t> (ms < 0.0 ? 0.0 : ms * 1000.0 + 0.5); };
        auto add = [] (uint32_t& f, uint32_t v) { __atomic_store_n (&f, f + v, __ATOMIC_RELAXED); };
        const uint32_t total = us (t1 - t0);
        add (r.totalUs, total);
        add (r.readUs, us (tGen - t0));
        add (r.buildUs, us (tBuilt - tGen));
        add (r.submitUs, us (tSubmitted - tBuilt));
        add (r.finishUs, us (tFinished - tSubmitted));
        add (r.tailUs, us (t1 - tFinished));
        add (r.ops, s.numOps);
        add (r.cachedOps, lastCached_);
        add (r.redrawn, lastRedrawn_);
        add (r.paintedPx, lastPaintedPx_);
        add (r.fullFrames, lastPaintedPx_ >= static_cast<uint32_t> (bw_ * bh_) ? 1u : 0u);
        add (r.verts, static_cast<uint32_t> (verts_.size()));
        add (r.draws, static_cast<uint32_t> (draws_.size()));
        const uint32_t b = std::min<uint32_t> (63u, total / 1000u);
        add (r.hist[b], 1u);
        add (r.count, 1u);
    }

    /** Diagnostics (a file once): what the display's EGL says (extensions) and the age of the back buffer of the first pictures (EGL_EXT_buffer_age): how many pictures ago the buffer was last drawn. */
    void probeBufferAge()
    {
        if (probed_ >= 24)
            return;
        EGLint age = -1;
        const EGLBoolean ok = api_.eglQuerySurface (dpy_, esurf_, 0x313D /* EGL_BUFFER_AGE_EXT */, &age);
        ages_[probed_++] = ok ? age : -2;
        if (probed_ == 24)
        {
            {
                // one diagnostic file, written once: through the bounded writer (no symlink followed, mode 0644)
                char text[4096];
                const char* ext = api_.eglQueryString (dpy_, 0x3055 /* EGL_EXTENSIONS */);
                int n = std::snprintf (text, sizeof text, "extensions: %s\nbuffer age of pictures 1..24:", ext != nullptr ? ext : "(none)");
                for (int i = 0; i < 24 && n > 0 && static_cast<size_t> (n) < sizeof text - 16; ++i)
                    n += std::snprintf (text + n, sizeof text - static_cast<size_t> (n), " %d", ages_[i]);
                if (n > 0 && static_cast<size_t> (n) < sizeof text - 2)
                    text[n++] = '\n';
#if SYNTHYBOY_MPC_WRITE_FILES   // the release build (write_files.h) writes no probe file
                if (n > 0)
                    synthyboy_mpc::safefile::writeSmall ("/tmp/synthyboy-overlay-gpu-egl.txt", text, static_cast<size_t> (n < static_cast<int> (sizeof text) ? n : static_cast<int> (sizeof text) - 1));
#endif
            }
        }
    }
    int probed_ = 0, ages_[24] = {};

    void publishPace() noexcept
    {
        const PaceStats& ps = pacer_->stats();
        for (int i = 0; i < PaceStats::kVb; ++i)
            __atomic_store_n (&block_->pace.vb[i], ps.vb[i], __ATOMIC_RELAXED);
        for (int i = 0; i < PaceStats::kMs; ++i)
            __atomic_store_n (&block_->pace.ms[i], ps.ms[i], __ATOMIC_RELAXED);
        __atomic_store_n (&block_->pace.count, ps.count, __ATOMIC_RELAXED);
        __atomic_store_n (&block_->pace.planeChecks, planeChecks_, __ATOMIC_RELAXED);
        __atomic_store_n (&block_->pace.planeRefreshes, planeRefreshes_, __ATOMIC_RELAXED);
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
        viewIndex_ = static_cast<int> (m.f);
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
        rot_ = (rot == 0 || rot == 180 || rot == 270) ? rot : 90;
        const bool turned = rot_ == 90 || rot_ == 270;   // a portrait panel: the buffer is the canvas turned; a landscape one: as it is
        bw_ = turned ? h : w;
        bh_ = turned ? w : h;
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

    /** The plugin chose another number of vblanks per picture. 1: the plane update follows the render at once (no slot wait): on a panel whose plane update lands on the next vblank wherever in the period it is called
        that is a picture per vblank; 2 and more: the pace grid. */
    void setStride (int s)
    {
        s = std::clamp (s, 1, 4);
        if (s == stride_ || ! clock_)
            return;
        stride_ = s;
        pacer_ = std::make_unique<FramePacer> (*clock_, periodMs_, stride_, (flags_ & kFlagVblank) != 0);
        __atomic_store_n (&block_->pace.stride, static_cast<uint32_t> (stride_), __ATOMIC_RELAXED);
    }

    void onHide()
    {
        if (shown_)
            teardownSurface();
        dirty_ = false;
        gate_.release();
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
            {
                endReason_ = "the socket closed: the plugin or the application went away";
                return false;
            }
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
                case kMsgStride: setStride (static_cast<int> (m.a)); break;
                case kMsgPageOk: gate_.onPageOk (m.a, static_cast<int> (m.b)); gpu::guardTrace ("helper", "page_ok_recv", static_cast<long> (m.a), static_cast<long> (m.b)); break;
                case kMsgQuit: endReason_ = "quit"; return false;
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
#ifdef SYNTHYBOY_OVERLAY_BENCH
            if (shown_)
            {
                benchPoll();
                if (benchActive_)
                    dirty_ = true;
            }
#endif
            // a scene waits for its slot of the pace grid: the thread sleeps (and reads messages) until the picture can be drawn just in time, then draws, waits for the vblank of the
            // slot (the pacer) and updates the plane at once
            // nothing to draw: the sleep is as long as the duties allow (gpu/guard_state.h LoopTimeout: 10 s at most while a picture is verified, 1 s only for debug files, the bench or a guard flag nobody waits for)
            bool needsPoll = (flags_ & kFlagDump) != 0 || (layerBlock_ != nullptr && guardWatcher_.fd() < 0);
#ifdef SYNTHYBOY_OVERLAY_BENCH
            needsPoll = true;
#endif
            double wait = LoopTimeout::idleWaitMs (nowMs(), lastVerifyMs_, shown_ && planeOn_ && curFb_ != 0 && ! dirty_, needsPoll);
            if (dirty_ && shown_ && ! gate_.blocks (viewIndex_))
            {
                wait = pacer_->msUntilSlot() - renderBudgetMs_ - 1.5;
                if (wait < 0.0)
                    wait = 0.0;
                if (wait > 100.0)
                    wait = 100.0;
                if (wait < 1.0)
                    wait = 0.0;
            }
            pollfd p[2] = { { kSock, POLLIN, 0 }, { guardWatcher_.fd(), POLLIN, 0 } };   // (a negative fd is ignored by poll)
            const int rc = ::poll (p, 2, static_cast<int> (wait));
            if (rc < 0 && errno != EINTR)
                break;
            if (g_stop)
                break;
            const bool wasShown = shown_, wasDirty = dirty_;
            if (rc > 0 && (p[1].revents & POLLIN))
                guardWatcher_.drain();   // the guard flag is read just below, this very pass
            if (rc > 0 && (p[0].revents & (POLLIN | POLLHUP | POLLERR)))
                if (! drain())
                    break;
            {
                // the counters: a timeout is idle unless it had a duty (a picture to draw, the check of a still picture that is due, the debug dump file)
                const bool verifyDue = wasShown && planeOn_ && curFb_ != 0 && ! wasDirty && nowMs() - lastVerifyMs_ >= LoopTimeout::kVerifyMs;
                wakes_.count (rc, rc != 0 || wasDirty || verifyDue || (flags_ & kFlagDump) != 0);
                if (block_)
                {
                    __atomic_store_n (&block_->pace.loopWakes, wakes_.wakes, __ATOMIC_RELAXED);
                    __atomic_store_n (&block_->pace.loopWakesIdle, wakes_.idle, __ATOMIC_RELAXED);
                }
            }
            if (rc == 0 && shown_ && ! dirty_ && (flags_ & kFlagDump) && ::access ("/tmp/synthyboy-overlay-gpu-dump", F_OK) == 0)
                dirty_ = true;     // the debug file asks for the frame on disk: draw the same scene again (once a second at most, maybeDump)
            if (layerBlock_ && sb_lb_load_u32 (&layerBlock_->guard_plane_off, std::memory_order_acquire))
            {
                sb_lb_store_u32 (&layerBlock_->guard_plane_off, 0u, std::memory_order_release);
                gpu::guardTrace ("helper", "guard_flag_read");
                planeOn_ = false;
                curFb_ = 0;
                // the plane is off with the host's flip. It stays off until the plugin's page detector (made after this report) finds one of our views on the current host framebuffer (gpu/guard_state.h):
                // the helper re-presented about 35 ms later over a host screen (the Load dialog) before
                if (shown_)
                {
                    const uint32_t ep = gate_.onGuard();
                    send (makeMsg (kMsgGuard, ep));
                    gpu::guardTrace ("helper", "guard_sent", static_cast<long> (ep));
                }
            }
            // a picture that stays (nothing changes, nothing is updated) is checked too, every LoopTimeout::kVerifyMs (10 s, g134: the loop sleeps that long at rest; the check after an update stays at once a second): another program may have taken the plane since the last update
            if (shown_ && planeOn_ && curFb_ != 0 && ! dirty_ && nowMs() - lastVerifyMs_ >= LoopTimeout::kVerifyMs && ! verifyPlane (curFb_))
                break;
            if (gate_.hidden && shown_ && ! gate_.blocks (viewIndex_))
            {
                dirty_ = true;     // the page is confirmed after a guard plane-off: our plane is off, the picture goes up again
                pacer_->presentNextImmediately();   // ... at once, not at the next stride boundary of the pace grid (g134); the picture after it keeps the grid
            }
            if (dirty_ && shown_ && ! gate_.blocks (viewIndex_))
            {
                if (pacer_->msUntilSlot() - renderBudgetMs_ - 1.5 >= 1.0)
                    continue;      // not yet: sleep on
                const bool wasHeld = gate_.hidden;
                if (renderAndPresent())
                {
                    if (wasHeld)
                        gpu::guardTrace ("helper", "first_present", viewIndex_);
                    dirty_ = false;
                    gate_.release();
                }
                else if (exitCode_ != 0)
                    break;
                else
                    ::usleep (5000);   // the scene could not be read this time (the writer was busy): again in 5 ms
            }
        }
    }

    void cleanupAll()
    {
        guardWatcher_.stop();
        if (shown_ || planeOn_ || esurf_ != EGL_NO_SURFACE || gsurf_)
            teardownSurface();
        if (layerBlock_)
        {
            layer_block_unmap (layerBlock_);
            layerBlock_ = nullptr;
        }
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
    LayerBlock* layerBlock_ = nullptr;
    GuardWatcher guardWatcher_;                  // waits on the guard flag of the layer block (futex) and wakes the loop through an eventfd
    GuardGate gate_;
    LoopWakes wakes_;                            // the loop's wake-ups (shared block: loopWakes, loopWakesIdle)
    double lastLayerBlockProbeMs_ = -10000.0;
    int viewIndex_ = -1;
    EGLDisplay dpy_ = EGL_NO_DISPLAY;
    EGLConfig cfg_ = nullptr;
    EGLContext ctx_ = EGL_NO_CONTEXT;
    EGLSurface esurf_ = EGL_NO_SURFACE;
    GLuint layerTex_[2] = {}, layerFbo_[2] = {};
    LayerCache cache_;                           // gpu/cell_cache.h: what the cached layers hold, and the plan of the partial picture
    PartialPaint partial_;
    int layerW_ = 0, layerH_ = 0;
    bool layersOk_ = true;
    int pictureTarget_ = -1;
    bool frameBoxOn_ = false;
    int frameBox_[4] = {};                       // the box (buffer pixels) of the part of the picture that is painted now
    std::vector<DamageRect> layerDamage_, liveBoxes_;   // what changed in the layers this picture, where the live ops are
    uint32_t lastPaintedPx_ = 0;
    bool noSlot_ = true;                         // stride 1 without the slot wait
    bool partialOk_ = true;                      // switched off by /tmp/synthyboy-overlay-nopartial (diagnostics)   // where the picture of the ops being built goes: -1 the window, 2 / 3 a cached layer
    uint32_t lastCached_ = 0, lastRedrawn_ = 0;
    GLuint prog_[kPrograms] = {}, vbo_ = 0, atlasTex_ = 0, feedbackTex_ = 0, glowTex_[2] = {}, glowFbo_[2] = {};
    int glowW_ = 0, glowH_ = 0, curTarget_ = -2, curTex_ = -1;
    bool sceneFeedback_ = false, prevFeedback_ = false;
    struct Locs { GLint rowX = -1, rowY = -1, kind = -1, c0 = -1, c1 = -1, half = -1, r = -1, u = -1, soft = -1, e = -1, a = -1, len = -1, textK = -1, rr = -1, texel = -1, pm = -1, p0 = -1, p1 = -1, p2 = -1, p3 = -1, tex = -1; } u_;
    Locs locs_[kPrograms];
    int curProg_ = -1;
    Draw last_;
    bool haveLast_ = false;
    Import imports_[kMaxLedger];
    std::vector<Vtx> verts_;
    std::vector<Draw> draws_;
    std::vector<Pt> pts_, bandA_, bandB_;
    StyleState style_;
    PaintState paint_;
    int glowDivisor_ = 4, buildTarget_ = -1, wantSamples_ = 0, clipBox_[4] = {};
    bool clipOn_ = false;
    SceneData scene_ {};
    bool shown_ = false, dirty_ = false, planeOn_ = false;
    int w_ = 0, h_ = 0, bw_ = 0, bh_ = 0, panelX_ = 0, panelY_ = 0, rot_ = 90, exitCode_ = 0;
    char viewName_[32] = {};
    const char* endReason_ = "loop ended";
    unsigned long frames_ = 0, late_ = 0, skipped_ = 0;
    int pipe_ = 0, stride_ = 2;
    uint32_t planeChecks_ = 0, curFb_ = 0, planeRefreshes_ = 0;
    double lastVerifyMs_ = 0.0;
    double periodMs_ = 16.67, renderBudgetMs_ = 4.0;
    std::string renderNode_;
    std::unique_ptr<LinuxVblankClock> clock_;
    std::unique_ptr<FramePacer> pacer_;
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
    int rc;
    {
        Helper h;
        rc = h.run();
    }
    // the plugin raises the lowest clock of the GPU while the picture moves (gpu_clock.h) and keeps the level it replaced in a file; when the plugin died raised (the socket ended), this puts it back
    {
        synthyboy_mpc::overlay::GpuClock leftover;
        leftover.open();
    }
    return rc;
}
