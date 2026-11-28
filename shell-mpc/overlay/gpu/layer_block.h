// SynthyBoy - MPC overlay: shared memory block for main-layer atomic takeover between the GPU helper and the commit shim.
// Pure header (no libdrm, plain lock-free atomics; usable from both C11 and C++17).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__linux__)
#include <linux/futex.h>
#include <sys/syscall.h>
#endif

#ifdef __cplusplus
#include <atomic>
static_assert (std::atomic<uint64_t>::is_always_lock_free, "LayerBlock requires lock-free 64-bit atomics");
static_assert (sizeof (std::atomic<uint64_t>) == sizeof (uint64_t), "std::atomic<uint64_t> layout");
static_assert (sizeof (std::atomic<uint32_t>) == sizeof (uint32_t), "std::atomic<uint32_t> layout");
static_assert (sizeof (std::atomic<int32_t>) == sizeof (int32_t), "std::atomic<int32_t> layout");
#define SB_LB_ATOMIC(T) std::atomic<T>
#else
#include <stdbool.h>
#include <stdatomic.h>
_Static_assert (ATOMIC_LLONG_LOCK_FREE == 2, "LayerBlock requires lock-free 64-bit atomics");
_Static_assert (sizeof (_Atomic uint64_t) == sizeof (uint64_t), "_Atomic uint64_t layout");
_Static_assert (sizeof (_Atomic uint32_t) == sizeof (uint32_t), "_Atomic uint32_t layout");
_Static_assert (sizeof (_Atomic int32_t) == sizeof (int32_t), "_Atomic int32_t layout");
#define SB_LB_ATOMIC(T) _Atomic T
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define LAYER_BLOCK_MAGIC         0x53424C42u /* 'SBLB' */
#define LAYER_BLOCK_VERSION       3u
#define LAYER_BLOCK_DEFAULT_PATH  "/tmp/synthyboy-layer-block"
#define LAYER_BLOCK_PATH_ENV      "SYNTHYBOY_LAYER_BLOCK_PATH"
#define LAYER_BLOCK_HEARTBEAT_NS  250000000ULL /* 250 ms */

typedef struct LayerBlock {
    SB_LB_ATOMIC(uint32_t) magic;
    SB_LB_ATOMIC(uint32_t) version;
    SB_LB_ATOMIC(uint64_t) seq;
    SB_LB_ATOMIC(uint64_t) claimed_seq;
    SB_LB_ATOMIC(uint64_t) presented_seq;
    SB_LB_ATOMIC(uint64_t) hook_heartbeat_ns;
    SB_LB_ATOMIC(uint32_t) hook_active;
    SB_LB_ATOMIC(int32_t)  helper_pid;
    SB_LB_ATOMIC(uint32_t) plane_id;
    SB_LB_ATOMIC(uint32_t) crtc_id;
    SB_LB_ATOMIC(uint32_t) fb_id;
    SB_LB_ATOMIC(uint32_t) enabled;
    SB_LB_ATOMIC(int32_t)  crtc_x;
    SB_LB_ATOMIC(int32_t)  crtc_y;
    SB_LB_ATOMIC(uint32_t) crtc_w;
    SB_LB_ATOMIC(uint32_t) crtc_h;
    SB_LB_ATOMIC(uint32_t) src_x;
    SB_LB_ATOMIC(uint32_t) src_y;
    SB_LB_ATOMIC(uint32_t) src_w;
    SB_LB_ATOMIC(uint32_t) src_h;
    SB_LB_ATOMIC(int32_t)  page_id;
    SB_LB_ATOMIC(uint32_t) expected_marker_l;
    SB_LB_ATOMIC(uint32_t) expected_marker_r;
    SB_LB_ATOMIC(uint32_t) rotation;
    SB_LB_ATOMIC(uint32_t) guard_plane_off;
    uint32_t reserved[3];
} LayerBlock;

