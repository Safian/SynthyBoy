// SynthyBoy - MPC XL atomic commit measurement shim host test.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "xl-commit-shim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>

#define MAX_FAKE_REQS 32
#define MAX_FAKE_PROPS 128
#define MAX_COMMIT_CALLS 64

typedef struct {
    int in_use;
    int cursor;
    ShimPropItem props[MAX_FAKE_PROPS];
} FakeReq;

typedef struct {
    int fd;
    uint32_t flags;
    void *user_data;
    int item_count;
    ShimPropItem props[MAX_FAKE_PROPS];
} CommitCall;

static FakeReq g_fake_reqs[MAX_FAKE_REQS];
static CommitCall g_fake_commits[MAX_COMMIT_CALLS];
static int g_fake_commit_count = 0;
static int g_fake_commit_ret = 0;
static int g_fake_commit_errno = 0;
static int g_fake_commit_rets[MAX_COMMIT_CALLS];
static int g_fake_commit_errs[MAX_COMMIT_CALLS];
static bool g_use_per_commit_ret = false;

static bool g_fake_destroy_dumb_called = false;
static uint32_t g_fake_destroyed_handle = 0;
static bool g_fake_rmfb_called = false;
static uint32_t g_fake_rmfb_id = 0;
static bool g_fake_mmap_called = false;
static bool g_fake_munmap_called = false;
static uint8_t g_fake_dumb_buf[400 * 300 * 4];

static void fake_reset(void) {
    memset(g_fake_reqs, 0, sizeof(g_fake_reqs));
    memset(g_fake_commits, 0, sizeof(g_fake_commits));
    memset(g_fake_commit_rets, 0, sizeof(g_fake_commit_rets));
    memset(g_fake_commit_errs, 0, sizeof(g_fake_commit_errs));
    memset(g_fake_dumb_buf, 0, sizeof(g_fake_dumb_buf));
    g_fake_commit_count = 0;
    g_fake_commit_ret = 0;
    g_fake_commit_errno = 0;
    g_use_per_commit_ret = false;
    g_fake_destroy_dumb_called = false;
    g_fake_destroyed_handle = 0;
    g_fake_rmfb_called = false;
    g_fake_rmfb_id = 0;
    g_fake_mmap_called = false;
    g_fake_munmap_called = false;
}

void *fake_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    (void)addr; (void)prot; (void)flags; (void)fd; (void)offset;
    assert(length <= sizeof(g_fake_dumb_buf));
    g_fake_mmap_called = true;
    return g_fake_dumb_buf;
}

int fake_munmap(void *addr, size_t length) {
    (void)addr; (void)length;
    g_fake_munmap_called = true;
    return 0;
}

int fake_drmModeRmFB(int fd, uint32_t bufferId) {
    (void)fd;
    g_fake_rmfb_called = true;
    g_fake_rmfb_id = bufferId;
    return 0;
}

drmModeAtomicReqPtr fake_drmModeAtomicAlloc(void) {
    for (int i = 0; i < MAX_FAKE_REQS; i++) {
        if (!g_fake_reqs[i].in_use) {
            g_fake_reqs[i].in_use = 1;
            g_fake_reqs[i].cursor = 0;
            return (drmModeAtomicReqPtr)&g_fake_reqs[i];
        }
    }
    return NULL;
}

void fake_drmModeAtomicFree(drmModeAtomicReqPtr req) {
    if (!req) return;
    FakeReq *r = (FakeReq *)req;
    r->in_use = 0;
    r->cursor = 0;
}

int fake_drmModeAtomicAddProperty(drmModeAtomicReqPtr req, uint32_t object_id, uint32_t property_id, uint64_t value) {
    if (!req) return -1;
    FakeReq *r = (FakeReq *)req;
    if (r->cursor >= MAX_FAKE_PROPS) return -1;
    r->props[r->cursor].object_id = object_id;
    r->props[r->cursor].property_id = property_id;
    r->props[r->cursor].value = value;
    r->cursor++;
    return r->cursor;
}

int fake_drmModeAtomicGetCursor(drmModeAtomicReqPtr req) {
    if (!req) return -1;
    FakeReq *r = (FakeReq *)req;
    return r->cursor;
}

int fake_drmModeAtomicCommit(int fd, drmModeAtomicReqPtr req, uint32_t flags, void *user_data) {
    int call_idx = g_fake_commit_count;
    if (g_fake_commit_count < MAX_COMMIT_CALLS) {
        CommitCall *c = &g_fake_commits[g_fake_commit_count++];
        c->fd = fd;
        c->flags = flags;
        c->user_data = user_data;
        if (req) {
            FakeReq *r = (FakeReq *)req;
            c->item_count = r->cursor;
            memcpy(c->props, r->props, r->cursor * sizeof(ShimPropItem));
        } else {
            c->item_count = 0;
        }
    }
    if (g_use_per_commit_ret) {
        if (g_fake_commit_rets[call_idx] < 0) {
            errno = g_fake_commit_errs[call_idx];
        }
        return g_fake_commit_rets[call_idx];
    }
    if (g_fake_commit_ret < 0) {
        errno = g_fake_commit_errno;
    }
    return g_fake_commit_ret;
}

