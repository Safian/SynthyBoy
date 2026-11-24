// SynthyBoy - MPC XL / Key 61 atomic commit takeover & measurement shim.
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
#define MAX_CACHED_PLANES           8
#define MAX_SUMMARY_DURATIONS       4096
#define MAX_SUMMARY_TIDS            32
#define MAX_LOG_SIZE                (4 * 1024 * 1024)

#define DEFAULT_TAKEOVER_FLAG_PATH  "/tmp/xl-commit-shim-takeover"
#define DEFAULT_STAGE2_FLAG_PATH    "/tmp/xl-commit-shim-stage2"
#define DEFAULT_STAGE3_FLAG_PATH    "/tmp/xl-commit-shim-stage3"

// Real symbol function pointers
static drmModeAtomicReqPtr (*real_drmModeAtomicAlloc)(void) = NULL;
static void (*real_drmModeAtomicFree)(drmModeAtomicReqPtr) = NULL;
static int (*real_drmModeAtomicAddProperty)(drmModeAtomicReqPtr, uint32_t, uint32_t, uint64_t) = NULL;
static int (*real_drmModeAtomicGetCursor)(drmModeAtomicReqPtr) = NULL;
static int (*real_drmModeAtomicCommit)(int, drmModeAtomicReqPtr, uint32_t, void *) = NULL;

static drmModeResPtr (*real_drmModeGetResources)(int) = NULL;
static void (*real_drmModeFreeResources)(drmModeResPtr) = NULL;
static drmModeCrtcPtr (*real_drmModeGetCrtc)(int, uint32_t) = NULL;
static void (*real_drmModeFreeCrtc)(drmModeCrtcPtr) = NULL;
static drmModePlaneResPtr (*real_drmModeGetPlaneResources)(int) = NULL;
static void (*real_drmModeFreePlaneResources)(drmModePlaneResPtr) = NULL;
static drmModePlanePtr (*real_drmModeGetPlane)(int, uint32_t) = NULL;
static void (*real_drmModeFreePlane)(drmModePlanePtr) = NULL;

static drmModeObjectPropertiesPtr (*real_drmModeObjectGetProperties)(int, uint32_t, uint32_t) = NULL;
static void (*real_drmModeFreeObjectProperties)(drmModeObjectPropertiesPtr) = NULL;
static drmModePropertyPtr (*real_drmModeGetProperty)(int, uint32_t) = NULL;
static void (*real_drmModeFreeProperty)(drmModePropertyPtr) = NULL;
static int (*real_drmModeAddFB2)(int, uint32_t, uint32_t, uint32_t,
                                const uint32_t[4], const uint32_t[4], const uint32_t[4],
                                uint32_t *, uint32_t) = NULL;
static int (*real_drmModeRmFB)(int, uint32_t) = NULL;
static drmModeFB2Ptr (*real_drmModeGetFB2)(int, uint32_t) = NULL;
static void (*real_drmModeFreeFB2)(drmModeFB2Ptr) = NULL;
static drmModeFBPtr (*real_drmModeGetFB)(int, uint32_t) = NULL;
static void (*real_drmModeFreeFB)(drmModeFBPtr) = NULL;
static int (*real_drmIoctl)(int, unsigned long, void *) = NULL;

#ifdef SHIM_TEST
extern drmModeAtomicReqPtr fake_drmModeAtomicAlloc(void);
extern void fake_drmModeAtomicFree(drmModeAtomicReqPtr req);
extern int fake_drmModeAtomicAddProperty(drmModeAtomicReqPtr req, uint32_t object_id, uint32_t property_id, uint64_t value);
extern int fake_drmModeAtomicGetCursor(drmModeAtomicReqPtr req);
extern int fake_drmModeAtomicCommit(int fd, drmModeAtomicReqPtr req, uint32_t flags, void *user_data);

extern drmModeResPtr fake_drmModeGetResources(int fd);
extern void fake_drmModeFreeResources(drmModeResPtr ptr);
extern drmModeCrtcPtr fake_drmModeGetCrtc(int fd, uint32_t crtcId);
extern void fake_drmModeFreeCrtc(drmModeCrtcPtr ptr);
extern drmModePlaneResPtr fake_drmModeGetPlaneResources(int fd);
extern void fake_drmModeFreePlaneResources(drmModePlaneResPtr ptr);
extern drmModePlanePtr fake_drmModeGetPlane(int fd, uint32_t plane_id);
extern void fake_drmModeFreePlane(drmModePlanePtr ptr);

extern drmModeObjectPropertiesPtr fake_drmModeObjectGetProperties(int fd, uint32_t object_id, uint32_t object_type);
extern void fake_drmModeFreeObjectProperties(drmModeObjectPropertiesPtr ptr);
extern drmModePropertyPtr fake_drmModeGetProperty(int fd, uint32_t propertyId);
extern void fake_drmModeFreeProperty(drmModePropertyPtr ptr);
extern int fake_drmModeAddFB2(int fd, uint32_t width, uint32_t height, uint32_t pixel_format,
                             const uint32_t bo_handles[4], const uint32_t pitches[4],
                             const uint32_t offsets[4], uint32_t *buf_id, uint32_t flags);
extern int fake_drmModeRmFB(int fd, uint32_t bufferId);
extern drmModeFB2Ptr fake_drmModeGetFB2(int fd, uint32_t fb_id);
extern void fake_drmModeFreeFB2(drmModeFB2Ptr ptr);
extern drmModeFBPtr fake_drmModeGetFB(int fd, uint32_t fb_id);
extern void fake_drmModeFreeFB(drmModeFBPtr ptr);
extern int fake_drmIoctl(int fd, unsigned long request, void *arg);
extern void *fake_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
extern int fake_munmap(void *addr, size_t length);

#define shim_mmap fake_mmap
#define shim_munmap fake_munmap
#else
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
    real_drmModeGetResources = fake_drmModeGetResources;
    real_drmModeFreeResources = fake_drmModeFreeResources;
    real_drmModeGetCrtc = fake_drmModeGetCrtc;
    real_drmModeFreeCrtc = fake_drmModeFreeCrtc;
    real_drmModeGetPlaneResources = fake_drmModeGetPlaneResources;
    real_drmModeFreePlaneResources = fake_drmModeFreePlaneResources;
    real_drmModeGetPlane = fake_drmModeGetPlane;
    real_drmModeFreePlane = fake_drmModeFreePlane;
    real_drmModeObjectGetProperties = fake_drmModeObjectGetProperties;
    real_drmModeFreeObjectProperties = fake_drmModeFreeObjectProperties;
    real_drmModeGetProperty = fake_drmModeGetProperty;
    real_drmModeFreeProperty = fake_drmModeFreeProperty;
    real_drmModeAddFB2 = fake_drmModeAddFB2;
    real_drmModeRmFB = fake_drmModeRmFB;
    real_drmModeGetFB2 = fake_drmModeGetFB2;
    real_drmModeFreeFB2 = fake_drmModeFreeFB2;
    real_drmModeGetFB = fake_drmModeGetFB;
    real_drmModeFreeFB = fake_drmModeFreeFB;
    real_drmIoctl = fake_drmIoctl;