typedef struct LayerFields {
    int32_t  helper_pid;
    uint32_t plane_id;
    uint32_t crtc_id;
    uint32_t fb_id;
    int32_t  crtc_x;
    int32_t  crtc_y;
    uint32_t crtc_w;
    uint32_t crtc_h;
    uint32_t src_x;
    uint32_t src_y;
    uint32_t src_w;
    uint32_t src_h;
    uint32_t enabled;
    uint64_t seq;
    int32_t  page_id;
    uint32_t expected_marker_l;
    uint32_t expected_marker_r;
    uint32_t rotation;
} LayerFields;

#ifdef __cplusplus
static inline uint32_t sb_lb_load_u32 (const SB_LB_ATOMIC(uint32_t)* p, std::memory_order ord) { return p->load (ord); }
static inline void     sb_lb_store_u32 (SB_LB_ATOMIC(uint32_t)* p, uint32_t v, std::memory_order ord) { p->store (v, ord); }
static inline int32_t  sb_lb_load_i32 (const SB_LB_ATOMIC(int32_t)* p, std::memory_order ord) { return p->load (ord); }
static inline void     sb_lb_store_i32 (SB_LB_ATOMIC(int32_t)* p, int32_t v, std::memory_order ord) { p->store (v, ord); }
static inline uint64_t sb_lb_load_u64 (const SB_LB_ATOMIC(uint64_t)* p, std::memory_order ord) { return p->load (ord); }
static inline void     sb_lb_store_u64 (SB_LB_ATOMIC(uint64_t)* p, uint64_t v, std::memory_order ord) { p->store (v, ord); }
static inline bool     sb_lb_cas_u64 (SB_LB_ATOMIC(uint64_t)* p, uint64_t* expected, uint64_t desired)
{
    return p->compare_exchange_strong (*expected, desired, std::memory_order_acq_rel, std::memory_order_acquire);
}
static inline void     sb_lb_fence_acquire (void) { std::atomic_thread_fence (std::memory_order_acquire); }
static inline void     sb_lb_fence_release (void) { std::atomic_thread_fence (std::memory_order_release); }
#define SB_MO_RELAXED std::memory_order_relaxed
#define SB_MO_ACQUIRE std::memory_order_acquire
#define SB_MO_RELEASE std::memory_order_release
#else
static inline uint32_t sb_lb_load_u32 (const SB_LB_ATOMIC(uint32_t)* p, memory_order ord) { return atomic_load_explicit ((SB_LB_ATOMIC(uint32_t)*)p, ord); }
static inline void     sb_lb_store_u32 (SB_LB_ATOMIC(uint32_t)* p, uint32_t v, memory_order ord) { atomic_store_explicit (p, v, ord); }
static inline int32_t  sb_lb_load_i32 (const SB_LB_ATOMIC(int32_t)* p, memory_order ord) { return atomic_load_explicit ((SB_LB_ATOMIC(int32_t)*)p, ord); }
static inline void     sb_lb_store_i32 (SB_LB_ATOMIC(int32_t)* p, int32_t v, memory_order ord) { atomic_store_explicit (p, v, ord); }
static inline uint64_t sb_lb_load_u64 (const SB_LB_ATOMIC(uint64_t)* p, memory_order ord) { return atomic_load_explicit ((SB_LB_ATOMIC(uint64_t)*)p, ord); }
static inline void     sb_lb_store_u64 (SB_LB_ATOMIC(uint64_t)* p, uint64_t v, memory_order ord) { atomic_store_explicit (p, v, ord); }
static inline bool     sb_lb_cas_u64 (SB_LB_ATOMIC(uint64_t)* p, uint64_t* expected, uint64_t desired)
{
    return atomic_compare_exchange_strong_explicit (p, expected, desired, memory_order_acq_rel, memory_order_acquire);
}
static inline void     sb_lb_fence_acquire (void) { atomic_thread_fence (memory_order_acquire); }
static inline void     sb_lb_fence_release (void) { atomic_thread_fence (memory_order_release); }
#define SB_MO_RELAXED memory_order_relaxed
#define SB_MO_ACQUIRE memory_order_acquire
#define SB_MO_RELEASE memory_order_release
#endif

