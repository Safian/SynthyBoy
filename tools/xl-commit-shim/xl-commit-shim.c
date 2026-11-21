// SynthyBoy - MPC XL atomic commit measurement shim.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#define _GNU_SOURCE
#include "xl-commit-shim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>

#ifdef __linux__
#include <sys/syscall.h>
#endif

#ifndef SHIM_TEST
#include <dlfcn.h>
#include <sys/ioctl.h>
#endif

#define MAX_TRACKED_REQS_PER_THREAD 8
#define MAX_SUMMARY_DURATIONS       4096
#define MAX_SUMMARY_TIDS            32
#define MAX_LOG_SIZE                (4 * 1024 * 1024)

// Real symbol function pointers
static drmModeAtomicReqPtr (*real_drmModeAtomicAlloc)(void) = NULL;
static void (*real_drmModeAtomicFree)(drmModeAtomicReqPtr) = NULL;
static int (*real_drmModeAtomicAddProperty)(drmModeAtomicReqPtr, uint32_t, uint32_t, uint64_t) = NULL;
static int (*real_drmModeAtomicGetCursor)(drmModeAtomicReqPtr) = NULL;
static int (*real_drmModeAtomicCommit)(int, drmModeAtomicReqPtr, uint32_t, void *) = NULL;

static drmModeObjectPropertiesPtr (*real_drmModeObjectGetProperties)(int, uint32_t, uint32_t) = NULL;
static void (*real_drmModeFreeObjectProperties)(drmModeObjectPropertiesPtr) = NULL;
static drmModePropertyPtr (*real_drmModeGetProperty)(int, uint32_t) = NULL;
static void (*real_drmModeFreeProperty)(drmModePropertyPtr) = NULL;
static int (*real_drmModeAddFB2)(int, uint32_t, uint32_t, uint32_t,
                                const uint32_t[4], const uint32_t[4], const uint32_t[4],
                                uint32_t *, uint32_t) = NULL;
static int (*real_drmModeRmFB)(int, uint32_t) = NULL;
static int (*real_drmIoctl)(int, unsigned long, void *) = NULL;

#ifdef SHIM_TEST
extern drmModeAtomicReqPtr fake_drmModeAtomicAlloc(void);
extern void fake_drmModeAtomicFree(drmModeAtomicReqPtr req);
extern int fake_drmModeAtomicAddProperty(drmModeAtomicReqPtr req, uint32_t object_id, uint32_t property_id, uint64_t value);
extern int fake_drmModeAtomicGetCursor(drmModeAtomicReqPtr req);
extern int fake_drmModeAtomicCommit(int fd, drmModeAtomicReqPtr req, uint32_t flags, void *user_data);

extern drmModeObjectPropertiesPtr fake_drmModeObjectGetProperties(int fd, uint32_t object_id, uint32_t object_type);
extern void fake_drmModeFreeObjectProperties(drmModeObjectPropertiesPtr ptr);
extern drmModePropertyPtr fake_drmModeGetProperty(int fd, uint32_t propertyId);
extern void fake_drmModeFreeProperty(drmModePropertyPtr ptr);
extern int fake_drmModeAddFB2(int fd, uint32_t width, uint32_t height, uint32_t pixel_format,
                             const uint32_t bo_handles[4], const uint32_t pitches[4],
                             const uint32_t offsets[4], uint32_t *buf_id, uint32_t flags);
extern int fake_drmModeRmFB(int fd, uint32_t bufferId);
extern int fake_drmIoctl(int fd, unsigned long request, void *arg);
extern void *fake_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
extern int fake_munmap(void *addr, size_t length);

#define shim_mmap fake_mmap
#define shim_munmap fake_munmap
#else
#include <sys/mman.h>
#define shim_mmap mmap
#define shim_munmap munmap
#endif

static void init_real_symbols(void) {
#ifdef SHIM_TEST
    real_drmModeAtomicAlloc = fake_drmModeAtomicAlloc;
    real_drmModeAtomicFree = fake_drmModeAtomicFree;
    real_drmModeAtomicAddProperty = fake_drmModeAtomicAddProperty;
    real_drmModeAtomicGetCursor = fake_drmModeAtomicGetCursor;
    real_drmModeAtomicCommit = fake_drmModeAtomicCommit;
    real_drmModeObjectGetProperties = fake_drmModeObjectGetProperties;
    real_drmModeFreeObjectProperties = fake_drmModeFreeObjectProperties;
    real_drmModeGetProperty = fake_drmModeGetProperty;
    real_drmModeFreeProperty = fake_drmModeFreeProperty;
    real_drmModeAddFB2 = fake_drmModeAddFB2;
    real_drmModeRmFB = fake_drmModeRmFB;
    real_drmIoctl = fake_drmIoctl;
#else
    if (real_drmModeAtomicCommit) return;
    real_drmModeAtomicAlloc = (drmModeAtomicReqPtr (*)(void))dlsym(RTLD_NEXT, "drmModeAtomicAlloc");
    real_drmModeAtomicFree = (void (*)(drmModeAtomicReqPtr))dlsym(RTLD_NEXT, "drmModeAtomicFree");
    real_drmModeAtomicAddProperty = (int (*)(drmModeAtomicReqPtr, uint32_t, uint32_t, uint64_t))dlsym(RTLD_NEXT, "drmModeAtomicAddProperty");
    real_drmModeAtomicGetCursor = (int (*)(drmModeAtomicReqPtr))dlsym(RTLD_NEXT, "drmModeAtomicGetCursor");
    real_drmModeAtomicCommit = (int (*)(int, drmModeAtomicReqPtr, uint32_t, void *))dlsym(RTLD_NEXT, "drmModeAtomicCommit");

    real_drmModeObjectGetProperties = (drmModeObjectPropertiesPtr (*)(int, uint32_t, uint32_t))dlsym(RTLD_NEXT, "drmModeObjectGetProperties");
    real_drmModeFreeObjectProperties = (void (*)(drmModeObjectPropertiesPtr))dlsym(RTLD_NEXT, "drmModeFreeObjectProperties");
    real_drmModeGetProperty = (drmModePropertyPtr (*)(int, uint32_t))dlsym(RTLD_NEXT, "drmModeGetProperty");
    real_drmModeFreeProperty = (void (*)(drmModePropertyPtr))dlsym(RTLD_NEXT, "drmModeFreeProperty");
    real_drmModeAddFB2 = (int (*)(int, uint32_t, uint32_t, uint32_t, const uint32_t[4], const uint32_t[4], const uint32_t[4], uint32_t *, uint32_t))dlsym(RTLD_NEXT, "drmModeAddFB2");
    real_drmModeRmFB = (int (*)(int, uint32_t))dlsym(RTLD_NEXT, "drmModeRmFB");
    real_drmIoctl = (int (*)(int, unsigned long, void *))dlsym(RTLD_NEXT, "drmIoctl");
    if (!real_drmIoctl) {
        real_drmIoctl = (int (*)(int, unsigned long, void *))dlsym(RTLD_NEXT, "ioctl");
    }
#endif
}