static uint32_t g_plane46_prop_ids[] = {101, 102, 103, 104, 105, 106, 107, 108, 109, 110};
static const char *g_plane46_prop_names[] = {
    "FB_ID", "CRTC_ID", "CRTC_X", "CRTC_Y", "CRTC_W", "CRTC_H",
    "SRC_X", "SRC_Y", "SRC_W", "SRC_H"
};

static uint32_t g_plane34_prop_ids[] = {201, 202, 203, 204};
static const char *g_plane34_prop_names[] = {
    "FB_ID", "CRTC_ID", "CRTC_X", "CRTC_Y"
};

drmModeObjectPropertiesPtr fake_drmModeObjectGetProperties(int fd, uint32_t object_id, uint32_t object_type) {
    (void)fd;
    if (object_type != DRM_MODE_OBJECT_PLANE) return NULL;

    drmModeObjectPropertiesPtr props = calloc(1, sizeof(drmModeObjectProperties));
    if (object_id == 46) {
        props->count_props = 10;
        props->props = calloc(10, sizeof(uint32_t));
        memcpy(props->props, g_plane46_prop_ids, 10 * sizeof(uint32_t));
    } else if (object_id == 34) {
        props->count_props = 4;
        props->props = calloc(4, sizeof(uint32_t));
        memcpy(props->props, g_plane34_prop_ids, 4 * sizeof(uint32_t));
    } else {
        free(props);
        return NULL;
    }
    return props;
}

void fake_drmModeFreeObjectProperties(drmModeObjectPropertiesPtr ptr) {
    if (!ptr) return;
    free(ptr->props);
    free(ptr->prop_values);
    free(ptr);
}

drmModePropertyPtr fake_drmModeGetProperty(int fd, uint32_t propertyId) {
    (void)fd;
    for (int i = 0; i < 10; i++) {
        if (g_plane46_prop_ids[i] == propertyId) {
            drmModePropertyPtr p = calloc(1, sizeof(drmModePropertyRes));
            p->prop_id = propertyId;
            strncpy(p->name, g_plane46_prop_names[i], DRM_PROP_NAME_LEN - 1);
            return p;
        }
    }
    for (int i = 0; i < 4; i++) {
        if (g_plane34_prop_ids[i] == propertyId) {
            drmModePropertyPtr p = calloc(1, sizeof(drmModePropertyRes));
            p->prop_id = propertyId;
            strncpy(p->name, g_plane34_prop_names[i], DRM_PROP_NAME_LEN - 1);
            return p;
        }
    }
    return NULL;
}

void fake_drmModeFreeProperty(drmModePropertyPtr ptr) {
    free(ptr);
}

int fake_drmModeAddFB2(int fd, uint32_t width, uint32_t height, uint32_t pixel_format,
                      const uint32_t bo_handles[4], const uint32_t pitches[4],
                      const uint32_t offsets[4], uint32_t *buf_id, uint32_t flags) {
    (void)fd; (void)flags; (void)offsets; (void)pitches;
    assert(width == 400);
    assert(height == 300);
    assert(pixel_format == DRM_FORMAT_XRGB8888);
    assert(bo_handles[0] == 42);
    if (buf_id) *buf_id = 99;
    return 0;
}

int fake_drmIoctl(int fd, unsigned long request, void *arg) {
    (void)fd;
    if (request == DRM_IOCTL_MODE_CREATE_DUMB) {
        struct drm_mode_create_dumb *create = (struct drm_mode_create_dumb *)arg;
        assert(create->width == 400);
        assert(create->height == 300);
        assert(create->bpp == 32);
        create->handle = 42;
        create->pitch = 1600;
        create->size = 400 * 300 * 4;
        return 0;
    }
    if (request == DRM_IOCTL_MODE_MAP_DUMB) {
        struct drm_mode_map_dumb *map = (struct drm_mode_map_dumb *)arg;
        assert(map->handle == 42);
        map->offset = 0x1000;
        return 0;
    }
    if (request == DRM_IOCTL_MODE_DESTROY_DUMB) {
        struct drm_mode_destroy_dumb *destroy = (struct drm_mode_destroy_dumb *)arg;
        g_fake_destroy_dumb_called = true;
        g_fake_destroyed_handle = destroy->handle;
        return 0;
    }
    return -1;
}