static inline const char* layer_block_path (void)
{
    const char* env = getenv (LAYER_BLOCK_PATH_ENV);
    return (env != NULL && env[0] != '\0') ? env : LAYER_BLOCK_DEFAULT_PATH;
}

static inline bool layer_block_valid (const LayerBlock* b)
{
    if (b == NULL) return false;
    return sb_lb_load_u32 (&b->magic, SB_MO_ACQUIRE) == LAYER_BLOCK_MAGIC &&
           sb_lb_load_u32 (&b->version, SB_MO_ACQUIRE) == LAYER_BLOCK_VERSION;
}

static inline void layer_block_init (LayerBlock* b)
{
    if (b == NULL) return;
    sb_lb_store_u32 (&b->magic, 0u, SB_MO_RELAXED);
    sb_lb_store_u32 (&b->version, LAYER_BLOCK_VERSION, SB_MO_RELAXED);
    sb_lb_store_u64 (&b->seq, 0ULL, SB_MO_RELAXED);
    sb_lb_store_u64 (&b->claimed_seq, 0ULL, SB_MO_RELAXED);
    sb_lb_store_u64 (&b->presented_seq, 0ULL, SB_MO_RELAXED);
    sb_lb_store_u64 (&b->hook_heartbeat_ns, 0ULL, SB_MO_RELAXED);
    sb_lb_store_u32 (&b->hook_active, 0u, SB_MO_RELAXED);
    sb_lb_store_i32 (&b->helper_pid, 0, SB_MO_RELAXED);
    sb_lb_store_u32 (&b->plane_id, 0u, SB_MO_RELAXED);
    sb_lb_store_u32 (&b->crtc_id, 0u, SB_MO_RELAXED);
    sb_lb_store_u32 (&b->fb_id, 0u, SB_MO_RELAXED);
    sb_lb_store_u32 (&b->enabled, 0u, SB_MO_RELAXED);
    sb_lb_store_i32 (&b->crtc_x, 0, SB_MO_RELAXED);
    sb_lb_store_i32 (&b->crtc_y, 0, SB_MO_RELAXED);
    sb_lb_store_u32 (&b->crtc_w, 0u, SB_MO_RELAXED);
    sb_lb_store_u32 (&b->crtc_h, 0u, SB_MO_RELAXED);
    sb_lb_store_u32 (&b->src_x, 0u, SB_MO_RELAXED);
    sb_lb_store_u32 (&b->src_y, 0u, SB_MO_RELAXED);
    sb_lb_store_u32 (&b->src_w, 0u, SB_MO_RELAXED);
    sb_lb_store_u32 (&b->src_h, 0u, SB_MO_RELAXED);
    sb_lb_store_i32 (&b->page_id, -1, SB_MO_RELAXED);
    sb_lb_store_u32 (&b->expected_marker_l, 0u, SB_MO_RELAXED);
    sb_lb_store_u32 (&b->expected_marker_r, 0u, SB_MO_RELAXED);
    sb_lb_store_u32 (&b->rotation, 270u, SB_MO_RELAXED);
    sb_lb_store_u32 (&b->guard_plane_off, 0u, SB_MO_RELAXED);
    memset (b->reserved, 0, sizeof (b->reserved));
    sb_lb_store_u32 (&b->magic, LAYER_BLOCK_MAGIC, SB_MO_RELEASE);
}