// Portable monotonic time in ns
static inline uint64_t shim_monotonic_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

// Portable thread id
static inline pid_t shim_gettid(void) {
#ifdef __linux__
#ifdef SYS_gettid
    return (pid_t)syscall(SYS_gettid);
#else
    return gettid();
#endif
#elif defined(__APPLE__)
    uint64_t tid = 0;
    pthread_threadid_np(NULL, &tid);
    return (pid_t)tid;
#else
    return (pid_t)(uintptr_t)pthread_self();
#endif
}

// Thread-local request tracking (mutex-free)
typedef struct {
    void *req;
    uint32_t count;
    ShimPropItem items[MAX_ITEMS_PER_REQ];
} ThreadReqTrack;

static __thread ThreadReqTrack tl_requests[MAX_TRACKED_REQS_PER_THREAD];

// Ring buffer structures
typedef struct {
    _Atomic uint64_t seq;
    RingEntry entry;
} RingSlot;

static RingSlot g_ring[RING_BUFFER_SIZE];
static _Atomic uint64_t g_ring_head = 0;
static _Atomic uint64_t g_ring_tail = 0;

static _Atomic uint64_t g_call_site_counts[CALL_SITE_COUNT] = {0, 0, 0};
static _Atomic bool g_stage2_enabled = false;
static _Atomic uint64_t g_last_stage2_commit_ns = 0;
static _Atomic bool g_stage3_enabled = false;
static _Atomic bool g_stage3_failed = false;
static _Atomic bool g_plane46_active = false;
static _Atomic uint32_t g_stage3_fallbacks = 0;
static _Atomic uint32_t g_stage3_skipped_merges = 0;

static _Atomic bool g_logger_started = false;
static _Atomic bool g_logger_stop = false;
static pthread_mutex_t g_start_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_start_cond  = PTHREAD_COND_INITIALIZER;
static pthread_t       g_logger_thread;

// Plane properties cache for Stage 2 & Stage 3
typedef struct {
    uint32_t fb_id;
    uint32_t crtc_id;
    uint32_t crtc_x;
    uint32_t crtc_y;
    uint32_t crtc_w;
    uint32_t crtc_h;
    uint32_t src_x;
    uint32_t src_y;
    uint32_t src_w;
    uint32_t src_h;
    bool resolved;
} PlanePropIds;

static PlanePropIds g_plane46 = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, false};
static uint32_t g_test_fb_id = 0;
static uint32_t g_test_dumb_handle = 0;
static uint64_t g_test_dumb_size = 0;
static bool g_props_logged = false;
static uint32_t g_prev_frame_item_count = 0;
static bool g_has_prev_frame = false;

// 5-second summary stats aggregator
typedef struct {
    pid_t tid;
    uint32_t count;
} TidCount;

typedef struct {
    uint32_t site_commits[CALL_SITE_COUNT];
    uint32_t merged_commits;
    uint32_t ret_ok;
    uint32_t ret_err;
    uint32_t duration_count;
    uint64_t durations[MAX_SUMMARY_DURATIONS];
    uint32_t merged_duration_count;
    uint64_t merged_durations[MAX_SUMMARY_DURATIONS];
    uint32_t fallbacks;
    uint32_t num_tids;
    TidCount tids[MAX_SUMMARY_TIDS];
} SummaryStats;

static SummaryStats g_summary;

// Ring buffer producer: reserve slot
static RingEntry *ring_reserve(uint64_t *out_seq) {
    uint64_t head = atomic_load_explicit(&g_ring_head, memory_order_relaxed);
    while (1) {
        uint64_t tail = atomic_load_explicit(&g_ring_tail, memory_order_acquire);
        if (head - tail >= RING_BUFFER_SIZE) {
            // Ring full: silently drop
            return NULL;
        }
        if (atomic_compare_exchange_weak_explicit(&g_ring_head, &head, head + 1,
                                                  memory_order_acq_rel, memory_order_relaxed)) {
            *out_seq = head + 1;
            return &g_ring[head & RING_BUFFER_MASK].entry;
        }
    }
}

static void ring_commit(uint64_t seq) {
    atomic_store_explicit(&g_ring[(seq - 1) & RING_BUFFER_MASK].seq, seq, memory_order_release);
}

static void ring_enqueue_commit(uint64_t time_ns, pid_t tid, uint32_t flags, uint32_t item_count,
                               bool has_user_data, int ret, int err, int call_site,
                               uint64_t duration_ns, bool is_merged, bool is_fallback,
                               bool include_items, const ShimPropItem *items, uint32_t num_items) {
    uint64_t seq = 0;
    RingEntry *e = ring_reserve(&seq);
    if (!e) return;

    e->type = ENTRY_COMMIT;
    e->time_ns = time_ns;
    e->tid = tid;
    e->flags = flags;
    e->item_count = item_count;
    e->has_user_data = has_user_data;
    e->ret = ret;
    e->err = err;
    e->call_site = call_site;
    e->duration_ns = duration_ns;
    e->is_merged = is_merged;
    e->is_fallback = is_fallback;
    e->has_items = include_items;
    e->num_items = 0;
    e->msg[0] = '\0';

    if (include_items && items && num_items > 0) {
        uint32_t n = num_items;
        if (n > RING_ENTRY_MAX_ITEMS) n = RING_ENTRY_MAX_ITEMS;
        e->num_items = n;
        memcpy(e->items, items, n * sizeof(ShimPropItem));
    }

    ring_commit(seq);
}