// Tests
static void test_forward_and_ring_recording(void) {
    printf("Running test_forward_and_ring_recording...\n");
    fake_reset();
    shim_test_reset();

    int fd = 10;
    drmModeAtomicReqPtr req = drmModeAtomicAlloc();
    assert(req != NULL);

    // Add 13 properties
    for (uint32_t i = 0; i < 13; i++) {
        int r = drmModeAtomicAddProperty(req, 34, 1000 + i, 5000 + i);
        assert(r == (int)(i + 1));
    }
    assert(drmModeAtomicGetCursor(req) == 13);

    // Real commit with DRM_MODE_PAGE_FLIP_EVENT (call site a)
    void *dummy_userdata = (void *)0x12345678;
    int ret = drmModeAtomicCommit(fd, req, DRM_MODE_PAGE_FLIP_EVENT, dummy_userdata);
    assert(ret == 0);

    // Verify fake commit call
    assert(g_fake_commit_count == 1);
    assert(g_fake_commits[0].fd == fd);
    assert(g_fake_commits[0].flags == DRM_MODE_PAGE_FLIP_EVENT);
    assert(g_fake_commits[0].user_data == dummy_userdata);
    assert(g_fake_commits[0].item_count == 13);
    for (uint32_t i = 0; i < 13; i++) {
        assert(g_fake_commits[0].props[i].object_id == 34);
        assert(g_fake_commits[0].props[i].property_id == 1000 + i);
        assert(g_fake_commits[0].props[i].value == 5000 + i);
    }

    // Verify ring record
    assert(shim_test_get_ring_count() == 1);
    RingEntry entry;
    bool ok = shim_test_get_ring_entry(0, &entry);
    assert(ok);
    assert(entry.type == ENTRY_COMMIT);
    assert(entry.flags == DRM_MODE_PAGE_FLIP_EVENT);
    assert(entry.item_count == 13);
    assert(entry.has_user_data == true);
    assert(entry.ret == 0);
    assert(entry.call_site == CALL_SITE_FRAME);
    assert(entry.has_items == true); // 1st commit includes full item list
    assert(entry.num_items == 13);
    for (uint32_t i = 0; i < 13; i++) {
        assert(entry.items[i].object_id == 34);
        assert(entry.items[i].property_id == 1000 + i);
        assert(entry.items[i].value == 5000 + i);
    }

    drmModeAtomicFree(req);
    printf("test_forward_and_ring_recording PASSED\n");
}

static void test_first_8_vs_subsequent_items_recording(void) {
    printf("Running test_first_8_vs_subsequent_items_recording...\n");
    fake_reset();
    shim_test_reset();

    int fd = 10;
    // Run 10 commits for frame call site
    for (int c = 0; c < 10; c++) {
        drmModeAtomicReqPtr req = drmModeAtomicAlloc();
        drmModeAtomicAddProperty(req, 34, 10, 100);
        drmModeAtomicCommit(fd, req, DRM_MODE_PAGE_FLIP_EVENT, NULL);
        drmModeAtomicFree(req);
    }

    // Check first 8 commits have has_items == true
    for (uint32_t i = 0; i < 8; i++) {
        RingEntry entry;
        assert(shim_test_get_ring_entry(i, &entry));
        assert(entry.has_items == true);
        assert(entry.num_items == 1);
    }

    // 9th and 10th commits (index 8 and 9) must have has_items == false
    RingEntry e8, e9;
    assert(shim_test_get_ring_entry(8, &e8));
    assert(e8.has_items == false);
    assert(shim_test_get_ring_entry(9, &e9));
    assert(e9.has_items == false);

    printf("test_first_8_vs_subsequent_items_recording PASSED\n");
}

static void test_error_forwarding(void) {
    printf("Running test_error_forwarding...\n");
    fake_reset();
    shim_test_reset();

    g_fake_commit_ret = -1;
    g_fake_commit_errno = EBUSY;

    int fd = 10;
    drmModeAtomicReqPtr req = drmModeAtomicAlloc();
    assert(req != NULL);
    drmModeAtomicAddProperty(req, 34, 10, 100);

    int ret = drmModeAtomicCommit(fd, req, DRM_MODE_PAGE_FLIP_EVENT, NULL);
    assert(ret == -1);
    assert(errno == EBUSY);

    assert(shim_test_get_ring_count() == 1);
    RingEntry entry;
    assert(shim_test_get_ring_entry(0, &entry));
    assert(entry.ret == -1);
    assert(entry.err == EBUSY);

    drmModeAtomicFree(req);
    printf("test_error_forwarding PASSED\n");
}

static void test_stage2_off_no_extra_commit(void) {
    printf("Running test_stage2_off_no_extra_commit...\n");
    fake_reset();
    shim_test_reset();
    shim_test_set_stage2(false);

    int fd = 10;
    drmModeAtomicReqPtr req = drmModeAtomicAlloc();
    assert(req != NULL);
    for (uint32_t i = 0; i < 13; i++) {
        drmModeAtomicAddProperty(req, 34, 100 + i, 200 + i);
    }

    int ret = drmModeAtomicCommit(fd, req, DRM_MODE_PAGE_FLIP_EVENT, (void *)0xabcd);
    assert(ret == 0);

    // Exactly 1 commit in fake DRM (the host commit), NO extra commit
    assert(g_fake_commit_count == 1);
    assert(g_fake_commits[0].flags == DRM_MODE_PAGE_FLIP_EVENT);
    assert(g_fake_commits[0].item_count == 13);

    drmModeAtomicFree(req);
    printf("test_stage2_off_no_extra_commit PASSED\n");
}