static inline void layer_block_reset_hook (LayerBlock* b)
{
    if (! layer_block_valid (b)) return;
    sb_lb_store_u32 (&b->hook_active, 0u, SB_MO_RELAXED);
    sb_lb_store_u64 (&b->hook_heartbeat_ns, 0ULL, SB_MO_RELAXED);
    sb_lb_store_u64 (&b->claimed_seq, 0ULL, SB_MO_RELAXED);
    sb_lb_store_u64 (&b->presented_seq, 0ULL, SB_MO_RELAXED);
    sb_lb_store_u32 (&b->guard_plane_off, 0u, SB_MO_RELAXED);
    sb_lb_store_u64 (&b->seq, 0ULL, SB_MO_RELEASE);
}

static inline uint64_t layer_block_publish (LayerBlock* b, const LayerFields* f)
{
    if (! layer_block_valid (b) || f == NULL) return 0ULL;

    /* Seqlock writer: acquire an even seq and transition it to odd (s + 1) while writing. */
    uint64_t s = sb_lb_load_u64 (&b->seq, SB_MO_ACQUIRE);
    for (;;)
    {
        if (s & 1ULL)
        {
            s = sb_lb_load_u64 (&b->seq, SB_MO_ACQUIRE);
            continue;
        }
        if (sb_lb_cas_u64 (&b->seq, &s, s + 1ULL))
            break;
    }

    /* Release fence: the CAS acq_rel orders prior memory accesses before the seq store,
       but does NOT order subsequent stores. On weakly-ordered architectures (such as ARMv8),
       without this release fence the subsequent relaxed data stores could be reordered and
       become visible before the store that makes seq odd, allowing a concurrent reader
       to observe updated fields with the previous even seq and accept a torn snapshot. */
    sb_lb_fence_release ();

    sb_lb_store_i32 (&b->helper_pid, f->helper_pid, SB_MO_RELAXED);
    sb_lb_store_u32 (&b->plane_id, f->plane_id, SB_MO_RELAXED);
    sb_lb_store_u32 (&b->crtc_id, f->crtc_id, SB_MO_RELAXED);
    sb_lb_store_u32 (&b->fb_id, f->fb_id, SB_MO_RELAXED);
    sb_lb_store_i32 (&b->crtc_x, f->crtc_x, SB_MO_RELAXED);
    sb_lb_store_i32 (&b->crtc_y, f->crtc_y, SB_MO_RELAXED);
    sb_lb_store_u32 (&b->crtc_w, f->crtc_w, SB_MO_RELAXED);
    sb_lb_store_u32 (&b->crtc_h, f->crtc_h, SB_MO_RELAXED);
    sb_lb_store_u32 (&b->src_x, f->src_x, SB_MO_RELAXED);
    sb_lb_store_u32 (&b->src_y, f->src_y, SB_MO_RELAXED);
    sb_lb_store_u32 (&b->src_w, f->src_w, SB_MO_RELAXED);
    sb_lb_store_u32 (&b->src_h, f->src_h, SB_MO_RELAXED);
    sb_lb_store_u32 (&b->enabled, f->enabled, SB_MO_RELAXED);
    sb_lb_store_i32 (&b->page_id, f->page_id, SB_MO_RELAXED);
    sb_lb_store_u32 (&b->expected_marker_l, f->expected_marker_l, SB_MO_RELAXED);
    sb_lb_store_u32 (&b->expected_marker_r, f->expected_marker_r, SB_MO_RELAXED);
    sb_lb_store_u32 (&b->rotation, f->rotation, SB_MO_RELAXED);

    const uint64_t done_seq = s + 2ULL;
    sb_lb_fence_release ();
    sb_lb_store_u64 (&b->seq, done_seq, SB_MO_RELEASE);
    return done_seq;
}