#else
    if (real_drmModeAtomicCommit) return;
    real_drmModeAtomicAlloc = (drmModeAtomicReqPtr (*)(void))dlsym(RTLD_NEXT, "drmModeAtomicAlloc");
    real_drmModeAtomicFree = (void (*)(drmModeAtomicReqPtr))dlsym(RTLD_NEXT, "drmModeAtomicFree");
    real_drmModeAtomicAddProperty = (int (*)(drmModeAtomicReqPtr, uint32_t, uint32_t, uint64_t))dlsym(RTLD_NEXT, "drmModeAtomicAddProperty");
    real_drmModeAtomicGetCursor = (int (*)(drmModeAtomicReqPtr))dlsym(RTLD_NEXT, "drmModeAtomicGetCursor");
    real_drmModeAtomicCommit = (int (*)(int, drmModeAtomicReqPtr, uint32_t, void *))dlsym(RTLD_NEXT, "drmModeAtomicCommit");

    real_drmModeGetResources = (drmModeResPtr (*)(int))dlsym(RTLD_NEXT, "drmModeGetResources");
    real_drmModeFreeResources = (void (*)(drmModeResPtr))dlsym(RTLD_NEXT, "drmModeFreeResources");
    real_drmModeGetCrtc = (drmModeCrtcPtr (*)(int, uint32_t))dlsym(RTLD_NEXT, "drmModeGetCrtc");
    real_drmModeFreeCrtc = (void (*)(drmModeCrtcPtr))dlsym(RTLD_NEXT, "drmModeFreeCrtc");
    real_drmModeGetPlaneResources = (drmModePlaneResPtr (*)(int))dlsym(RTLD_NEXT, "drmModeGetPlaneResources");
    real_drmModeFreePlaneResources = (void (*)(drmModePlaneResPtr))dlsym(RTLD_NEXT, "drmModeFreePlaneResources");
    real_drmModeGetPlane = (drmModePlanePtr (*)(int, uint32_t))dlsym(RTLD_NEXT, "drmModeGetPlane");
    real_drmModeFreePlane = (void (*)(drmModePlanePtr))dlsym(RTLD_NEXT, "drmModeFreePlane");

    real_drmModeObjectGetProperties = (drmModeObjectPropertiesPtr (*)(int, uint32_t, uint32_t))dlsym(RTLD_NEXT, "drmModeObjectGetProperties");
    real_drmModeFreeObjectProperties = (void (*)(drmModeObjectPropertiesPtr))dlsym(RTLD_NEXT, "drmModeFreeObjectProperties");
    real_drmModeGetProperty = (drmModePropertyPtr (*)(int, uint32_t))dlsym(RTLD_NEXT, "drmModeGetProperty");
    real_drmModeFreeProperty = (void (*)(drmModePropertyPtr))dlsym(RTLD_NEXT, "drmModeFreeProperty");
    real_drmModeAddFB2 = (int (*)(int, uint32_t, uint32_t, uint32_t, const uint32_t[4], const uint32_t[4], const uint32_t[4], uint32_t *, uint32_t))dlsym(RTLD_NEXT, "drmModeAddFB2");
    real_drmModeRmFB = (int (*)(int, uint32_t))dlsym(RTLD_NEXT, "drmModeRmFB");
    real_drmModeGetFB2 = (drmModeFB2Ptr (*)(int, uint32_t))dlsym(RTLD_NEXT, "drmModeGetFB2");
    real_drmModeFreeFB2 = (void (*)(drmModeFB2Ptr))dlsym(RTLD_NEXT, "drmModeFreeFB2");
    real_drmModeGetFB = (drmModeFBPtr (*)(int, uint32_t))dlsym(RTLD_NEXT, "drmModeGetFB");
    real_drmModeFreeFB = (void (*)(drmModeFBPtr))dlsym(RTLD_NEXT, "drmModeFreeFB");
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

static const char *takeover_flag_path(void) {
    const char *env = getenv("SYNTHYBOY_SHIM_TAKEOVER_FILE");
    return (env && env[0]) ? env : DEFAULT_TAKEOVER_FLAG_PATH;
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
static _Atomic bool g_plane_active = false;
static _Atomic uint32_t g_stage3_disable_consec_failures = 0;
static _Atomic uint32_t g_stage3_fallbacks = 0;
static _Atomic uint32_t g_stage3_skipped_merges = 0;

static _Atomic bool g_takeover_enabled = false;
static _Atomic bool g_takeover_failed = false;
static _Atomic uint32_t g_takeover_consec_failures = 0;
static _Atomic uint32_t g_takeover_fallbacks = 0;
static _Atomic uint32_t g_nonblock_skipped = 0;

static _Atomic uint32_t g_guard_checked = 0;
static _Atomic uint32_t g_guard_match = 0;
static _Atomic uint32_t g_guard_mismatch = 0;
static _Atomic uint32_t g_guard_unknown = 0;
static _Atomic uint32_t g_guard_map_errors = 0;
static _Atomic uint32_t g_plane_off_added = 0;

static LayerBlock *g_layer_block = NULL;
static bool g_layer_block_owned = false;

static _Atomic bool g_logger_started = false;
static _Atomic bool g_logger_stop = false;
static pthread_mutex_t g_start_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_start_cond  = PTHREAD_COND_INITIALIZER;
static pthread_t       g_logger_thread;

// Plane properties cache
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
    uint32_t type_id;
    uint64_t type_val;
    bool has_type;
    bool resolved;
} PlanePropIds;

typedef struct {
    uint32_t plane_id;
    PlanePropIds props;
} CachedPlaneProps;

typedef struct {
    uint32_t crtc_id;
    int crtc_index;
    uint32_t primary_plane_id;
    uint32_t overlay_plane_id;
    uint32_t panel_w;
    uint32_t panel_h;
    bool discovered;
    bool overlay_searched;
} DiscoveredDisplay;

static CachedPlaneProps g_plane_cache[MAX_CACHED_PLANES];
static uint32_t g_plane_cache_count = 0;
static DiscoveredDisplay g_disc = {0, -1, 0, 0, 0, 0, false, false};

static uint32_t g_test_fb_id = 0;
static uint32_t g_test_dumb_handle = 0;
static uint64_t g_test_dumb_size = 0;
static uint32_t g_test_fb_w = 0;
static uint32_t g_test_fb_h = 0;
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
    uint32_t nonblock_skipped;
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
                               bool is_nonblock_skipped,
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
    e->is_nonblock_skipped = is_nonblock_skipped;
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
    e->is_nonblock_skipped = false;
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
    e->is_nonblock_skipped = false;
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

static void ensure_layer_block_mapped(bool create) {
    if (g_layer_block != NULL) return;
    LayerBlock *b = layer_block_map(layer_block_path(), create);
    if (b) {
        if (!layer_block_valid(b)) {
            layer_block_unmap(b);
            return;
        }
        g_layer_block = b;
        g_layer_block_owned = true;
    }
}

// Plane property lookup & caching (no allocation on subsequent uses)
static void query_plane_props(int fd, uint32_t plane_id, PlanePropIds *out_props, bool log_props) {
    if (!out_props) return;
    memset(out_props, 0, sizeof(*out_props));

    if (!real_drmModeObjectGetProperties || !real_drmModeGetProperty ||
        !real_drmModeFreeProperty || !real_drmModeFreeObjectProperties) {
        return;
    }

    drmModeObjectPropertiesPtr props = real_drmModeObjectGetProperties(fd, plane_id, DRM_MODE_OBJECT_PLANE);
    if (!props) return;

    char msg[256];
    int pos = log_props ? snprintf(msg, sizeof(msg), "[PROPS] Plane %u props:", plane_id) : 0;

    for (uint32_t i = 0; i < props->count_props; i++) {
        drmModePropertyPtr p = real_drmModeGetProperty(fd, props->props[i]);
        if (p) {
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
            else if (strcmp(p->name, "type") == 0) {
                out_props->type_id = p->prop_id;
                if (props->prop_values) {
                    out_props->type_val = props->prop_values[i];
                    out_props->has_type = true;
                }
            }
            if (log_props && pos < (int)sizeof(msg) - 24) {
                pos += snprintf(msg + pos, sizeof(msg) - pos, " %s=%u", p->name, p->prop_id);
            }
            real_drmModeFreeProperty(p);
        }
    }
    out_props->resolved = (out_props->fb_id != 0 && out_props->crtc_id != 0);
    real_drmModeFreeObjectProperties(props);

    if (log_props) {
        ring_enqueue_msg(msg);
    }
}