static void ring_enqueue_stage2(uint64_t time_ns, pid_t tid, int ret, int err, uint64_t duration_ns) {
    uint64_t seq = 0;
    RingEntry *e = ring_reserve(&seq);
    if (!e) return;

    e->type = ENTRY_STAGE2;
    e->time_ns = time_ns;
    e->tid = tid;
    e->flags = DRM_MODE_ATOMIC_TEST_ONLY;
    e->item_count = 0;
    e->has_user_data = false;
    e->ret = ret;
    e->err = err;
    e->call_site = CALL_SITE_FRAME;
    e->duration_ns = duration_ns;
    e->is_merged = false;
    e->is_fallback = false;
    e->has_items = false;
    e->num_items = 0;
    e->msg[0] = '\0';

    ring_commit(seq);
}

static void ring_enqueue_msg(const char *msg) {
    uint64_t seq = 0;
    RingEntry *e = ring_reserve(&seq);
    if (!e) return;

    e->type = ENTRY_MSG;
    e->time_ns = shim_monotonic_ns();
    e->tid = shim_gettid();
    e->flags = 0;
    e->item_count = 0;
    e->has_user_data = false;
    e->ret = 0;
    e->err = 0;
    e->call_site = CALL_SITE_OTHER;
    e->duration_ns = 0;
    e->is_merged = false;
    e->is_fallback = false;
    e->has_items = false;
    e->num_items = 0;
    strncpy(e->msg, msg, sizeof(e->msg) - 1);
    e->msg[sizeof(e->msg) - 1] = '\0';

    ring_commit(seq);
}

static void ensure_logger_started(void) {
    if (!atomic_load_explicit(&g_logger_started, memory_order_relaxed)) {
        atomic_store_explicit(&g_logger_started, true, memory_order_release);
        pthread_mutex_lock(&g_start_mutex);
        pthread_cond_signal(&g_start_cond);
        pthread_mutex_unlock(&g_start_mutex);
    }
}

// Stage 2 setup & helpers
static void log_plane_props_once(int fd, uint32_t plane_id, PlanePropIds *out_props) {
    if (!real_drmModeObjectGetProperties || !real_drmModeGetProperty ||
        !real_drmModeFreeProperty || !real_drmModeFreeObjectProperties) {
        return;
    }
    drmModeObjectPropertiesPtr props = real_drmModeObjectGetProperties(fd, plane_id, DRM_MODE_OBJECT_PLANE);
    if (!props) return;

    char msg[256];
    int pos = snprintf(msg, sizeof(msg), "[STAGE2] Plane %u props:", plane_id);

    for (uint32_t i = 0; i < props->count_props; i++) {
        drmModePropertyPtr p = real_drmModeGetProperty(fd, props->props[i]);
        if (p) {
            if (out_props) {
                if (strcmp(p->name, "FB_ID") == 0) out_props->fb_id = p->prop_id;
                else if (strcmp(p->name, "CRTC_ID") == 0) out_props->crtc_id = p->prop_id;
                else if (strcmp(p->name, "CRTC_X") == 0) out_props->crtc_x = p->prop_id;
                else if (strcmp(p->name, "CRTC_Y") == 0) out_props->crtc_y = p->prop_id;
                else if (strcmp(p->name, "CRTC_W") == 0) out_props->crtc_w = p->prop_id;
                else if (strcmp(p->name, "CRTC_H") == 0) out_props->crtc_h = p->prop_id;
                else if (strcmp(p->name, "SRC_X") == 0) out_props->src_x = p->prop_id;
                else if (strcmp(p->name, "SRC_Y") == 0) out_props->src_y = p->prop_id;
                else if (strcmp(p->name, "SRC_W") == 0) out_props->src_w = p->prop_id;
                else if (strcmp(p->name, "SRC_H") == 0) out_props->src_h = p->prop_id;
            }
            if (pos < (int)sizeof(msg) - 24) {
                pos += snprintf(msg + pos, sizeof(msg) - pos, " %s=%u", p->name, p->prop_id);
            }
            real_drmModeFreeProperty(p);
        }
    }
    real_drmModeFreeObjectProperties(props);
    ring_enqueue_msg(msg);
}

#define TEST_BUF_W 400
#define TEST_BUF_H 300
#define COLOR_WHITE   0x00FFFFFF
#define COLOR_MID_RED 0x00C03030

static void fill_test_pattern(void *map_addr, uint32_t width, uint32_t height, uint32_t pitch) {
    if (!map_addr) return;
    for (uint32_t y = 0; y < height; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)map_addr + y * pitch);
        for (uint32_t x = 0; x < width; x++) {
            if (x < 16 || x >= width - 16 || y < 16 || y >= height - 16) {
                // 16 px border in white
                row[x] = COLOR_WHITE;
            } else {
                // Diagonal stripe from top-left to bottom-right so orientation is visible
                int diag_y = (int)((x * height) / width);
                int diff = (int)y - diag_y;
                if (diff < 0) diff = -diff;
                if (diff <= 8) {
                    row[x] = COLOR_WHITE;
                } else {
                    row[x] = COLOR_MID_RED;
                }
            }
        }
    }
}

static void ensure_test_fb(int fd) {
    if (g_test_fb_id != 0) return;
    if (!real_drmIoctl || !real_drmModeAddFB2) return;

    struct drm_mode_create_dumb create;
    memset(&create, 0, sizeof(create));
    create.width = TEST_BUF_W;
    create.height = TEST_BUF_H;
    create.bpp = 32;
    create.flags = 0;

    int rc = real_drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create);
    if (rc != 0 || create.handle == 0) return;

    // Map and fill visible test pattern once on host thread
    struct drm_mode_map_dumb map_req;
    memset(&map_req, 0, sizeof(map_req));
    map_req.handle = create.handle;
    rc = real_drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map_req);
    if (rc == 0) {
        void *addr = shim_mmap(NULL, create.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, map_req.offset);
        if (addr && addr != MAP_FAILED) {
            fill_test_pattern(addr, create.width, create.height, create.pitch);
            shim_munmap(addr, create.size);
        }
    }

    uint32_t bo_handles[4] = { create.handle, 0, 0, 0 };
    uint32_t pitches[4] = { create.pitch, 0, 0, 0 };
    uint32_t offsets[4] = { 0, 0, 0, 0 };
    uint32_t fb_id = 0;
    rc = real_drmModeAddFB2(fd, TEST_BUF_W, TEST_BUF_H, DRM_FORMAT_XRGB8888,
                            bo_handles, pitches, offsets, &fb_id, 0);
    if (rc == 0 && fb_id != 0) {
        g_test_fb_id = fb_id;
        g_test_dumb_handle = create.handle;
        g_test_dumb_size = create.size;
        char msg[128];
        snprintf(msg, sizeof(msg), "[STAGE2] Created dumb FB %ux%u id=%u handle=%u pitch=%u",
                 TEST_BUF_W, TEST_BUF_H, fb_id, create.handle, create.pitch);
        ring_enqueue_msg(msg);
    }
}