static inline bool layer_block_read (const LayerBlock* b, LayerFields* out)
{
    if (! layer_block_valid (b) || out == NULL) return false;

    for (int attempt = 0; attempt < 16; ++attempt)
    {
        const uint64_t s1 = sb_lb_load_u64 (&b->seq, SB_MO_ACQUIRE);
        if (s1 == 0ULL || (s1 & 1ULL) != 0ULL)
            return false;

        LayerFields snap;
        snap.helper_pid        = sb_lb_load_i32 (&b->helper_pid, SB_MO_RELAXED);
        snap.plane_id          = sb_lb_load_u32 (&b->plane_id, SB_MO_RELAXED);
        snap.crtc_id           = sb_lb_load_u32 (&b->crtc_id, SB_MO_RELAXED);
        snap.fb_id             = sb_lb_load_u32 (&b->fb_id, SB_MO_RELAXED);
        snap.crtc_x            = sb_lb_load_i32 (&b->crtc_x, SB_MO_RELAXED);
        snap.crtc_y            = sb_lb_load_i32 (&b->crtc_y, SB_MO_RELAXED);
        snap.crtc_w            = sb_lb_load_u32 (&b->crtc_w, SB_MO_RELAXED);
        snap.crtc_h            = sb_lb_load_u32 (&b->crtc_h, SB_MO_RELAXED);
        snap.src_x             = sb_lb_load_u32 (&b->src_x, SB_MO_RELAXED);
        snap.src_y             = sb_lb_load_u32 (&b->src_y, SB_MO_RELAXED);
        snap.src_w             = sb_lb_load_u32 (&b->src_w, SB_MO_RELAXED);
        snap.src_h             = sb_lb_load_u32 (&b->src_h, SB_MO_RELAXED);
        snap.enabled           = sb_lb_load_u32 (&b->enabled, SB_MO_RELAXED);
        snap.page_id           = sb_lb_load_i32 (&b->page_id, SB_MO_RELAXED);
        snap.expected_marker_l = sb_lb_load_u32 (&b->expected_marker_l, SB_MO_RELAXED);
        snap.expected_marker_r = sb_lb_load_u32 (&b->expected_marker_r, SB_MO_RELAXED);
        snap.rotation          = sb_lb_load_u32 (&b->rotation, SB_MO_RELAXED);
        snap.seq               = s1;

        sb_lb_fence_acquire ();
        const uint64_t s2 = sb_lb_load_u64 (&b->seq, SB_MO_ACQUIRE);
        if (s1 == s2)
        {
            *out = snap;
            return true;
        }
    }
    return false;
}

static inline bool layer_block_try_claim (LayerBlock* b, LayerFields* out)
{
    if (! layer_block_valid (b) || out == NULL) return false;

    for (int attempt = 0; attempt < 16; ++attempt)
    {
        const uint64_t s1 = sb_lb_load_u64 (&b->seq, SB_MO_ACQUIRE);
        if (s1 == 0ULL || (s1 & 1ULL) != 0ULL)
            return false;

        const uint64_t claimed = sb_lb_load_u64 (&b->claimed_seq, SB_MO_ACQUIRE);
        if (s1 <= claimed)
            return false;

        LayerFields snap;
        snap.helper_pid        = sb_lb_load_i32 (&b->helper_pid, SB_MO_RELAXED);
        snap.plane_id          = sb_lb_load_u32 (&b->plane_id, SB_MO_RELAXED);
        snap.crtc_id           = sb_lb_load_u32 (&b->crtc_id, SB_MO_RELAXED);
        snap.fb_id             = sb_lb_load_u32 (&b->fb_id, SB_MO_RELAXED);
        snap.crtc_x            = sb_lb_load_i32 (&b->crtc_x, SB_MO_RELAXED);
        snap.crtc_y            = sb_lb_load_i32 (&b->crtc_y, SB_MO_RELAXED);
        snap.crtc_w            = sb_lb_load_u32 (&b->crtc_w, SB_MO_RELAXED);
        snap.crtc_h            = sb_lb_load_u32 (&b->crtc_h, SB_MO_RELAXED);
        snap.src_x             = sb_lb_load_u32 (&b->src_x, SB_MO_RELAXED);
        snap.src_y             = sb_lb_load_u32 (&b->src_y, SB_MO_RELAXED);
        snap.src_w             = sb_lb_load_u32 (&b->src_w, SB_MO_RELAXED);
        snap.src_h             = sb_lb_load_u32 (&b->src_h, SB_MO_RELAXED);
        snap.enabled           = sb_lb_load_u32 (&b->enabled, SB_MO_RELAXED);
        snap.page_id           = sb_lb_load_i32 (&b->page_id, SB_MO_RELAXED);
        snap.expected_marker_l = sb_lb_load_u32 (&b->expected_marker_l, SB_MO_RELAXED);
        snap.expected_marker_r = sb_lb_load_u32 (&b->expected_marker_r, SB_MO_RELAXED);
        snap.rotation          = sb_lb_load_u32 (&b->rotation, SB_MO_RELAXED);
        snap.seq               = s1;

        sb_lb_fence_acquire ();
        const uint64_t s2 = sb_lb_load_u64 (&b->seq, SB_MO_ACQUIRE);
        if (s1 != s2)
            continue;

        uint64_t expected = claimed;
        while (expected < s1)
        {
            if (sb_lb_cas_u64 (&b->claimed_seq, &expected, s1))
            {
                *out = snap;
                return true;
            }
        }
        return false;
    }
    return false;
}