static const PlanePropIds *get_plane_props(int fd, uint32_t plane_id) {
    for (uint32_t i = 0; i < g_plane_cache_count; i++) {
        if (g_plane_cache[i].plane_id == plane_id) {
            return &g_plane_cache[i].props;
        }
    }
    PlanePropIds props;
    query_plane_props(fd, plane_id, &props, true);
    if (g_plane_cache_count < MAX_CACHED_PLANES) {
        g_plane_cache[g_plane_cache_count].plane_id = plane_id;
        g_plane_cache[g_plane_cache_count].props = props;
        return &g_plane_cache[g_plane_cache_count++].props;
    }
    // Cache full: reuse last slot so commit path never allocates
    g_plane_cache[MAX_CACHED_PLANES - 1].plane_id = plane_id;
    g_plane_cache[MAX_CACHED_PLANES - 1].props = props;
    return &g_plane_cache[MAX_CACHED_PLANES - 1].props;
}

// Runtime discovery: CRTC and primary plane from the host's frame commit, panel size from CRTC,
// and first free overlay plane via drmModeGetPlaneResources (no FB, matching possible_crtcs, type overlay).
static void discover_display_once(int fd, const ShimPropItem *host_items, uint32_t host_item_count, bool need_overlay_plane) {
    if (!g_disc.discovered) {
        g_disc.discovered = true;

        uint32_t unique_objs[8];
        uint32_t num_unique = 0;
        if (host_items) {
            for (uint32_t i = 0; i < host_item_count && num_unique < 8; i++) {
                uint32_t oid = host_items[i].object_id;
                bool seen = false;
                for (uint32_t j = 0; j < num_unique; j++) {
                    if (unique_objs[j] == oid) {
                        seen = true;
                        break;
                    }
                }
                if (!seen) {
                    unique_objs[num_unique++] = oid;
                }
            }
        }

        // 1. Inspect host request items to find primary plane and CRTC
        uint32_t req_w = 0, req_h = 0;
        for (uint32_t i = 0; i < num_unique; i++) {
            uint32_t oid = unique_objs[i];
            const PlanePropIds *pp = get_plane_props(fd, oid);
            if (!pp || !pp->resolved) continue;

            if (g_disc.primary_plane_id == 0) {
                g_disc.primary_plane_id = oid;
            }
            for (uint32_t k = 0; k < host_item_count; k++) {
                if (host_items[k].object_id != oid) continue;
                if (pp->crtc_id && host_items[k].property_id == pp->crtc_id && host_items[k].value != 0) {
                    g_disc.primary_plane_id = oid;
                    g_disc.crtc_id = (uint32_t)host_items[k].value;
                } else if (pp->crtc_w && host_items[k].property_id == pp->crtc_w && host_items[k].value != 0) {
                    if (req_w == 0) req_w = (uint32_t)host_items[k].value;
                } else if (pp->crtc_h && host_items[k].property_id == pp->crtc_h && host_items[k].value != 0) {
                    if (req_h == 0) req_h = (uint32_t)host_items[k].value;
                }
            }
        }

        // 2. Query DRM resources for CRTC index and panel mode size (and CRTC fallback if request lacked CRTC_ID)
        if (real_drmModeGetResources && real_drmModeFreeResources) {
            drmModeResPtr res = real_drmModeGetResources(fd);
            if (res) {
                if (g_disc.crtc_id != 0) {
                    for (int i = 0; i < res->count_crtcs; i++) {
                        if (res->crtcs[i] == g_disc.crtc_id) {
                            g_disc.crtc_index = i;
                            break;
                        }
                    }
                } else if (real_drmModeGetCrtc && real_drmModeFreeCrtc) {
                    for (int i = 0; i < res->count_crtcs; i++) {
                        drmModeCrtcPtr c = real_drmModeGetCrtc(fd, res->crtcs[i]);
                        if (c) {
                            if (c->mode_valid && (c->width > 0 || c->mode.hdisplay > 0)) {
                                g_disc.crtc_id = c->crtc_id;
                                g_disc.crtc_index = i;
                                g_disc.panel_w = c->width ? c->width : c->mode.hdisplay;
                                g_disc.panel_h = c->height ? c->height : c->mode.vdisplay;
                                real_drmModeFreeCrtc(c);
                                break;
                            }
                            real_drmModeFreeCrtc(c);
                        }
                    }
                }
                real_drmModeFreeResources(res);
            }
        }

        if (g_disc.crtc_id != 0 && (g_disc.panel_w == 0 || g_disc.panel_h == 0) &&
            real_drmModeGetCrtc && real_drmModeFreeCrtc) {
            drmModeCrtcPtr c = real_drmModeGetCrtc(fd, g_disc.crtc_id);
            if (c) {
                if (c->width > 0 && c->height > 0) {
                    g_disc.panel_w = c->width;
                    g_disc.panel_h = c->height;
                } else if (c->mode_valid) {
                    g_disc.panel_w = c->mode.hdisplay;
                    g_disc.panel_h = c->mode.vdisplay;
                }
                real_drmModeFreeCrtc(c);
            }
        }
        if (g_disc.panel_w == 0) g_disc.panel_w = req_w;
        if (g_disc.panel_h == 0) g_disc.panel_h = req_h;
    }

    // 3. Discover first free overlay plane for Stage 2 / Stage 3 test rectangle
    if (need_overlay_plane && !g_disc.overlay_searched) {
        g_disc.overlay_searched = true;
        if (g_disc.crtc_index >= 0 && real_drmModeGetPlaneResources && real_drmModeFreePlaneResources &&
            real_drmModeGetPlane && real_drmModeFreePlane) {
            drmModePlaneResPtr pres = real_drmModeGetPlaneResources(fd);
            if (pres) {
                for (uint32_t i = 0; i < pres->count_planes; i++) {
                    uint32_t pid = pres->planes[i];
                    if (pid == g_disc.primary_plane_id) continue;
                    drmModePlanePtr pl = real_drmModeGetPlane(fd, pid);
                    if (!pl) continue;
                    bool free_and_possible = (pl->fb_id == 0 && pl->crtc_id == 0 &&
                                              (pl->possible_crtcs & (1u << g_disc.crtc_index)) != 0);
                    real_drmModeFreePlane(pl);
                    if (!free_and_possible) continue;

                    const PlanePropIds *pp = get_plane_props(fd, pid);
                    if (pp && pp->resolved && (!pp->has_type || pp->type_val == DRM_PLANE_TYPE_OVERLAY)) {
                        g_disc.overlay_plane_id = pid;
                        break;
                    }
                }
                real_drmModeFreePlaneResources(pres);
            }
        }

        char msg[160];
        snprintf(msg, sizeof(msg),
                 "[DISCOVERY] crtc=%u (idx=%d) panel=%ux%u primary_plane=%u overlay_plane=%u",
                 g_disc.crtc_id, g_disc.crtc_index, g_disc.panel_w, g_disc.panel_h,
                 g_disc.primary_plane_id, g_disc.overlay_plane_id);
        ring_enqueue_msg(msg);
    }
}

// Host Framebuffer cache (dumb buffers mapped once per host FB id, dropped on RmFB)
typedef struct {
    uint32_t fb_id;
    uint32_t handle;
    void *map_addr;
    size_t map_size;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t format;
} HostFbCache;