static void destroy_test_fb(int fd) {
    if (g_test_fb_id != 0) {
        if (real_drmModeRmFB) {
            real_drmModeRmFB(fd, g_test_fb_id);
        }
        if (g_test_dumb_handle != 0 && real_drmIoctl) {
            struct drm_mode_destroy_dumb destroy;
            memset(&destroy, 0, sizeof(destroy));
            destroy.handle = g_test_dumb_handle;
            real_drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
        }
        char msg[128];
        snprintf(msg, sizeof(msg), "[STAGE3] Freed test FB id=%u dumb handle=%u",
                 g_test_fb_id, g_test_dumb_handle);
        ring_enqueue_msg(msg);
        g_test_fb_id = 0;
        g_test_dumb_handle = 0;
        g_test_dumb_size = 0;
    }
}

static void ensure_stage_initialized(int fd) {
    if (!g_props_logged) {
        g_props_logged = true;
        log_plane_props_once(fd, 46, &g_plane46);
        log_plane_props_once(fd, 34, NULL);
    }
    ensure_test_fb(fd);
}

static void ensure_stage2_initialized(int fd) {
    ensure_stage_initialized(fd);
}

static void run_stage2_probe_if_due(int fd, const ShimPropItem *host_items, uint32_t host_item_count) {
    uint64_t now_ns = shim_monotonic_ns();
    uint64_t last_ns = atomic_load_explicit(&g_last_stage2_commit_ns, memory_order_relaxed);
    if (now_ns - last_ns < 1000000000ULL) {
        return; // Rate limited to at most once per second
    }
    if (!atomic_compare_exchange_strong_explicit(&g_last_stage2_commit_ns, &last_ns, now_ns,
                                                memory_order_acq_rel, memory_order_relaxed)) {
        return;
    }

    ensure_stage2_initialized(fd);

    if (!real_drmModeAtomicAlloc || !real_drmModeAtomicFree || !real_drmModeAtomicAddProperty) {
        return;
    }

    void *req2 = real_drmModeAtomicAlloc();
    if (!req2) return;

    // 1. Copy host items from request (a)
    if (host_items) {
        for (uint32_t i = 0; i < host_item_count; i++) {
            real_drmModeAtomicAddProperty(req2, host_items[i].object_id,
                                          host_items[i].property_id,
                                          host_items[i].value);
        }
    }

    // 2. Add plane 46 properties (crop 400x300 at CRTC 200,200; CRTC_ID 82; SRC in 16.16)
    if (g_plane46.fb_id)   real_drmModeAtomicAddProperty(req2, 46, g_plane46.fb_id, g_test_fb_id);
    if (g_plane46.crtc_id) real_drmModeAtomicAddProperty(req2, 46, g_plane46.crtc_id, 82);
    if (g_plane46.crtc_x)  real_drmModeAtomicAddProperty(req2, 46, g_plane46.crtc_x, 200);
    if (g_plane46.crtc_y)  real_drmModeAtomicAddProperty(req2, 46, g_plane46.crtc_y, 200);
    if (g_plane46.crtc_w)  real_drmModeAtomicAddProperty(req2, 46, g_plane46.crtc_w, 400);
    if (g_plane46.crtc_h)  real_drmModeAtomicAddProperty(req2, 46, g_plane46.crtc_h, 300);
    if (g_plane46.src_x)   real_drmModeAtomicAddProperty(req2, 46, g_plane46.src_x, 0);
    if (g_plane46.src_y)   real_drmModeAtomicAddProperty(req2, 46, g_plane46.src_y, 0);
    if (g_plane46.src_w)   real_drmModeAtomicAddProperty(req2, 46, g_plane46.src_w, (uint64_t)400 << 16);
    if (g_plane46.src_h)   real_drmModeAtomicAddProperty(req2, 46, g_plane46.src_h, (uint64_t)300 << 16);

    // 3. Commit with DRM_MODE_ATOMIC_TEST_ONLY, NULL user_data
    uint64_t t0 = shim_monotonic_ns();
    int ret = real_drmModeAtomicCommit(fd, req2, DRM_MODE_ATOMIC_TEST_ONLY, NULL);
    uint64_t t1 = shim_monotonic_ns();
    int err = (ret < 0) ? errno : 0;
    uint64_t dur_ns = t1 - t0;

    real_drmModeAtomicFree(req2);

    ring_enqueue_stage2(t0, shim_gettid(), ret, err, dur_ns);
}

// Intercepted public functions
drmModeAtomicReqPtr drmModeAtomicAlloc(void) {
    init_real_symbols();
    void *req = real_drmModeAtomicAlloc ? real_drmModeAtomicAlloc() : NULL;
    if (req) {
        for (int i = 0; i < MAX_TRACKED_REQS_PER_THREAD; i++) {
            if (tl_requests[i].req == NULL || tl_requests[i].req == req) {
                tl_requests[i].req = req;
                tl_requests[i].count = 0;
                break;
            }
        }
    }
    return req;
}

void drmModeAtomicFree(drmModeAtomicReqPtr req) {
    init_real_symbols();
    if (req) {
        for (int i = 0; i < MAX_TRACKED_REQS_PER_THREAD; i++) {
            if (tl_requests[i].req == req) {
                tl_requests[i].req = NULL;
                tl_requests[i].count = 0;
                break;
            }
        }
        if (real_drmModeAtomicFree) {
            real_drmModeAtomicFree(req);
        }
    }
}

int drmModeAtomicAddProperty(drmModeAtomicReqPtr req, uint32_t object_id, uint32_t property_id, uint64_t value) {
    init_real_symbols();
    if (req) {
        ThreadReqTrack *track = NULL;
        for (int i = 0; i < MAX_TRACKED_REQS_PER_THREAD; i++) {
            if (tl_requests[i].req == req) {
                track = &tl_requests[i];
                break;
            }
        }
        if (!track) {
            for (int i = 0; i < MAX_TRACKED_REQS_PER_THREAD; i++) {
                if (tl_requests[i].req == NULL) {
                    track = &tl_requests[i];
                    track->req = req;
                    track->count = 0;
                    break;
                }
            }
        }
        if (track) {
            if (track->count < MAX_ITEMS_PER_REQ) {
                track->items[track->count].object_id = object_id;
                track->items[track->count].property_id = property_id;
                track->items[track->count].value = value;
            }
            track->count++;
        }
    }
    if (real_drmModeAtomicAddProperty) {
        return real_drmModeAtomicAddProperty(req, object_id, property_id, value);
    }
    return -1;
}