static inline uint32_t layer_block_guard_plane_off (const LayerBlock* b)
{
    if (! layer_block_valid (b)) return 0u;
    return sb_lb_load_u32 (&b->guard_plane_off, SB_MO_ACQUIRE);
}

/* Wakes whoever waits (futex, shared mapping: no FUTEX_PRIVATE_FLAG) on the guard flag word: the GPU helper sleeps on it instead of polling the flag (g133: the flag was read 24.9 ms median / 100 ms p90 after the host's
   flip because the helper only looked when a message or its timeout woke it). No-op where there is no futex (the Mac tests); an old shim that does not wake leaves the helper's slow timeout as the fallback. */
static inline void layer_block_wake_guard_waiters (LayerBlock* b)
{
#if defined(__linux__)
    syscall (SYS_futex, (uint32_t*) (void*) &b->guard_plane_off, FUTEX_WAKE, 0x7fffffff, 0, 0, 0);
#else
    (void) b;
#endif
}

static inline void layer_block_set_guard_plane_off (LayerBlock* b, uint32_t val)
{
    if (! layer_block_valid (b)) return;
    sb_lb_store_u32 (&b->guard_plane_off, val, SB_MO_RELEASE);
    if (val != 0u)
        layer_block_wake_guard_waiters (b);
}

static inline bool layer_block_hook_alive (const LayerBlock* b, uint64_t now_ns, uint64_t timeout_ns)
{
    if (! layer_block_valid (b)) return false;
    if (sb_lb_load_u32 (&b->hook_active, SB_MO_ACQUIRE) == 0u) return false;
    const uint64_t hb = sb_lb_load_u64 (&b->hook_heartbeat_ns, SB_MO_ACQUIRE);
    if (hb == 0ULL) return false;
    if (now_ns < hb) return true;
    return (now_ns - hb) <= timeout_ns;
}

static inline void layer_block_set_heartbeat (LayerBlock* b, uint64_t now_ns, uint32_t active)
{
    if (! layer_block_valid (b)) return;
    sb_lb_store_u64 (&b->hook_heartbeat_ns, now_ns, SB_MO_RELEASE);
    sb_lb_store_u32 (&b->hook_active, active, SB_MO_RELEASE);
}

static inline void layer_block_set_active (LayerBlock* b, uint32_t active)
{
    if (! layer_block_valid (b)) return;
    sb_lb_store_u32 (&b->hook_active, active, SB_MO_RELEASE);
}

static inline uint64_t layer_block_claimed_seq (const LayerBlock* b)
{
    if (! layer_block_valid (b)) return 0ULL;
    return sb_lb_load_u64 (&b->claimed_seq, SB_MO_ACQUIRE);
}