#define MAX_HOST_FB_CACHE 16
static HostFbCache g_host_fbs[MAX_HOST_FB_CACHE];

static void evict_host_fb(uint32_t fb_id) {
    if (fb_id == 0) return;
    for (int i = 0; i < MAX_HOST_FB_CACHE; i++) {
        if (g_host_fbs[i].fb_id == fb_id) {
            if (g_host_fbs[i].map_addr && g_host_fbs[i].map_addr != MAP_FAILED) {
                shim_munmap(g_host_fbs[i].map_addr, g_host_fbs[i].map_size);
            }
            memset(&g_host_fbs[i], 0, sizeof(HostFbCache));
        }
    }
}

static __attribute__((unused)) void clear_all_host_fbs(void) {
    for (int i = 0; i < MAX_HOST_FB_CACHE; i++) {
        if (g_host_fbs[i].fb_id != 0) {
            if (g_host_fbs[i].map_addr && g_host_fbs[i].map_addr != MAP_FAILED) {
                shim_munmap(g_host_fbs[i].map_addr, g_host_fbs[i].map_size);
            }
            memset(&g_host_fbs[i], 0, sizeof(HostFbCache));
        }
    }
}

static const HostFbCache *get_or_map_host_fb(int fd, uint32_t fb_id) {
    if (fb_id == 0) return NULL;
    for (int i = 0; i < MAX_HOST_FB_CACHE; i++) {
        if (g_host_fbs[i].fb_id == fb_id && g_host_fbs[i].map_addr != NULL) {
            return &g_host_fbs[i];
        }
    }

    uint32_t handle = 0;
    uint32_t width = 0, height = 0, pitch = 0, format = 0;

    if (real_drmModeGetFB2 && real_drmModeFreeFB2) {
        drmModeFB2Ptr fb2 = real_drmModeGetFB2(fd, fb_id);
        if (fb2) {
            handle = fb2->handles[0];
            pitch = fb2->pitches[0];
            width = fb2->width;
            height = fb2->height;
            format = fb2->pixel_format;
            real_drmModeFreeFB2(fb2);
        }
    }
    if (handle == 0 && real_drmModeGetFB && real_drmModeFreeFB) {
        drmModeFBPtr fb = real_drmModeGetFB(fd, fb_id);
        if (fb) {
            handle = fb->handle;
            pitch = fb->pitch;
            width = fb->width;
            height = fb->height;
            format = DRM_FORMAT_XRGB8888;
            real_drmModeFreeFB(fb);
        }
    }

    if (handle == 0 || width == 0 || height == 0 || pitch == 0 || !real_drmIoctl) {
        return NULL;
    }

    struct drm_mode_map_dumb map_req;
    memset(&map_req, 0, sizeof(map_req));
    map_req.handle = handle;
    int rc = real_drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map_req);
    if (rc != 0) {
        return NULL;
    }

    size_t map_size = (size_t)pitch * height;
    void *addr = shim_mmap(NULL, map_size, PROT_READ, MAP_SHARED, fd, map_req.offset);
    if (!addr || addr == MAP_FAILED) {
        return NULL;
    }

    int slot = -1;
    for (int i = 0; i < MAX_HOST_FB_CACHE; i++) {
        if (g_host_fbs[i].fb_id == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        slot = 0;
        if (g_host_fbs[0].map_addr && g_host_fbs[0].map_addr != MAP_FAILED) {
            shim_munmap(g_host_fbs[0].map_addr, g_host_fbs[0].map_size);
        }
        memset(&g_host_fbs[0], 0, sizeof(HostFbCache));
    }

    g_host_fbs[slot].fb_id = fb_id;
    g_host_fbs[slot].handle = handle;
    g_host_fbs[slot].map_addr = addr;
    g_host_fbs[slot].map_size = map_size;
    g_host_fbs[slot].width = width;
    g_host_fbs[slot].height = height;
    g_host_fbs[slot].pitch = pitch;
    g_host_fbs[slot].format = format;

    return &g_host_fbs[slot];
}

// Marker reading & coordinate translation
static inline void ui_to_panel(int rotation, int x, int y, int panel_w, int panel_h, int *px, int *py) {
    int r = (rotation == 0 || rotation == 90 || rotation == 180 || rotation == 270) ? rotation : 90;
    switch (r) {
        case 0:   *px = x; *py = y; break;
        case 180: *px = panel_w - 1 - x; *py = panel_h - 1 - y; break;
        case 270: *px = panel_w - 1 - y; *py = x; break;
        default:  *px = y; *py = panel_h - 1 - x; break;
    }
}

static inline bool read_host_fb_pixel(const HostFbCache *fb, int px, int py, uint32_t *out_rgb) {
    if (!fb || !fb->map_addr || px < 0 || (uint32_t)px >= fb->width || py < 0 || (uint32_t)py >= fb->height) {
        return false;
    }
    size_t offset = (size_t)py * fb->pitch + (size_t)px * 4;
    if (offset + 4 > fb->map_size) {
        return false;
    }
    uint32_t raw = *(const uint32_t *)((const uint8_t *)fb->map_addr + offset);
    *out_rgb = raw & 0x00ffffffu;
    return true;
}

#include "view_markers.h"

// Intercepted public function: evict host FB from cache and call real drmModeRmFB
int drmModeRmFB(int fd, uint32_t bufferId) {
    init_real_symbols();
    evict_host_fb(bufferId);
    if (real_drmModeRmFB) {
        return real_drmModeRmFB(fd, bufferId);
    }
    return -1;
}

static void compute_test_rect(uint32_t panel_w, uint32_t panel_h,
                              uint32_t *out_x, uint32_t *out_y,
                              uint32_t *out_w, uint32_t *out_h) {
    if (panel_w == 0) panel_w = 800;
    if (panel_h == 0) panel_h = 1280;
    uint32_t w = (panel_w >= 800) ? 400 : (panel_w / 2);
    uint32_t h = (panel_h >= 600) ? 300 : (panel_h / 2);
    if (w < 64) w = (panel_w >= 64) ? 64 : panel_w;
    if (h < 64) h = (panel_h >= 64) ? 64 : panel_h;
    *out_w = w;
    *out_h = h;
    *out_x = (panel_w - w) / 2;
    *out_y = (panel_h >= 1200) ? 200 : ((panel_h - h) / 2);
}

#define COLOR_WHITE   0x00FFFFFF
#define COLOR_MID_RED 0x00C03030

static void fill_test_pattern(void *map_addr, uint32_t width, uint32_t height, uint32_t pitch) {
    if (!map_addr) return;
    for (uint32_t y = 0; y < height; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)map_addr + y * pitch);
        for (uint32_t x = 0; x < width; x++) {
            if (x < 16 || x >= width - 16 || y < 16 || y >= height - 16) {
                row[x] = COLOR_WHITE;
            } else {
                int diag_y = (int)((x * height) / width);
                int diff = (int)y - diag_y;
                if (diff < 0) diff = -diff;
                row[x] = (diff <= 8) ? COLOR_WHITE : COLOR_MID_RED;
            }
        }
    }
}