int drmModeAtomicGetCursor(drmModeAtomicReqPtr req) {
    init_real_symbols();
    if (real_drmModeAtomicGetCursor) {
        return real_drmModeAtomicGetCursor(req);
    }
    if (req) {
        for (int i = 0; i < MAX_TRACKED_REQS_PER_THREAD; i++) {
            if (tl_requests[i].req == req) {
                return (int)tl_requests[i].count;
            }
        }
    }
    return 0;
}

int drmModeAtomicCommit(int fd, drmModeAtomicReqPtr req, uint32_t flags, void *user_data) {
    init_real_symbols();
    if (!real_drmModeAtomicCommit) {
        return -1;
    }

    // 1. Find recorded properties on current thread
    ThreadReqTrack *track = NULL;
    if (req) {
        for (int i = 0; i < MAX_TRACKED_REQS_PER_THREAD; i++) {
            if (tl_requests[i].req == req) {
                track = &tl_requests[i];
                break;
            }
        }
    }
    uint32_t recorded_count = track ? track->count : 0;
    int cursor = -1;
    if (real_drmModeAtomicGetCursor && req) {
        cursor = real_drmModeAtomicGetCursor(req);
    }
    uint32_t item_count = (cursor >= 0) ? (uint32_t)cursor : recorded_count;

    // 2. Classify call site
    int call_site = CALL_SITE_OTHER;
    if (flags & DRM_MODE_PAGE_FLIP_EVENT) {
        call_site = CALL_SITE_FRAME; // (a) frame present
    } else if ((flags & DRM_MODE_ATOMIC_NONBLOCK) && (user_data == NULL)) {
        call_site = CALL_SITE_CURSOR; // (b) cursor move
    }

    // 3. Commit count for this call site & item count change detection
    uint64_t commit_num = atomic_fetch_add_explicit(&g_call_site_counts[call_site], 1, memory_order_relaxed);
    bool item_count_changed = false;
    if (call_site == CALL_SITE_FRAME) {
        uint32_t host_frame_items = (track ? track->count : item_count);
        if (!g_has_prev_frame || g_prev_frame_item_count != host_frame_items) {
            item_count_changed = true;
            g_prev_frame_item_count = host_frame_items;
            g_has_prev_frame = true;
        }
    }
    bool include_items = (commit_num < 8) || (commit_num % 600 == 0) || item_count_changed;

    // 4. Stage 3 live merge decision
    bool s3_active = atomic_load_explicit(&g_stage3_enabled, memory_order_acquire);
    bool s3_plane_active = atomic_load_explicit(&g_plane46_active, memory_order_acquire);
    bool need_disable = (!s3_active && s3_plane_active);

    bool do_merge = false;
    // A host commit that is itself TEST_ONLY applies nothing: never merge into it (the plane-active flag would claim a plane that was not set,
    // and the disable path would free a framebuffer that is still scanned out).
    if (call_site == CALL_SITE_FRAME && !(flags & DRM_MODE_ATOMIC_TEST_ONLY) && (s3_active || need_disable)) {
        bool complete = (track != NULL) && (cursor >= 0 ? ((uint32_t)cursor == recorded_count) : true);
        if (item_count <= 64 && recorded_count <= 64 && complete) {
            ensure_stage_initialized(fd);
            if (need_disable || g_test_fb_id != 0) {
                do_merge = true;
            }
        } else {
            atomic_fetch_add_explicit(&g_stage3_skipped_merges, 1, memory_order_relaxed);
        }
    }

    if (do_merge) {
        static __thread ShimPropItem tl_merged_items[128];
        uint32_t merged_item_count = 0;

        // Copy host items
        for (uint32_t i = 0; i < recorded_count && merged_item_count < 128; i++) {
            tl_merged_items[merged_item_count++] = track->items[i];
        }

        if (s3_active) {
            #define ADD_MERGED(prop, val) do { \
                if ((prop) && merged_item_count < 128) { \
                    tl_merged_items[merged_item_count].object_id = 46; \
                    tl_merged_items[merged_item_count].property_id = (prop); \
                    tl_merged_items[merged_item_count].value = (val); \
                    merged_item_count++; \
                } \
            } while(0)

            ADD_MERGED(g_plane46.fb_id, g_test_fb_id);
            ADD_MERGED(g_plane46.crtc_id, 82);
            ADD_MERGED(g_plane46.crtc_x, 200);
            ADD_MERGED(g_plane46.crtc_y, 200);
            ADD_MERGED(g_plane46.crtc_w, 400);
            ADD_MERGED(g_plane46.crtc_h, 300);
            ADD_MERGED(g_plane46.src_x, 0);
            ADD_MERGED(g_plane46.src_y, 0);
            ADD_MERGED(g_plane46.src_w, (uint64_t)400 << 16);
            ADD_MERGED(g_plane46.src_h, (uint64_t)300 << 16);
            #undef ADD_MERGED
        } else if (need_disable) {
            if (g_plane46.fb_id && merged_item_count < 128) {
                tl_merged_items[merged_item_count].object_id = 46;
                tl_merged_items[merged_item_count].property_id = g_plane46.fb_id;
                tl_merged_items[merged_item_count].value = 0;
                merged_item_count++;
            }
            if (g_plane46.crtc_id && merged_item_count < 128) {
                tl_merged_items[merged_item_count].object_id = 46;
                tl_merged_items[merged_item_count].property_id = g_plane46.crtc_id;
                tl_merged_items[merged_item_count].value = 0;
                merged_item_count++;
            }
        }

        void *req_merged = real_drmModeAtomicAlloc ? real_drmModeAtomicAlloc() : NULL;
        if (req_merged) {
            for (uint32_t i = 0; i < merged_item_count; i++) {
                real_drmModeAtomicAddProperty(req_merged,
                                              tl_merged_items[i].object_id,
                                              tl_merged_items[i].property_id,
                                              tl_merged_items[i].value);
            }

            pid_t tid = shim_gettid();
            uint64_t t_start = shim_monotonic_ns();
            int ret = real_drmModeAtomicCommit(fd, req_merged, flags, user_data);
            uint64_t t_end = shim_monotonic_ns();
            int err = (ret < 0) ? errno : 0;
            uint64_t duration_ns = t_end - t_start;

            real_drmModeAtomicFree(req_merged);

            if (ret < 0) {
                // Merged commit failed! Fallback IMMEDIATELY to original host request
                atomic_store_explicit(&g_stage3_enabled, false, memory_order_release);
                atomic_store_explicit(&g_stage3_failed, true, memory_order_release);
                unlink("/tmp/xl-commit-shim-stage3");
                atomic_fetch_add_explicit(&g_stage3_fallbacks, 1, memory_order_relaxed);

                char fallback_msg[128];
                snprintf(fallback_msg, sizeof(fallback_msg),
                         "[STAGE3] Merged commit failed (ret=%d, errno=%d), stage 3 disabled, fallback to original host request",
                         ret, err);
                ring_enqueue_msg(fallback_msg);

                uint64_t orig_t_start = shim_monotonic_ns();
                ret = real_drmModeAtomicCommit(fd, req, flags, user_data);
                uint64_t orig_t_end = shim_monotonic_ns();
                err = (ret < 0) ? errno : 0;
                duration_ns = orig_t_end - orig_t_start;

                ring_enqueue_commit(orig_t_start, tid, flags, item_count, user_data != NULL,
                                    ret, err, call_site, duration_ns, false, true,
                                    include_items, track ? track->items : NULL, recorded_count);
                ensure_logger_started();
                if (ret < 0) errno = err;
                return ret;
            }

            // Merged commit succeeded!
            if (s3_active) {
                atomic_store_explicit(&g_plane46_active, true, memory_order_release);
            } else if (need_disable) {
                atomic_store_explicit(&g_plane46_active, false, memory_order_release);
                ring_enqueue_msg("[STAGE3] Plane 46 disabled");
                if (!atomic_load_explicit(&g_stage2_enabled, memory_order_acquire) &&
                    !atomic_load_explicit(&g_stage3_enabled, memory_order_acquire)) {
                    destroy_test_fb(fd);
                }
            }

            ring_enqueue_commit(t_start, tid, flags, merged_item_count, user_data != NULL,
                                ret, err, call_site, duration_ns, true, false,
                                include_items, tl_merged_items, merged_item_count);
            ensure_logger_started();
            return ret;
        }
    }

    // Normal (non-merged) commit
    pid_t tid = shim_gettid();
    uint64_t t_start = shim_monotonic_ns();
    int ret = real_drmModeAtomicCommit(fd, req, flags, user_data);
    uint64_t t_end = shim_monotonic_ns();
    int err = (ret < 0) ? errno : 0;
    uint64_t duration_ns = t_end - t_start;

    // If both stage 2 & stage 3 are off and plane 46 is not active, destroy test buffer
    if (!atomic_load_explicit(&g_stage2_enabled, memory_order_acquire) &&
        !atomic_load_explicit(&g_stage3_enabled, memory_order_acquire) &&
        !atomic_load_explicit(&g_plane46_active, memory_order_acquire) &&
        g_test_fb_id != 0) {
        destroy_test_fb(fd);
    }

    ring_enqueue_commit(t_start, tid, flags, item_count, user_data != NULL,
                        ret, err, call_site, duration_ns, false, false,
                        include_items, track ? track->items : NULL, recorded_count);

    ensure_logger_started();

    // Stage 2 TEST_ONLY probe (if enabled and frame present)
    if (call_site == CALL_SITE_FRAME && atomic_load_explicit(&g_stage2_enabled, memory_order_acquire)) {
        run_stage2_probe_if_due(fd, track ? track->items : NULL, recorded_count);
    }

    if (ret < 0) {
        errno = err;
    }
    return ret;
}