static inline uint64_t layer_block_presented_seq (const LayerBlock* b)
{
    if (! layer_block_valid (b)) return 0ULL;
    return sb_lb_load_u64 (&b->presented_seq, SB_MO_ACQUIRE);
}

static inline void layer_block_set_presented_seq (LayerBlock* b, uint64_t seq)
{
    if (! layer_block_valid (b)) return;
    sb_lb_store_u64 (&b->presented_seq, seq, SB_MO_RELEASE);
}

/** Opens and mmaps the shared LayerBlock file. When `create` is true, creates/extends the file and initialises the header if needed. */
static inline LayerBlock* layer_block_map (const char* path, bool create)
{
    if (path == NULL || path[0] == '\0')
        path = layer_block_path ();

    int flags = O_RDWR | O_CLOEXEC | O_NOFOLLOW;
    if (create)
        flags |= O_CREAT;

    int fd = open (path, flags, 0644);
    if (fd < 0)
        return NULL;

    struct stat st;
    if (fstat (fd, &st) != 0 || ! S_ISREG (st.st_mode))
    {
        close (fd);
        return NULL;
    }

    const off_t need = (off_t)sizeof (LayerBlock);
    bool newly_sized = false;
    if (st.st_size < need)
    {
        if (! create || ftruncate (fd, need) != 0)
        {
            close (fd);
            return NULL;
        }
        newly_sized = true;
    }

    void* p = mmap (NULL, sizeof (LayerBlock), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close (fd);
    if (p == MAP_FAILED)
        return NULL;

    LayerBlock* b = (LayerBlock*)p;
    const uint32_t magic = sb_lb_load_u32 (&b->magic, SB_MO_ACQUIRE);
    const uint32_t ver   = sb_lb_load_u32 (&b->version, SB_MO_ACQUIRE);
    if (magic == LAYER_BLOCK_MAGIC && ver != LAYER_BLOCK_VERSION)
    {
        munmap (p, sizeof (LayerBlock));
        return NULL;
    }

    if (create && (newly_sized || ! layer_block_valid (b)))
        layer_block_init (b);
    else if (! create && ! layer_block_valid (b))
    {
        munmap (p, sizeof (LayerBlock));
        return NULL;
    }
    return b;
}

static inline void layer_block_unmap (LayerBlock* b)
{
    if (b != NULL)
        munmap ((void*)b, sizeof (LayerBlock));
}

#ifdef __cplusplus
} // extern "C"

namespace synthyboy_mpc { namespace overlay { namespace gpu {

inline uint64_t publish (LayerBlock* b, const LayerFields& f) { return layer_block_publish (b, &f); }
inline bool read (const LayerBlock* b, LayerFields& out) { return layer_block_read (b, &out); }
inline bool try_claim (LayerBlock* b, LayerFields& out) { return layer_block_try_claim (b, &out); }
inline uint32_t guard_plane_off (const LayerBlock* b) { return layer_block_guard_plane_off (b); }
inline void set_guard_plane_off (LayerBlock* b, uint32_t val) { layer_block_set_guard_plane_off (b, val); }
inline bool hook_alive (const LayerBlock* b, uint64_t now_ns, uint64_t timeout_ns = LAYER_BLOCK_HEARTBEAT_NS)
{
    return layer_block_hook_alive (b, now_ns, timeout_ns);
}
inline uint64_t claimed_seq (const LayerBlock* b) { return layer_block_claimed_seq (b); }
inline uint64_t presented_seq (const LayerBlock* b) { return layer_block_presented_seq (b); }
inline void set_presented_seq (LayerBlock* b, uint64_t seq) { layer_block_set_presented_seq (b, seq); }

}}} // namespace synthyboy_mpc::overlay::gpu
#endif

#undef SB_LB_ATOMIC
#undef SB_MO_RELAXED
#undef SB_MO_ACQUIRE
#undef SB_MO_RELEASE