static void ensure_test_fb(int fd, uint32_t w, uint32_t h) {
    if (g_test_fb_id != 0) return;
    if (!real_drmIoctl || !real_drmModeAddFB2 || w == 0 || h == 0) return;

    struct drm_mode_create_dumb create;
    memset(&create, 0, sizeof(create));
    create.width = w;
    create.height = h;
    create.bpp = 32;
    create.flags = 0;

    int rc = real_drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create);
    if (rc != 0 || create.handle == 0) return;

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
    rc = real_drmModeAddFB2(fd, w, h, DRM_FORMAT_XRGB8888,
                            bo_handles, pitches, offsets, &fb_id, 0);
    if (rc == 0 && fb_id != 0) {
        g_test_fb_id = fb_id;
        g_test_dumb_handle = create.handle;
        g_test_dumb_size = create.size;
        g_test_fb_w = w;
        g_test_fb_h = h;
        char msg[128];
        snprintf(msg, sizeof(msg), "[STAGE2] Created dumb FB %ux%u id=%u handle=%u pitch=%u",
                 w, h, fb_id, create.handle, create.pitch);
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
        g_test_fb_w = 0;
        g_test_fb_h = 0;
    }
}

// Shared helper: append plane enable/disable properties into `dst` (capped at `max_add` added items).
static uint32_t append_plane_items(ShimPropItem *dst, uint32_t base_count, uint32_t max_total,
                                   uint32_t max_add, uint32_t plane_id, const PlanePropIds *props,
                                   uint32_t crtc_id, uint32_t fb_id,
                                   int32_t crtc_x, int32_t crtc_y, uint32_t crtc_w, uint32_t crtc_h,
                                   uint32_t src_x, uint32_t src_y, uint32_t src_w, uint32_t src_h,
                                   bool enabled) {
    if (!props || !props->resolved || plane_id == 0) return base_count;
    uint32_t n = base_count;
    uint32_t added = 0;

    #define APPEND_ONE(prop, val) do { \
        if ((prop) && added < max_add && n < max_total) { \
            dst[n].object_id = plane_id; \
            dst[n].property_id = (prop); \
            dst[n].value = (val); \
            n++; \
            added++; \
        } \
    } while (0)

    if (enabled) {
        APPEND_ONE(props->fb_id, fb_id);
        APPEND_ONE(props->crtc_id, crtc_id);
        APPEND_ONE(props->crtc_x, (uint64_t)(int64_t)crtc_x);
        APPEND_ONE(props->crtc_y, (uint64_t)(int64_t)crtc_y);
        APPEND_ONE(props->crtc_w, crtc_w);
        APPEND_ONE(props->crtc_h, crtc_h);
        APPEND_ONE(props->src_x, src_x);
        APPEND_ONE(props->src_y, src_y);
        APPEND_ONE(props->src_w, src_w);
        APPEND_ONE(props->src_h, src_h);
    } else {
        APPEND_ONE(props->fb_id, 0);
        APPEND_ONE(props->crtc_id, 0);
    }

    #undef APPEND_ONE
    return n;
}

static int commit_items_request(int fd, uint32_t flags, void *user_data,
                                const ShimPropItem *items, uint32_t item_count,
                                uint64_t *out_t_start, uint64_t *out_dur_ns, int *out_err) {
    void *req_new = real_drmModeAtomicAlloc ? real_drmModeAtomicAlloc() : NULL;
    if (!req_new) {
        *out_err = ENOMEM;
        return -1;
    }
    for (uint32_t i = 0; i < item_count; i++) {
        real_drmModeAtomicAddProperty(req_new, items[i].object_id, items[i].property_id, items[i].value);
    }
    uint64_t t0 = shim_monotonic_ns();
    int ret = real_drmModeAtomicCommit(fd, req_new, flags, user_data);
    uint64_t t1 = shim_monotonic_ns();
    int err = (ret < 0) ? errno : 0;
    real_drmModeAtomicFree(req_new);
    *out_t_start = t0;
    *out_dur_ns = t1 - t0;
    *out_err = err;
    return ret;
}