// Logger file output & summary formatting
static size_t g_log_bytes_written = 0;
static bool g_log_capped = false;

static void write_log_line(FILE *fp, const char *line) {
    if (!fp || g_log_capped) return;
    size_t len = strlen(line);
    if (g_log_bytes_written + len > MAX_LOG_SIZE) {
        static const char cap_msg[] = "[xl-commit-shim] Log file reached 4 MB limit, stopping logging.\n";
        fwrite(cap_msg, 1, sizeof(cap_msg) - 1, fp);
        fflush(fp);
        g_log_bytes_written += sizeof(cap_msg) - 1;
        g_log_capped = true;
        return;
    }
    fwrite(line, 1, len, fp);
    g_log_bytes_written += len;
}

static int compare_u64(const void *a, const void *b) {
    uint64_t val_a = *(const uint64_t *)a;
    uint64_t val_b = *(const uint64_t *)b;
    if (val_a < val_b) return -1;
    if (val_a > val_b) return 1;
    return 0;
}

static void update_summary_with_commit(const RingEntry *e) {
    if (e->is_fallback) {
        g_summary.fallbacks++;
    }
    if (e->is_merged) {
        g_summary.merged_commits++;
        if (g_summary.merged_duration_count < MAX_SUMMARY_DURATIONS) {
            g_summary.merged_durations[g_summary.merged_duration_count++] = e->duration_ns;
        }
    } else {
        if (e->call_site < CALL_SITE_COUNT) {
            g_summary.site_commits[e->call_site]++;
        }
        if (g_summary.duration_count < MAX_SUMMARY_DURATIONS) {
            g_summary.durations[g_summary.duration_count++] = e->duration_ns;
        }
    }
    if (e->ret == 0) {
        g_summary.ret_ok++;
    } else {
        g_summary.ret_err++;
    }

    bool found_tid = false;
    for (uint32_t i = 0; i < g_summary.num_tids; i++) {
        if (g_summary.tids[i].tid == e->tid) {
            g_summary.tids[i].count++;
            found_tid = true;
            break;
        }
    }
    if (!found_tid && g_summary.num_tids < MAX_SUMMARY_TIDS) {
        g_summary.tids[g_summary.num_tids].tid = e->tid;
        g_summary.tids[g_summary.num_tids].count = 1;
        g_summary.num_tids++;
    }
}