static void test_stage2_on_probe(void) {
    printf("Running test_stage2_on_probe...\n");
    fake_reset();
    shim_test_reset();
    shim_test_set_stage2(true);

    int fd = 10;
    drmModeAtomicReqPtr req = drmModeAtomicAlloc();
    assert(req != NULL);

    // 13 properties from host
    for (uint32_t i = 0; i < 13; i++) {
        drmModeAtomicAddProperty(req, 34, 1000 + i, 2000 + i);
    }

    void *host_userdata = (void *)0xbeef;
    int ret = drmModeAtomicCommit(fd, req, DRM_MODE_PAGE_FLIP_EVENT, host_userdata);
    assert(ret == 0);

    // Verify 2 commits occurred:
    // 1. Host commit
    // 2. Stage 2 TEST_ONLY commit
    assert(g_fake_commit_count == 2);

    // Check host commit (Commit 0): NOT modified! Still has 13 properties
    CommitCall *c0 = &g_fake_commits[0];
    assert(c0->flags == DRM_MODE_PAGE_FLIP_EVENT);
    assert(c0->user_data == host_userdata);
    assert(c0->item_count == 13);

    // Check host's request pointer cursor still 13
    assert(drmModeAtomicGetCursor(req) == 13);

    // Check Stage 2 TEST_ONLY commit (Commit 1):
    CommitCall *c1 = &g_fake_commits[1];
    assert(c1->flags == DRM_MODE_ATOMIC_TEST_ONLY);
    assert((c1->flags & DRM_MODE_PAGE_FLIP_EVENT) == 0); // PAGE_FLIP_EVENT MUST NOT be set!
    assert(c1->user_data == NULL);

    // Must hold 13 copied items + 10 plane-46 properties = 23 items
    assert(c1->item_count == 23);

    // Verify first 13 items match host
    for (uint32_t i = 0; i < 13; i++) {
        assert(c1->props[i].object_id == 34);
        assert(c1->props[i].property_id == 1000 + i);
        assert(c1->props[i].value == 2000 + i);
    }

    // Verify plane-46 properties (items 13..22)
    // FB_ID (prop 101) = 99
    assert(c1->props[13].object_id == 46);
    assert(c1->props[13].property_id == 101);
    assert(c1->props[13].value == 99);

    // CRTC_ID (prop 102) = 82
    assert(c1->props[14].object_id == 46);
    assert(c1->props[14].property_id == 102);
    assert(c1->props[14].value == 82);

    // CRTC_X (prop 103) = 200
    assert(c1->props[15].object_id == 46);
    assert(c1->props[15].property_id == 103);
    assert(c1->props[15].value == 200);

    // CRTC_Y (prop 104) = 200
    assert(c1->props[16].object_id == 46);
    assert(c1->props[16].property_id == 104);
    assert(c1->props[16].value == 200);

    // CRTC_W (prop 105) = 400
    assert(c1->props[17].object_id == 46);
    assert(c1->props[17].property_id == 105);
    assert(c1->props[17].value == 400);

    // CRTC_H (prop 106) = 300
    assert(c1->props[18].object_id == 46);
    assert(c1->props[18].property_id == 106);
    assert(c1->props[18].value == 300);

    // SRC_X (prop 107) = 0
    assert(c1->props[19].object_id == 46);
    assert(c1->props[19].property_id == 107);
    assert(c1->props[19].value == 0);

    // SRC_Y (prop 108) = 0
    assert(c1->props[20].object_id == 46);
    assert(c1->props[20].property_id == 108);
    assert(c1->props[20].value == 0);

    // SRC_W (prop 109) = 400 << 16
    assert(c1->props[21].object_id == 46);
    assert(c1->props[21].property_id == 109);
    assert(c1->props[21].value == ((uint64_t)400 << 16));

    // SRC_H (prop 110) = 300 << 16
    assert(c1->props[22].object_id == 46);
    assert(c1->props[22].property_id == 110);
    assert(c1->props[22].value == ((uint64_t)300 << 16));

    // Rate-limiting test: immediate second commit within 1 second should NOT trigger another Stage 2 commit
    int ret2 = drmModeAtomicCommit(fd, req, DRM_MODE_PAGE_FLIP_EVENT, host_userdata);
    assert(ret2 == 0);
    assert(g_fake_commit_count == 3); // 2 previous + 1 host commit = 3 (no 4th commit)

    drmModeAtomicFree(req);
    printf("test_stage2_on_probe PASSED\n");
}

static void test_cursor_call_site_b(void) {
    printf("Running test_cursor_call_site_b...\n");
    fake_reset();
    shim_test_reset();
    shim_test_set_stage2(true); // Stage 2 enabled, but call site is cursor!

    int fd = 10;
    drmModeAtomicReqPtr req = drmModeAtomicAlloc();
    assert(req != NULL);
    drmModeAtomicAddProperty(req, 40, 10, 100);

    // Call site (b): DRM_MODE_ATOMIC_NONBLOCK and user_data == NULL
    int ret = drmModeAtomicCommit(fd, req, DRM_MODE_ATOMIC_NONBLOCK, NULL);
    assert(ret == 0);

    // Only 1 commit occurred (cursor move must never trigger stage 2)
    assert(g_fake_commit_count == 1);
    assert(g_fake_commits[0].flags == DRM_MODE_ATOMIC_NONBLOCK);

    RingEntry entry;
    assert(shim_test_get_ring_entry(0, &entry));
    assert(entry.call_site == CALL_SITE_CURSOR);

    drmModeAtomicFree(req);
    printf("test_cursor_call_site_b PASSED\n");
}