static void run_stage2_probe_if_due(int fd, const ShimPropItem *host_items, uint32_t host_item_count) {
    uint64_t now_ns = shim_monotonic_ns();
    uint64_t last_ns = atomic_load_explicit(&g_last_stage2_commit_ns, memory_order_relaxed);
    if (now_ns - last_ns < 1000000000ULL) {
        return;
    }
    if (!atomic_compare_exchange_strong_explicit(&g_last_stage2_commit_ns, &last_ns, now_ns,
                                                memory_order_acq_rel, memory_order_relaxed)) {
        return;
    }

    discover_display_once(fd, host_items, host_item_count, true);
    if (g_disc.overlay_plane_id == 0 || g_disc.crtc_id == 0) return;

    uint32_t rx, ry, rw, rh;
    compute_test_rect(g_disc.panel_w, g_disc.panel_h, &rx, &ry, &rw, &rh);
    ensure_test_fb(fd, rw, rh);
    if (g_test_fb_id == 0) return;

    const PlanePropIds *pprops = get_plane_props(fd, g_disc.overlay_plane_id);
    if (!pprops || !pprops->resolved) return;

    ShimPropItem items[128];
    uint32_t n = 0;
    if (host_items) {
        for (uint32_t i = 0; i < host_item_count && n < 128; i++) {
            items[n++] = host_items[i];
        }
    }
    n = append_plane_items(items, n, 128, MAX_ADDED_PROPS_PER_MERGE,
                           g_disc.overlay_plane_id, pprops, g_disc.crtc_id, g_test_fb_id,
                           (int32_t)rx, (int32_t)ry, rw, rh, 0, 0, rw << 16, rh << 16, true);

    uint64_t t0 = 0, dur_ns = 0;
    int err = 0;
    int ret = commit_items_request(fd, DRM_MODE_ATOMIC_TEST_ONLY, NULL, items, n, &t0, &dur_ns, &err);
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

    // 3. Commit count & item count change detection
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

    bool is_nonblock = (flags & DRM_MODE_ATOMIC_NONBLOCK) != 0;
    bool is_live_frame = (call_site == CALL_SITE_FRAME) && !(flags & DRM_MODE_ATOMIC_TEST_ONLY);
    bool merge_eligible = is_live_frame && !is_nonblock;
    bool complete = (track != NULL) && (cursor >= 0 ? ((uint32_t)cursor == recorded_count) : true);
    bool safe_to_merge = complete && (item_count <= MAX_ITEMS_PER_REQ) && (recorded_count <= MAX_ITEMS_PER_REQ);

    // 4. Page Guard mode (runs on all live host frame commits, including NONBLOCK; never on TEST_ONLY or cursor commits)
    bool guard_on = atomic_load_explicit(&g_takeover_enabled, memory_order_acquire);
    bool s3_active = !guard_on && atomic_load_explicit(&g_stage3_enabled, memory_order_acquire);
    bool s3_plane_active = atomic_load_explicit(&g_plane_active, memory_order_acquire);
    bool need_disable = (!s3_active && !guard_on && s3_plane_active);

    bool nonblock_skipped = is_live_frame && is_nonblock && (s3_active || need_disable);
    if (nonblock_skipped) {
        atomic_fetch_add_explicit(&g_nonblock_skipped, 1, memory_order_relaxed);
    }

    if (is_live_frame && guard_on) {
        if (g_layer_block == NULL) {
            ensure_layer_block_mapped(true);
        }
        if (g_layer_block != NULL && layer_block_valid(g_layer_block)) {
            layer_block_set_heartbeat(g_layer_block, shim_monotonic_ns(), 1u);
        }

        LayerFields snap;
        memset(&snap, 0, sizeof(snap));
        bool has_snap = (g_layer_block != NULL && layer_block_read(g_layer_block, &snap));

        bool need_overlay_disc = (!has_snap || snap.plane_id == 0);
        discover_display_once(fd, track ? track->items : NULL, recorded_count, need_overlay_disc);

        uint32_t host_fb_id = 0;
        const PlanePropIds *pprops_primary = (g_disc.primary_plane_id != 0) ? get_plane_props(fd, g_disc.primary_plane_id) : NULL;
        if (track && pprops_primary && pprops_primary->fb_id) {
            for (uint32_t i = 0; i < recorded_count; i++) {
                if (track->items[i].object_id == g_disc.primary_plane_id &&
                    track->items[i].property_id == pprops_primary->fb_id) {
                    host_fb_id = (uint32_t)track->items[i].value;
                    break;
                }
            }
        }

        if (host_fb_id != 0 && has_snap && snap.enabled != 0) {
            const HostFbCache *hfb = get_or_map_host_fb(fd, host_fb_id);
            if (!hfb) {
                atomic_fetch_add_explicit(&g_guard_map_errors, 1, memory_order_relaxed);
            } else {
                int rot = (snap.rotation == 0 || snap.rotation == 90 || snap.rotation == 180 || snap.rotation == 270)
                          ? (int)snap.rotation
                          : ((g_disc.panel_w == 800 && g_disc.panel_h == 1280) ? 270 : 90);
                int lx = 0, ly = 0, rx = 0, ry = 0;
                const bool valid_page = snap.page_id >= 0 && (unsigned)snap.page_id < sizeof(shim_marker_centers) / sizeof(shim_marker_centers[0]);
                const int *centers = shim_marker_centers[valid_page ? snap.page_id : 0];
                ui_to_panel(rot, centers[0], centers[1], (int)hfb->width, (int)hfb->height, &lx, &ly);
                ui_to_panel(rot, centers[2], centers[3], (int)hfb->width, (int)hfb->height, &rx, &ry);

                uint32_t c_left = 0, c_right = 0;
                bool ok_l = read_host_fb_pixel(hfb, lx, ly, &c_left);
                bool ok_r = read_host_fb_pixel(hfb, rx, ry, &c_right);

                if (!ok_l || !ok_r) {
                    atomic_fetch_add_explicit(&g_guard_map_errors, 1, memory_order_relaxed);
                } else {
                    atomic_fetch_add_explicit(&g_guard_checked, 1, memory_order_relaxed);

                    bool match_cur = valid_page &&
                                     color_close(c_left, snap.expected_marker_l) &&
                                     color_close(c_right, snap.expected_marker_r);

                    if (match_cur) {
                        atomic_fetch_add_explicit(&g_guard_match, 1, memory_order_relaxed);
                    } else {
                        bool match_other = shim_view_index(c_left, c_right) >= 0;
                        if (match_other) {
                            atomic_fetch_add_explicit(&g_guard_mismatch, 1, memory_order_relaxed);
                        } else {
                            atomic_fetch_add_explicit(&g_guard_unknown, 1, memory_order_relaxed);
                        }

                        uint32_t ov_id = (snap.plane_id != 0) ? snap.plane_id : g_disc.overlay_plane_id;
                        const PlanePropIds *pprops_ov = (ov_id != 0) ? get_plane_props(fd, ov_id) : NULL;

                        if (pprops_ov && pprops_ov->resolved && safe_to_merge) {
                            static __thread ShimPropItem tl_guard_items[128];
                            uint32_t merged_count = 0;
                            for (uint32_t i = 0; i < recorded_count && merged_count < 128; i++) {
                                tl_guard_items[merged_count++] = track->items[i];
                            }
                            merged_count = append_plane_items(
                                tl_guard_items, merged_count, 128, MAX_ADDED_PROPS_PER_MERGE,
                                ov_id, pprops_ov, 0, 0,
                                0, 0, 0, 0, 0, 0, 0, 0, false);

                            uint64_t t_start = 0, duration_ns = 0;
                            int err = 0;
                            pid_t tid = shim_gettid();
                            int ret = commit_items_request(fd, flags, user_data, tl_guard_items, merged_count,
                                                           &t_start, &duration_ns, &err);
                            if (ret < 0) {
                                atomic_fetch_add_explicit(&g_takeover_fallbacks, 1, memory_order_relaxed);
                                uint32_t consec = atomic_fetch_add_explicit(&g_takeover_consec_failures, 1, memory_order_acq_rel) + 1;
                                bool fatal = (err != EBUSY) || (consec >= MAX_TAKEOVER_CONSEC_FAILURES);
                                if (fatal) {
                                    layer_block_set_active(g_layer_block, 0u);
                                    atomic_store_explicit(&g_takeover_enabled, false, memory_order_release);
                                    atomic_store_explicit(&g_takeover_failed, true, memory_order_release);
                                    unlink(takeover_flag_path());

                                    char msg[160];
                                    snprintf(msg, sizeof(msg),
                                             "[PAGE_GUARD] Merged commit failed (ret=%d, errno=%d, consec=%u), guard disabled, hook_active=0",
                                             ret, err, consec);
                                    ring_enqueue_msg(msg);
                                }

                                uint64_t orig_t0 = shim_monotonic_ns();
                                int orig_ret = real_drmModeAtomicCommit(fd, req, flags, user_data);
                                uint64_t orig_t1 = shim_monotonic_ns();
                                int orig_err = (orig_ret < 0) ? errno : 0;
                                ring_enqueue_commit(orig_t0, tid, flags, item_count, user_data != NULL,
                                                    orig_ret, orig_err, call_site, orig_t1 - orig_t0,
                                                    false, true, false, include_items,
                                                    track ? track->items : NULL, recorded_count);
                                ensure_logger_started();
                                if (orig_ret < 0) errno = orig_err;
                                return orig_ret;
                            }

                            atomic_store_explicit(&g_takeover_consec_failures, 0, memory_order_release);
                            atomic_fetch_add_explicit(&g_plane_off_added, 1, memory_order_relaxed);
                            layer_block_set_guard_plane_off(g_layer_block, 1u);
                            ring_enqueue_commit(t_start, tid, flags, merged_count, user_data != NULL,
                                                ret, err, call_site, duration_ns, true, false, false,
                                                include_items, tl_guard_items, merged_count);
                            ensure_logger_started();
                            return ret;
                        }
                    }
                }
            }
        }
    }

    // 5. Stage 3 live merge (test rectangle on discovered overlay plane, when takeover is off)
    bool do_s3_merge = false;
    uint32_t s3_rx = 0, s3_ry = 0, s3_rw = 0, s3_rh = 0;
    const PlanePropIds *s3_props = NULL;
    if (merge_eligible && (s3_active || need_disable)) {
        if (safe_to_merge) {
            discover_display_once(fd, track ? track->items : NULL, recorded_count, true);
            if (g_disc.overlay_plane_id != 0 && g_disc.crtc_id != 0) {
                compute_test_rect(g_disc.panel_w, g_disc.panel_h, &s3_rx, &s3_ry, &s3_rw, &s3_rh);
                if (s3_active) {
                    ensure_test_fb(fd, s3_rw, s3_rh);
                }
                s3_props = get_plane_props(fd, g_disc.overlay_plane_id);
                if (s3_props && s3_props->resolved && (need_disable || g_test_fb_id != 0)) {
                    do_s3_merge = true;
                }
            }
        } else {
            atomic_fetch_add_explicit(&g_stage3_skipped_merges, 1, memory_order_relaxed);
        }
    }

    if (do_s3_merge) {
        static __thread ShimPropItem tl_merged_items[128];
        uint32_t merged_item_count = 0;
        for (uint32_t i = 0; i < recorded_count && merged_item_count < 128; i++) {
            tl_merged_items[merged_item_count++] = track->items[i];
        }
        merged_item_count = append_plane_items(
            tl_merged_items, merged_item_count, 128, MAX_ADDED_PROPS_PER_MERGE,
            g_disc.overlay_plane_id, s3_props, g_disc.crtc_id, g_test_fb_id,
            (int32_t)s3_rx, (int32_t)s3_ry, s3_rw, s3_rh,
            0, 0, s3_rw << 16, s3_rh << 16, s3_active);

        uint64_t t_start = 0, duration_ns = 0;
        int err = 0;
        pid_t tid = shim_gettid();
        int ret = commit_items_request(fd, flags, user_data, tl_merged_items, merged_item_count,
                                       &t_start, &duration_ns, &err);
        if (ret < 0) {
            atomic_store_explicit(&g_stage3_enabled, false, memory_order_release);
            atomic_store_explicit(&g_stage3_failed, true, memory_order_release);
            unlink(DEFAULT_STAGE3_FLAG_PATH);
            atomic_fetch_add_explicit(&g_stage3_fallbacks, 1, memory_order_relaxed);

            if (need_disable) {
                uint32_t dconsec = atomic_fetch_add_explicit(&g_stage3_disable_consec_failures, 1, memory_order_acq_rel) + 1;
                if (dconsec >= MAX_TAKEOVER_CONSEC_FAILURES) {
                    atomic_store_explicit(&g_plane_active, false, memory_order_release);
                    ring_enqueue_msg("[STAGE3] Plane disable failed 10 consecutive times, stopping retries");
                }
            }

            char fallback_msg[160];
            snprintf(fallback_msg, sizeof(fallback_msg),
                     "[STAGE3] Merged commit failed (ret=%d, errno=%d), stage 3 disabled, fallback to original host request",
                     ret, err);
            ring_enqueue_msg(fallback_msg);

            uint64_t orig_t0 = shim_monotonic_ns();
            int orig_ret = real_drmModeAtomicCommit(fd, req, flags, user_data);
            uint64_t orig_t1 = shim_monotonic_ns();
            int orig_err = (orig_ret < 0) ? errno : 0;

            ring_enqueue_commit(orig_t0, tid, flags, item_count, user_data != NULL,
                                orig_ret, orig_err, call_site, orig_t1 - orig_t0, false, true, false,
                                include_items, track ? track->items : NULL, recorded_count);
            ensure_logger_started();
            if (orig_ret < 0) errno = orig_err;
            return orig_ret;
        }

        atomic_store_explicit(&g_stage3_disable_consec_failures, 0, memory_order_release);
        if (s3_active) {
            atomic_store_explicit(&g_plane_active, true, memory_order_release);
        } else if (need_disable) {
            atomic_store_explicit(&g_plane_active, false, memory_order_release);
            char msg[96];
            snprintf(msg, sizeof(msg), "[STAGE3] Plane %u disabled", g_disc.overlay_plane_id);
            ring_enqueue_msg(msg);
            if (!atomic_load_explicit(&g_stage2_enabled, memory_order_acquire) &&
                !atomic_load_explicit(&g_stage3_enabled, memory_order_acquire)) {
                destroy_test_fb(fd);
            }
        }

        ring_enqueue_commit(t_start, tid, flags, merged_item_count, user_data != NULL,
                            ret, err, call_site, duration_ns, true, false, false,
                            include_items, tl_merged_items, merged_item_count);
        ensure_logger_started();
        return ret;
    }

    // 6. Normal (non-merged) commit
    pid_t tid = shim_gettid();
    uint64_t t_start = shim_monotonic_ns();
    int ret = real_drmModeAtomicCommit(fd, req, flags, user_data);
    uint64_t t_end = shim_monotonic_ns();
    int err = (ret < 0) ? errno : 0;
    uint64_t duration_ns = t_end - t_start;

    if (!atomic_load_explicit(&g_stage2_enabled, memory_order_acquire) &&
        !atomic_load_explicit(&g_stage3_enabled, memory_order_acquire) &&
        !atomic_load_explicit(&g_plane_active, memory_order_acquire) &&
        g_test_fb_id != 0) {
        destroy_test_fb(fd);
    }

    ring_enqueue_commit(t_start, tid, flags, item_count, user_data != NULL,
                        ret, err, call_site, duration_ns, false, false, nonblock_skipped,
                        include_items, track ? track->items : NULL, recorded_count);

    ensure_logger_started();

    if (call_site == CALL_SITE_FRAME && !guard_on &&
        atomic_load_explicit(&g_stage2_enabled, memory_order_acquire)) {
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
    if (e->is_nonblock_skipped) {
        g_summary.nonblock_skipped++;
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

    char summary_line[1024];
    snprintf(summary_line, sizeof(summary_line),
             "[SUMMARY 5s] rate(frame)=%.1f/s rate(cursor)=%.1f/s rate(other)=%.1f/s rate(merged)=%.1f/s | tids={%s} | "
             "dur_ns min=%llu med=%llu p99=%llu max=%llu | "
             "merged_dur_ns min=%llu med=%llu p99=%llu max=%llu | "
             "fallbacks=%u nonblock_skipped=%u | "
             "guard_checked=%u guard_match=%u guard_mismatch=%u guard_unknown=%u guard_map_errors=%u plane_off_added=%u | "
             "ret={0:%u, err:%u}\n",
             rate_frame, rate_cursor, rate_other, rate_merged, tid_buf,
             (unsigned long long)min_dur, (unsigned long long)med_dur,
             (unsigned long long)p99_dur, (unsigned long long)max_dur,
             (unsigned long long)m_min_dur, (unsigned long long)m_med_dur,
             (unsigned long long)m_p99_dur, (unsigned long long)m_max_dur,
             g_summary.fallbacks, g_summary.nonblock_skipped,
             atomic_load_explicit(&g_guard_checked, memory_order_relaxed),
             atomic_load_explicit(&g_guard_match, memory_order_relaxed),
             atomic_load_explicit(&g_guard_mismatch, memory_order_relaxed),
             atomic_load_explicit(&g_guard_unknown, memory_order_relaxed),
             atomic_load_explicit(&g_guard_map_errors, memory_order_relaxed),
             atomic_load_explicit(&g_plane_off_added, memory_order_relaxed),
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
                               e->is_merged ? " (merged)" : (e->is_nonblock_skipped ? " (nonblock_skipped)" : ""),
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

static void poll_takeover_flag(void) {
    bool tk = (access(takeover_flag_path(), F_OK) == 0);
    bool was_on = atomic_load_explicit(&g_takeover_enabled, memory_order_acquire);
    if (!tk) {
        if (g_layer_block) {
            layer_block_set_active(g_layer_block, 0u);
        }
        atomic_store_explicit(&g_takeover_enabled, false, memory_order_release);
        atomic_store_explicit(&g_takeover_failed, false, memory_order_relaxed);
        atomic_store_explicit(&g_takeover_consec_failures, 0, memory_order_relaxed);
        if (was_on) {
            ring_enqueue_msg("[TAKEOVER] Disabled (hook_active=0)");
        }
    } else if (!atomic_load_explicit(&g_takeover_failed, memory_order_acquire)) {
        if (g_layer_block == NULL) {
            ensure_layer_block_mapped(true);
        }
        if (g_layer_block) {
            layer_block_set_active(g_layer_block, 1u);
        }
        atomic_store_explicit(&g_takeover_enabled, true, memory_order_release);
        if (!was_on) {
            ring_enqueue_msg("[TAKEOVER] Enabled");
        }
    }
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

    pthread_mutex_lock(&g_start_mutex);
    while (!atomic_load_explicit(&g_logger_started, memory_order_acquire)) {
        pthread_cond_wait(&g_start_cond, &g_start_mutex);
    }
    pthread_mutex_unlock(&g_start_mutex);

    FILE *fp = NULL;
    uint64_t last_summary_ns = shim_monotonic_ns();
    int tick = 0;

    while (!atomic_load_explicit(&g_logger_stop, memory_order_relaxed)) {
        struct timespec ts = { 0, 100000000L }; // 100 ms
        nanosleep(&ts, NULL);
        if (atomic_load_explicit(&g_logger_stop, memory_order_relaxed)) {
            break;
        }

        // Poll takeover flag every 100 ms for rapid on/off response
        poll_takeover_flag();

        if (++tick < 10) {
            continue;
        }
        tick = 0;

        bool s2 = (access(DEFAULT_STAGE2_FLAG_PATH, F_OK) == 0);
        atomic_store_explicit(&g_stage2_enabled, s2, memory_order_release);

        bool s3 = (access(DEFAULT_STAGE3_FLAG_PATH, F_OK) == 0);
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
    ensure_layer_block_mapped(true);
    if (g_layer_block) {
        layer_block_reset_hook(g_layer_block);
        if (access(takeover_flag_path(), F_OK) == 0) {
            layer_block_set_active(g_layer_block, 1u);
            atomic_store_explicit(&g_takeover_enabled, true, memory_order_release);
        }
    }
    pthread_create(&g_logger_thread, NULL, logger_thread_func, NULL);
}

__attribute__((destructor))
static void shim_destructor(void) {
    if (g_layer_block) {
        layer_block_set_active(g_layer_block, 0u);
    }
    atomic_store_explicit(&g_logger_stop, true, memory_order_release);
    ensure_logger_started();
    pthread_join(g_logger_thread, NULL);
    if (g_layer_block_owned && g_layer_block) {
        layer_block_unmap(g_layer_block);
        g_layer_block = NULL;
        g_layer_block_owned = false;
    }
    clear_all_host_fbs();
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
    atomic_store_explicit(&g_plane_active, false, memory_order_seq_cst);
    atomic_store_explicit(&g_stage3_disable_consec_failures, 0, memory_order_seq_cst);
    atomic_store_explicit(&g_stage3_fallbacks, 0, memory_order_seq_cst);
    atomic_store_explicit(&g_stage3_skipped_merges, 0, memory_order_seq_cst);

    atomic_store_explicit(&g_takeover_enabled, false, memory_order_seq_cst);
    atomic_store_explicit(&g_takeover_failed, false, memory_order_seq_cst);
    atomic_store_explicit(&g_takeover_consec_failures, 0, memory_order_seq_cst);
    atomic_store_explicit(&g_takeover_fallbacks, 0, memory_order_seq_cst);
    atomic_store_explicit(&g_nonblock_skipped, 0, memory_order_seq_cst);

    atomic_store_explicit(&g_guard_checked, 0, memory_order_seq_cst);
    atomic_store_explicit(&g_guard_match, 0, memory_order_seq_cst);
    atomic_store_explicit(&g_guard_mismatch, 0, memory_order_seq_cst);
    atomic_store_explicit(&g_guard_unknown, 0, memory_order_seq_cst);
    atomic_store_explicit(&g_guard_map_errors, 0, memory_order_seq_cst);
    atomic_store_explicit(&g_plane_off_added, 0, memory_order_seq_cst);
    clear_all_host_fbs();

    if (g_layer_block_owned && g_layer_block) {
        layer_block_unmap(g_layer_block);
    }
    g_layer_block = NULL;
    g_layer_block_owned = false;

    atomic_store_explicit(&g_logger_started, false, memory_order_seq_cst);
    atomic_store_explicit(&g_logger_stop, false, memory_order_seq_cst);
    g_test_fb_id = 0;
    g_test_dumb_handle = 0;
    g_test_dumb_size = 0;
    g_test_fb_w = 0;
    g_test_fb_h = 0;
    g_prev_frame_item_count = 0;
    g_has_prev_frame = false;
    memset(g_plane_cache, 0, sizeof(g_plane_cache));
    g_plane_cache_count = 0;
    memset(&g_disc, 0, sizeof(g_disc));
    g_disc.crtc_index = -1;
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

void shim_test_set_takeover(bool enabled) {
    if (!enabled) {
        if (g_layer_block && layer_block_valid(g_layer_block)) {
            layer_block_set_active(g_layer_block, 0u);
        }
        atomic_store_explicit(&g_takeover_enabled, false, memory_order_seq_cst);
        atomic_store_explicit(&g_takeover_failed, false, memory_order_seq_cst);
        atomic_store_explicit(&g_takeover_consec_failures, 0, memory_order_seq_cst);
    } else {
        atomic_store_explicit(&g_takeover_failed, false, memory_order_seq_cst);
        atomic_store_explicit(&g_takeover_consec_failures, 0, memory_order_seq_cst);
        if (g_layer_block && layer_block_valid(g_layer_block)) {
            layer_block_set_active(g_layer_block, 1u);
        }
        atomic_store_explicit(&g_takeover_enabled, true, memory_order_seq_cst);
    }
}

bool shim_test_is_takeover_enabled(void) {
    return atomic_load_explicit(&g_takeover_enabled, memory_order_seq_cst);
}

void shim_test_set_guard(bool enabled) {
    shim_test_set_takeover(enabled);
}

bool shim_test_is_guard_enabled(void) {
    return shim_test_is_takeover_enabled();
}

uint32_t shim_test_get_guard_checked(void) {
    return atomic_load_explicit(&g_guard_checked, memory_order_seq_cst);
}

uint32_t shim_test_get_guard_match(void) {
    return atomic_load_explicit(&g_guard_match, memory_order_seq_cst);
}

uint32_t shim_test_get_guard_mismatch(void) {
    return atomic_load_explicit(&g_guard_mismatch, memory_order_seq_cst);
}

uint32_t shim_test_get_guard_unknown(void) {
    return atomic_load_explicit(&g_guard_unknown, memory_order_seq_cst);
}

uint32_t shim_test_get_guard_map_errors(void) {
    return atomic_load_explicit(&g_guard_map_errors, memory_order_seq_cst);
}

uint32_t shim_test_get_plane_off_added(void) {
    return atomic_load_explicit(&g_plane_off_added, memory_order_seq_cst);
}

uint32_t shim_test_get_host_fb_cache_count(void) {
    uint32_t count = 0;
    for (int i = 0; i < MAX_HOST_FB_CACHE; i++) {
        if (g_host_fbs[i].fb_id != 0 && g_host_fbs[i].map_addr != NULL) {
            count++;
        }
    }
    return count;
}

bool shim_test_is_stage3_enabled(void) {
    return atomic_load_explicit(&g_stage3_enabled, memory_order_seq_cst);
}

bool shim_test_is_plane46_active(void) {
    return atomic_load_explicit(&g_plane_active, memory_order_seq_cst);
}

void shim_test_set_layer_block(LayerBlock *block) {
    if (g_layer_block_owned && g_layer_block) {
        layer_block_unmap(g_layer_block);
    }
    g_layer_block = block;
    g_layer_block_owned = false;
}

uint32_t shim_test_get_discovered_crtc(void) { return g_disc.crtc_id; }
uint32_t shim_test_get_discovered_primary_plane(void) { return g_disc.primary_plane_id; }
uint32_t shim_test_get_discovered_overlay_plane(void) { return g_disc.overlay_plane_id; }
uint32_t shim_test_get_discovered_panel_w(void) { return g_disc.panel_w; }
uint32_t shim_test_get_discovered_panel_h(void) { return g_disc.panel_h; }

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

uint32_t shim_test_get_nonblock_skipped(void) {
    return atomic_load_explicit(&g_nonblock_skipped, memory_order_seq_cst);
}
#endif