static void write_summary_line(FILE *fp, uint64_t elapsed_ns) {
    if (!fp || g_log_capped) return;

    double sec = (elapsed_ns > 0) ? ((double)elapsed_ns / 1e9) : 5.0;
    if (sec <= 0.0) sec = 1.0;

    double rate_frame = (double)g_summary.site_commits[CALL_SITE_FRAME] / sec;
    double rate_cursor = (double)g_summary.site_commits[CALL_SITE_CURSOR] / sec;
    double rate_other = (double)g_summary.site_commits[CALL_SITE_OTHER] / sec;
    double rate_merged = (double)g_summary.merged_commits / sec;

    uint64_t min_dur = 0, med_dur = 0, p99_dur = 0, max_dur = 0;
    if (g_summary.duration_count > 0) {
        qsort(g_summary.durations, g_summary.duration_count, sizeof(uint64_t), compare_u64);
        min_dur = g_summary.durations[0];
        med_dur = g_summary.durations[g_summary.duration_count / 2];
        p99_dur = g_summary.durations[(g_summary.duration_count * 99) / 100];
        max_dur = g_summary.durations[g_summary.duration_count - 1];
    }

    uint64_t m_min_dur = 0, m_med_dur = 0, m_p99_dur = 0, m_max_dur = 0;
    if (g_summary.merged_duration_count > 0) {
        qsort(g_summary.merged_durations, g_summary.merged_duration_count, sizeof(uint64_t), compare_u64);
        m_min_dur = g_summary.merged_durations[0];
        m_med_dur = g_summary.merged_durations[g_summary.merged_duration_count / 2];
        m_p99_dur = g_summary.merged_durations[(g_summary.merged_duration_count * 99) / 100];
        m_max_dur = g_summary.merged_durations[g_summary.merged_duration_count - 1];
    }

    char tid_buf[256];
    int tid_pos = 0;
    tid_buf[0] = '\0';
    for (uint32_t i = 0; i < g_summary.num_tids; i++) {
        if (i > 0 && tid_pos < (int)sizeof(tid_buf) - 2) {
            tid_pos += snprintf(tid_buf + tid_pos, sizeof(tid_buf) - tid_pos, ", ");
        }
        if (tid_pos < (int)sizeof(tid_buf) - 24) {
            tid_pos += snprintf(tid_buf + tid_pos, sizeof(tid_buf) - tid_pos, "%d:%u",
                                (int)g_summary.tids[i].tid, g_summary.tids[i].count);
        }
    }

    char summary_line[512];
    snprintf(summary_line, sizeof(summary_line),
             "[SUMMARY 5s] rate(frame)=%.1f/s rate(cursor)=%.1f/s rate(other)=%.1f/s rate(merged)=%.1f/s | tids={%s} | "
             "dur_ns min=%llu med=%llu p99=%llu max=%llu | "
             "merged_dur_ns min=%llu med=%llu p99=%llu max=%llu | "
             "fallbacks=%u | ret={0:%u, err:%u}\n",
             rate_frame, rate_cursor, rate_other, rate_merged, tid_buf,
             (unsigned long long)min_dur, (unsigned long long)med_dur,
             (unsigned long long)p99_dur, (unsigned long long)max_dur,
             (unsigned long long)m_min_dur, (unsigned long long)m_med_dur,
             (unsigned long long)m_p99_dur, (unsigned long long)m_max_dur,
             g_summary.fallbacks,
             g_summary.ret_ok, g_summary.ret_err);

    write_log_line(fp, summary_line);
    memset(&g_summary, 0, sizeof(g_summary));
}

static void drain_ring_buffer(FILE *fp) {
    uint64_t tail = atomic_load_explicit(&g_ring_tail, memory_order_relaxed);
    while (1) {
        uint64_t expected_seq = tail + 1;
        uint32_t slot = tail & RING_BUFFER_MASK;
        if (atomic_load_explicit(&g_ring[slot].seq, memory_order_acquire) != expected_seq) {
            break;
        }

        const RingEntry *e = &g_ring[slot].entry;
        char line[2048];

        if (e->type == ENTRY_COMMIT) {
            update_summary_with_commit(e);
            const char *site_name = "other";
            if (e->call_site == CALL_SITE_FRAME) site_name = "frame";
            else if (e->call_site == CALL_SITE_CURSOR) site_name = "cursor";

            int pos = snprintf(line, sizeof(line),
                               "commit time_ns=%llu tid=%d site=%s%s flags=0x%x items=%u userdata=%d ret=%d dur_ns=%llu",
                               (unsigned long long)e->time_ns, (int)e->tid, site_name,
                               e->is_merged ? " (merged)" : "",
                               e->flags, e->item_count, e->has_user_data ? 1 : 0,
                               e->ret, (unsigned long long)e->duration_ns);

            if (e->has_items && e->num_items > 0) {
                pos += snprintf(line + pos, sizeof(line) - pos, " items=[");
                for (uint32_t i = 0; i < e->num_items; i++) {
                    pos += snprintf(line + pos, sizeof(line) - pos, "%s%u:%u=%llu",
                                    (i > 0) ? ", " : "",
                                    e->items[i].object_id,
                                    e->items[i].property_id,
                                    (unsigned long long)e->items[i].value);
                    if (pos >= (int)sizeof(line) - 32) break;
                }
                pos += snprintf(line + pos, sizeof(line) - pos, "]");
            }
            snprintf(line + pos, sizeof(line) - pos, "\n");
            write_log_line(fp, line);
        } else if (e->type == ENTRY_STAGE2) {
            snprintf(line, sizeof(line),
                     "[STAGE2] TEST_ONLY commit time_ns=%llu tid=%d ret=%d errno=%d dur_ns=%llu\n",
                     (unsigned long long)e->time_ns, (int)e->tid, e->ret, e->err,
                     (unsigned long long)e->duration_ns);
            write_log_line(fp, line);
        } else if (e->type == ENTRY_MSG) {
            snprintf(line, sizeof(line), "%s\n", e->msg);
            write_log_line(fp, line);
        }

        tail++;
    }
    atomic_store_explicit(&g_ring_tail, tail, memory_order_release);
}