static void test_summary_line_generation(void) {
    printf("Running test_summary_line_generation...\n");
    fake_reset();
    shim_test_reset();

    int fd = 10;
    drmModeAtomicReqPtr req = drmModeAtomicAlloc();
    drmModeAtomicAddProperty(req, 34, 1, 1);
    drmModeAtomicCommit(fd, req, DRM_MODE_PAGE_FLIP_EVENT, NULL);
    drmModeAtomicCommit(fd, req, DRM_MODE_ATOMIC_NONBLOCK, NULL);
    drmModeAtomicFree(req);

    char log_buf[4096];
    memset(log_buf, 0, sizeof(log_buf));
    FILE *mem_fp = fmemopen(log_buf, sizeof(log_buf) - 1, "w");
    assert(mem_fp != NULL);

    // Drain commits so summary is updated
    shim_test_drain_logger(mem_fp);

    // Now write summary
    shim_test_write_summary(mem_fp, 5000000000ULL);
    fclose(mem_fp);

    printf("--- Summary line preview ---\n%s----------------------------\n", log_buf);
    assert(strstr(log_buf, "[SUMMARY 5s]") != NULL);
    assert(strstr(log_buf, "rate(frame)=") != NULL);
    assert(strstr(log_buf, "rate(cursor)=") != NULL);
    assert(strstr(log_buf, "dur_ns min=") != NULL);
    assert(strstr(log_buf, "ret={0:2, err:0}") != NULL);

    printf("test_summary_line_generation PASSED\n");
}

static void test_4mb_log_cap(void) {
    printf("Running test_4mb_log_cap...\n");
    fake_reset();
    shim_test_reset();

    // Set written bytes close to 4MB
    shim_test_set_log_bytes_written(4 * 1024 * 1024 - 10);
    assert(!shim_test_is_log_capped());

    int fd = 10;
    drmModeAtomicReqPtr req = drmModeAtomicAlloc();
    drmModeAtomicAddProperty(req, 34, 1, 1);
    drmModeAtomicCommit(fd, req, DRM_MODE_PAGE_FLIP_EVENT, NULL);
    drmModeAtomicFree(req);

    char log_buf[4096];
    memset(log_buf, 0, sizeof(log_buf));
    FILE *mem_fp = fmemopen(log_buf, sizeof(log_buf) - 1, "w");
    assert(mem_fp != NULL);

    shim_test_drain_logger(mem_fp);
    fclose(mem_fp);

    assert(shim_test_is_log_capped());
    assert(strstr(log_buf, "Log file reached 4 MB limit, stopping logging") != NULL);

    printf("test_4mb_log_cap PASSED\n");
}

static void test_logger_drain_and_formatting(void) {
    printf("Running test_logger_drain_and_formatting...\n");
    fake_reset();
    shim_test_reset();
    shim_test_set_stage2(true);

    int fd = 10;
    drmModeAtomicReqPtr req = drmModeAtomicAlloc();
    for (uint32_t i = 0; i < 13; i++) {
        drmModeAtomicAddProperty(req, 34, 100 + i, 200 + i);
    }
    drmModeAtomicCommit(fd, req, DRM_MODE_PAGE_FLIP_EVENT, NULL);

    char log_buf[8192];
    memset(log_buf, 0, sizeof(log_buf));
    FILE *mem_fp = fmemopen(log_buf, sizeof(log_buf) - 1, "w");
    assert(mem_fp != NULL);

    shim_test_drain_logger(mem_fp);
    fclose(mem_fp);

    printf("--- Log output preview ---\n%s--------------------------\n", log_buf);
    assert(strstr(log_buf, "commit time_ns=") != NULL);
    assert(strstr(log_buf, "site=frame") != NULL);
    assert(strstr(log_buf, "items=[34:100=200") != NULL);
    assert(strstr(log_buf, "[STAGE2] Plane 46 props:") != NULL);
    assert(strstr(log_buf, "[STAGE2] Plane 34 props:") != NULL);
    assert(strstr(log_buf, "[STAGE2] Created dumb FB 400x300") != NULL);
    assert(strstr(log_buf, "[STAGE2] TEST_ONLY commit") != NULL);

    drmModeAtomicFree(req);
    printf("test_logger_drain_and_formatting PASSED\n");
}

static void test_background_logger_thread(void) {
    printf("Running test_background_logger_thread...\n");
    fake_reset();
    shim_test_reset();

    // Start background thread
    shim_test_start_thread();

    int fd = 10;
    drmModeAtomicReqPtr req = drmModeAtomicAlloc();
    drmModeAtomicAddProperty(req, 34, 1, 1);
    drmModeAtomicCommit(fd, req, DRM_MODE_PAGE_FLIP_EVENT, NULL);
    drmModeAtomicFree(req);

    // Stop background thread (joins cleanly)
    shim_test_stop_thread();

    printf("test_background_logger_thread PASSED\n");
}

static void test_stage3_on_merged_frame_commit(void) {
    printf("Running test_stage3_on_merged_frame_commit...\n");
    fake_reset();
    shim_test_reset();
    shim_test_set_stage3(true);

    int fd = 10;
    drmModeAtomicReqPtr req = drmModeAtomicAlloc();
    assert(req != NULL);

    // Host adds 15 properties
    for (uint32_t i = 0; i < 15; i++) {
        drmModeAtomicAddProperty(req, 34, 1000 + i, 2000 + i);
    }
    assert(drmModeAtomicGetCursor(req) == 15);

    void *host_userdata = (void *)0xfacefeed;
    int ret = drmModeAtomicCommit(fd, req, DRM_MODE_PAGE_FLIP_EVENT, host_userdata);
    assert(ret == 0);

    // Fake libdrm receives exactly ONE commit with 15 + 10 = 25 items
    assert(g_fake_commit_count == 1);
    CommitCall *c = &g_fake_commits[0];
    assert(c->fd == fd);
    assert(c->flags == DRM_MODE_PAGE_FLIP_EVENT);
    assert(c->user_data == host_userdata);
    assert(c->item_count == 25);

    // Host request object untouched (cursor still 15)
    assert(drmModeAtomicGetCursor(req) == 15);

    // First 15 items match host items
    for (uint32_t i = 0; i < 15; i++) {
        assert(c->props[i].object_id == 34);
        assert(c->props[i].property_id == 1000 + i);
        assert(c->props[i].value == 2000 + i);
    }

    // Plane 46 properties (items 15..24)
    // FB_ID (101) = 99
    assert(c->props[15].object_id == 46);
    assert(c->props[15].property_id == 101);
    assert(c->props[15].value == 99);

    // CRTC_ID (102) = 82
    assert(c->props[16].object_id == 46);
    assert(c->props[16].property_id == 102);
    assert(c->props[16].value == 82);

    // CRTC_X (103) = 200
    assert(c->props[17].object_id == 46);
    assert(c->props[17].property_id == 103);
    assert(c->props[17].value == 200);

    // CRTC_Y (104) = 200
    assert(c->props[18].object_id == 46);
    assert(c->props[18].property_id == 104);
    assert(c->props[18].value == 200);

    // CRTC_W (105) = 400
    assert(c->props[19].object_id == 46);
    assert(c->props[19].property_id == 105);
    assert(c->props[19].value == 400);

    // CRTC_H (106) = 300
    assert(c->props[20].object_id == 46);
    assert(c->props[20].property_id == 106);
    assert(c->props[20].value == 300);

    // SRC_X (107) = 0
    assert(c->props[21].object_id == 46);
    assert(c->props[21].property_id == 107);
    assert(c->props[21].value == 0);

    // SRC_Y (108) = 0
    assert(c->props[22].object_id == 46);
    assert(c->props[22].property_id == 108);
    assert(c->props[22].value == 0);

    // SRC_W (109) = 400 << 16
    assert(c->props[23].object_id == 46);
    assert(c->props[23].property_id == 109);
    assert(c->props[23].value == ((uint64_t)400 << 16));

    // SRC_H (110) = 300 << 16
    assert(c->props[24].object_id == 46);
    assert(c->props[24].property_id == 110);
    assert(c->props[24].value == ((uint64_t)300 << 16));

    // Verify test buffer pattern fill
    assert(g_fake_mmap_called);
    assert(g_fake_munmap_called);
    uint32_t *pixels = (uint32_t *)g_fake_dumb_buf;
    // Border at (0,0) is white
    assert(pixels[0] == 0x00FFFFFF);
    // Border at (15, 100) is white
    assert(pixels[100 * 400 + 15] == 0x00FFFFFF);
    // Inside at (20, 200) away from diagonal is mid-red
    assert(pixels[200 * 400 + 20] == 0x00C03030);
    // Diagonal at (100, 75) is white
    assert(pixels[75 * 400 + 100] == 0x00FFFFFF);

    assert(shim_test_is_plane46_active() == true);

    drmModeAtomicFree(req);
    printf("test_stage3_on_merged_frame_commit PASSED\n");
}