// Background logger thread
static void *logger_thread_func(void *arg) {
    (void)arg;
    struct sched_param param;
    param.sched_priority = 0;
    pthread_setschedparam(pthread_self(), SCHED_OTHER, &param);
#ifdef __linux__
    nice(10);
#endif

    // Wait until the first commit starts the logger
    pthread_mutex_lock(&g_start_mutex);
    while (!atomic_load_explicit(&g_logger_started, memory_order_acquire)) {
        pthread_cond_wait(&g_start_cond, &g_start_mutex);
    }
    pthread_mutex_unlock(&g_start_mutex);

    FILE *fp = NULL;
    uint64_t last_summary_ns = shim_monotonic_ns();

    while (!atomic_load_explicit(&g_logger_stop, memory_order_relaxed)) {
        sleep(1);

        // Check stage 2 marker file once per second
        bool s2 = (access("/tmp/xl-commit-shim-stage2", F_OK) == 0);
        atomic_store_explicit(&g_stage2_enabled, s2, memory_order_release);

        // Check stage 3 marker file once per second
        bool s3 = (access("/tmp/xl-commit-shim-stage3", F_OK) == 0);
        if (!s3) {
            atomic_store_explicit(&g_stage3_failed, false, memory_order_relaxed);
            atomic_store_explicit(&g_stage3_enabled, false, memory_order_release);
        } else {
            if (!atomic_load_explicit(&g_stage3_failed, memory_order_relaxed)) {
                atomic_store_explicit(&g_stage3_enabled, true, memory_order_release);
            }
        }

        if (!fp && !g_log_capped) {
            fp = fopen("/tmp/xl-commit-shim.log", "a");
            if (fp) {
                struct stat st;
                if (fstat(fileno(fp), &st) == 0) {
                    g_log_bytes_written = (size_t)st.st_size;
                }
            }
        }

        drain_ring_buffer(fp);

        uint64_t now_ns = shim_monotonic_ns();
        if (now_ns - last_summary_ns >= 5000000000ULL) {
            write_summary_line(fp, now_ns - last_summary_ns);
            last_summary_ns = now_ns;
        }

        if (fp) {
            fflush(fp);
        }
    }

    if (fp) {
        drain_ring_buffer(fp);
        fclose(fp);
    }
    return NULL;
}

#ifndef SHIM_TEST
__attribute__((constructor))
static void shim_constructor(void) {
    init_real_symbols();
    pthread_create(&g_logger_thread, NULL, logger_thread_func, NULL);
}

__attribute__((destructor))
static void shim_destructor(void) {
    atomic_store_explicit(&g_logger_stop, true, memory_order_release);
    ensure_logger_started();
    pthread_join(g_logger_thread, NULL);
}
#endif

#ifdef SHIM_TEST
void shim_test_reset(void) {
    init_real_symbols();
    atomic_store_explicit(&g_ring_head, 0, memory_order_seq_cst);
    atomic_store_explicit(&g_ring_tail, 0, memory_order_seq_cst);
    for (int i = 0; i < RING_BUFFER_SIZE; i++) {
        atomic_store_explicit(&g_ring[i].seq, 0, memory_order_seq_cst);
    }
    for (int i = 0; i < CALL_SITE_COUNT; i++) {
        atomic_store_explicit(&g_call_site_counts[i], 0, memory_order_seq_cst);
    }
    for (int i = 0; i < MAX_TRACKED_REQS_PER_THREAD; i++) {
        tl_requests[i].req = NULL;
        tl_requests[i].count = 0;
    }
    atomic_store_explicit(&g_stage2_enabled, false, memory_order_seq_cst);
    atomic_store_explicit(&g_last_stage2_commit_ns, 0, memory_order_seq_cst);
    atomic_store_explicit(&g_stage3_enabled, false, memory_order_seq_cst);
    atomic_store_explicit(&g_stage3_failed, false, memory_order_seq_cst);
    atomic_store_explicit(&g_plane46_active, false, memory_order_seq_cst);
    atomic_store_explicit(&g_stage3_fallbacks, 0, memory_order_seq_cst);
    atomic_store_explicit(&g_stage3_skipped_merges, 0, memory_order_seq_cst);
    atomic_store_explicit(&g_logger_started, false, memory_order_seq_cst);
    atomic_store_explicit(&g_logger_stop, false, memory_order_seq_cst);
    g_test_fb_id = 0;
    g_test_dumb_handle = 0;
    g_test_dumb_size = 0;
    g_prev_frame_item_count = 0;
    g_has_prev_frame = false;
    g_props_logged = false;
    memset(&g_plane46, 0, sizeof(g_plane46));
    memset(&g_summary, 0, sizeof(g_summary));
    g_log_bytes_written = 0;
    g_log_capped = false;
}

void shim_test_set_stage2(bool enabled) {
    atomic_store_explicit(&g_stage2_enabled, enabled, memory_order_seq_cst);
}

void shim_test_set_stage3(bool enabled) {
    atomic_store_explicit(&g_stage3_enabled, enabled, memory_order_seq_cst);
    if (!enabled) {
        atomic_store_explicit(&g_stage3_failed, false, memory_order_seq_cst);
    }
}

bool shim_test_is_stage3_enabled(void) {
    return atomic_load_explicit(&g_stage3_enabled, memory_order_seq_cst);
}

bool shim_test_is_plane46_active(void) {
    return atomic_load_explicit(&g_plane46_active, memory_order_seq_cst);
}

void shim_test_set_last_stage2_ns(uint64_t ns) {
    atomic_store_explicit(&g_last_stage2_commit_ns, ns, memory_order_seq_cst);
}

uint32_t shim_test_get_ring_count(void) {
    uint64_t head = atomic_load_explicit(&g_ring_head, memory_order_acquire);
    uint64_t tail = atomic_load_explicit(&g_ring_tail, memory_order_acquire);
    return (uint32_t)(head - tail);
}

bool shim_test_get_ring_entry(uint32_t index, RingEntry *out) {
    uint64_t tail = atomic_load_explicit(&g_ring_tail, memory_order_acquire);
    uint64_t head = atomic_load_explicit(&g_ring_head, memory_order_acquire);
    if (tail + index >= head) return false;
    uint32_t slot = (tail + index) & RING_BUFFER_MASK;
    if (out) {
        *out = g_ring[slot].entry;
    }
    return true;
}

void shim_test_drain_logger(FILE *fp) {
    drain_ring_buffer(fp);
}

void shim_test_write_summary(FILE *fp, uint64_t elapsed_ns) {
    write_summary_line(fp, elapsed_ns);
}

void shim_test_set_log_bytes_written(size_t bytes) {
    g_log_bytes_written = bytes;
}

bool shim_test_is_log_capped(void) {
    return g_log_capped;
}

void shim_test_start_thread(void) {
    pthread_create(&g_logger_thread, NULL, logger_thread_func, NULL);
}

void shim_test_stop_thread(void) {
    atomic_store_explicit(&g_logger_stop, true, memory_order_release);
    ensure_logger_started();
    pthread_join(g_logger_thread, NULL);
}
#endif