static void test_stage3_failing_merged_fallback(void) {
    printf("Running test_stage3_failing_merged_fallback...\n");
    fake_reset();
    shim_test_reset();
    shim_test_set_stage3(true);

    int fd = 10;
    drmModeAtomicReqPtr req = drmModeAtomicAlloc();
    assert(req != NULL);

    for (uint32_t i = 0; i < 15; i++) {
        drmModeAtomicAddProperty(req, 34, 1000 + i, 2000 + i);
    }

    // Configure fake libdrm:
    // First commit (merged) returns -1 with errno 22 (EINVAL)
    // Second commit (fallback) returns 0
    g_use_per_commit_ret = true;
    g_fake_commit_rets[0] = -1;
    g_fake_commit_errs[0] = 22; // EINVAL
    g_fake_commit_rets[1] = 0;
    g_fake_commit_errs[1] = 0;

    void *host_userdata = (void *)0xcafe;
    int ret = drmModeAtomicCommit(fd, req, DRM_MODE_PAGE_FLIP_EVENT, host_userdata);
    assert(ret == 0); // Host got the fallback commit's return value (0)

    // Exactly 2 commits occurred
    assert(g_fake_commit_count == 2);

    // Commit 0: merged attempt (25 items)
    assert(g_fake_commits[0].item_count == 25);

    // Commit 1: original host request fallback (15 items)
    assert(g_fake_commits[1].item_count == 15);
    assert(g_fake_commits[1].flags == DRM_MODE_PAGE_FLIP_EVENT);
    assert(g_fake_commits[1].user_data == host_userdata);
    for (uint32_t i = 0; i < 15; i++) {
        assert(g_fake_commits[1].props[i].object_id == 34);
        assert(g_fake_commits[1].props[i].property_id == 1000 + i);
    }

    // Stage 3 is now OFF!
    assert(shim_test_is_stage3_enabled() == false);

    drmModeAtomicFree(req);
    printf("test_stage3_failing_merged_fallback PASSED\n");
}

static void test_stage3_cursor_commit_unmerged(void) {
    printf("Running test_stage3_cursor_commit_unmerged...\n");
    fake_reset();
    shim_test_reset();
    shim_test_set_stage3(true);

    int fd = 10;
    drmModeAtomicReqPtr req = drmModeAtomicAlloc();
    assert(req != NULL);
    drmModeAtomicAddProperty(req, 40, 50, 100);

    // Call site (b): DRM_MODE_ATOMIC_NONBLOCK and user_data == NULL
    int ret = drmModeAtomicCommit(fd, req, DRM_MODE_ATOMIC_NONBLOCK, NULL);
    assert(ret == 0);

    // Exactly 1 commit, untouched / unmerged
    assert(g_fake_commit_count == 1);
    assert(g_fake_commits[0].flags == DRM_MODE_ATOMIC_NONBLOCK);
    assert(g_fake_commits[0].item_count == 1);
    assert(g_fake_commits[0].props[0].object_id == 40);

    drmModeAtomicFree(req);
    printf("test_stage3_cursor_commit_unmerged PASSED\n");
}

static void test_stage3_off_disables_plane_and_frees_fb(void) {
    printf("Running test_stage3_off_disables_plane_and_frees_fb...\n");
    fake_reset();
    shim_test_reset();
    shim_test_set_stage3(true);

    int fd = 10;
    // Step 1: Active Stage 3 frame commit
    drmModeAtomicReqPtr req1 = drmModeAtomicAlloc();
    for (uint32_t i = 0; i < 15; i++) {
        drmModeAtomicAddProperty(req1, 34, 1000 + i, 2000 + i);
    }
    int ret = drmModeAtomicCommit(fd, req1, DRM_MODE_PAGE_FLIP_EVENT, (void *)1);
    assert(ret == 0);
    assert(shim_test_is_plane46_active() == true);
    drmModeAtomicFree(req1);

    fake_reset(); // Reset commit tracking

    // Step 2: Stage 3 turned OFF
    shim_test_set_stage3(false);

    // Step 3: Next frame commit disables plane 46
    drmModeAtomicReqPtr req2 = drmModeAtomicAlloc();
    for (uint32_t i = 0; i < 15; i++) {
        drmModeAtomicAddProperty(req2, 34, 1000 + i, 2000 + i);
    }
    ret = drmModeAtomicCommit(fd, req2, DRM_MODE_PAGE_FLIP_EVENT, (void *)2);
    assert(ret == 0);

    // Exactly 1 commit disabling plane 46: 15 host items + 2 disable items = 17 items
    assert(g_fake_commit_count == 1);
    assert(g_fake_commits[0].item_count == 17);
    assert(g_fake_commits[0].props[15].object_id == 46);
    assert(g_fake_commits[0].props[15].property_id == 101); // FB_ID
    assert(g_fake_commits[0].props[15].value == 0);
    assert(g_fake_commits[0].props[16].object_id == 46);
    assert(g_fake_commits[0].props[16].property_id == 102); // CRTC_ID
    assert(g_fake_commits[0].props[16].value == 0);

    // Plane 46 no longer active
    assert(shim_test_is_plane46_active() == false);

    // Test buffer and FB freed!
    assert(g_fake_rmfb_called == true);
    assert(g_fake_rmfb_id == 99);
    assert(g_fake_destroy_dumb_called == true);
    assert(g_fake_destroyed_handle == 42);

    drmModeAtomicFree(req2);
    printf("test_stage3_off_disables_plane_and_frees_fb PASSED\n");
}

static void test_stage3_70_items_not_merged(void) {
    printf("Running test_stage3_70_items_not_merged...\n");
    fake_reset();
    shim_test_reset();
    shim_test_set_stage3(true);

    int fd = 10;
    drmModeAtomicReqPtr req = drmModeAtomicAlloc();
    assert(req != NULL);

    for (uint32_t i = 0; i < 70; i++) {
        drmModeAtomicAddProperty(req, 34, 100 + i, 200 + i);
    }
    assert(drmModeAtomicGetCursor(req) == 70);

    int ret = drmModeAtomicCommit(fd, req, DRM_MODE_PAGE_FLIP_EVENT, (void *)0x70);
    assert(ret == 0);

    // Exactly 1 commit, with exactly 70 items (NOT merged!)
    assert(g_fake_commit_count == 1);
    assert(g_fake_commits[0].item_count == 70);

    drmModeAtomicFree(req);
    printf("test_stage3_70_items_not_merged PASSED\n");
}

static void test_item_list_logged_on_count_change(void) {
    printf("Running test_item_list_logged_on_count_change...\n");
    fake_reset();
    shim_test_reset();

    int fd = 10;
    // 8 frame commits with 15 items
    for (int c = 0; c < 8; c++) {
        drmModeAtomicReqPtr req = drmModeAtomicAlloc();
        for (uint32_t i = 0; i < 15; i++) drmModeAtomicAddProperty(req, 34, 100 + i, 200 + i);
        drmModeAtomicCommit(fd, req, DRM_MODE_PAGE_FLIP_EVENT, NULL);
        drmModeAtomicFree(req);
    }

    // 9th frame commit with 15 items: should NOT have items logged (after first 8, same count)
    drmModeAtomicReqPtr req9 = drmModeAtomicAlloc();
    for (uint32_t i = 0; i < 15; i++) drmModeAtomicAddProperty(req9, 34, 100 + i, 200 + i);
    drmModeAtomicCommit(fd, req9, DRM_MODE_PAGE_FLIP_EVENT, NULL);
    drmModeAtomicFree(req9);

    RingEntry e9;
    assert(shim_test_get_ring_entry(8, &e9));
    assert(e9.has_items == false);

    // 10th frame commit with 23 items: item count changed from 15 to 23!
    drmModeAtomicReqPtr req10 = drmModeAtomicAlloc();
    for (uint32_t i = 0; i < 23; i++) drmModeAtomicAddProperty(req10, 34, 100 + i, 300 + i);
    drmModeAtomicCommit(fd, req10, DRM_MODE_PAGE_FLIP_EVENT, NULL);
    drmModeAtomicFree(req10);

    RingEntry e10;
    assert(shim_test_get_ring_entry(9, &e10));
    assert(e10.has_items == true); // Item list is logged!
    assert(e10.num_items == 23);
    assert(e10.items[22].object_id == 34);
    assert(e10.items[22].property_id == 122);
    assert(e10.items[22].value == 322);

    // Drain into string and verify log output
    char log_buf[8192];
    memset(log_buf, 0, sizeof(log_buf));
    FILE *mem_fp = fmemopen(log_buf, sizeof(log_buf) - 1, "w");
    assert(mem_fp != NULL);
    shim_test_drain_logger(mem_fp);
    fclose(mem_fp);

    assert(strstr(log_buf, "items=[34:100=300") != NULL);
    assert(strstr(log_buf, "34:122=322") != NULL);

    printf("test_item_list_logged_on_count_change PASSED\n");
}

static void test_stage2_off_frees_buffer(void) {
    printf("Running test_stage2_off_frees_buffer...\n");
    fake_reset();
    shim_test_reset();

    int fd = 10;
    // Enable stage 2 and commit a frame -> creates dumb buffer during probe
    shim_test_set_stage2(true);
    drmModeAtomicReqPtr req = drmModeAtomicAlloc();
    drmModeAtomicAddProperty(req, 34, 1, 1);
    drmModeAtomicCommit(fd, req, DRM_MODE_PAGE_FLIP_EVENT, NULL);
    drmModeAtomicFree(req);

    assert(g_fake_destroy_dumb_called == false);
    assert(g_fake_rmfb_called == false);

    // Disable stage 2
    shim_test_set_stage2(false);

    // Next frame commit: frees test buffer because both stage 2 and 3 are off!
    drmModeAtomicReqPtr req2 = drmModeAtomicAlloc();
    drmModeAtomicAddProperty(req2, 34, 1, 1);
    drmModeAtomicCommit(fd, req2, DRM_MODE_PAGE_FLIP_EVENT, NULL);
    drmModeAtomicFree(req2);

    assert(g_fake_rmfb_called == true);
    assert(g_fake_destroy_dumb_called == true);

    printf("test_stage2_off_frees_buffer PASSED\n");
}

int main(void) {
    printf("=== Starting xl-commit-shim Host Tests ===\n");
    test_forward_and_ring_recording();
    test_first_8_vs_subsequent_items_recording();
    test_error_forwarding();
    test_stage2_off_no_extra_commit();
    test_stage2_on_probe();
    test_cursor_call_site_b();
    test_summary_line_generation();
    test_4mb_log_cap();
    test_logger_drain_and_formatting();
    test_background_logger_thread();
    test_stage3_on_merged_frame_commit();
    test_stage3_failing_merged_fallback();
    test_stage3_cursor_commit_unmerged();
    test_stage3_off_disables_plane_and_frees_fb();
    test_stage3_70_items_not_merged();
    test_item_list_logged_on_count_change();
    test_stage2_off_frees_buffer();
    printf("=== All xl-commit-shim Host Tests PASSED! ===\n");
    return 0;
}
