// SynthyBoy - MPC XL atomic commit measurement shim.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef XL_COMMIT_SHIM_H
#define XL_COMMIT_SHIM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// DRM Atomic Flags
#define DRM_MODE_PAGE_FLIP_EVENT       0x01
#define DRM_MODE_PAGE_FLIP_ASYNC       0x02
#define DRM_MODE_ATOMIC_TEST_ONLY      0x0100
#define DRM_MODE_ATOMIC_NONBLOCK       0x0200
#define DRM_MODE_ATOMIC_ALLOW_MODESET  0x0400

// DRM Object Types
#define DRM_MODE_OBJECT_CRTC           0xcccccccc
#define DRM_MODE_OBJECT_CONNECTOR      0xc0c0c0c0
#define DRM_MODE_OBJECT_ENCODER        0xe0e0e0e0
#define DRM_MODE_OBJECT_MODE           0xdededede
#define DRM_MODE_OBJECT_PROPERTY       0xb0b0b0b0
#define DRM_MODE_OBJECT_FB             0xfbfbfbfb
#define DRM_MODE_OBJECT_BLOB           0xbbbbbbbb
#define DRM_MODE_OBJECT_PLANE          0xeeeeeeee
#define DRM_MODE_OBJECT_ANY            0

#define DRM_PROP_NAME_LEN              32

// DRM FourCC Formats
#define DRM_FORMAT_XRGB8888            0x34325258 /* XR24 */
#define DRM_FORMAT_ARGB8888            0x34325241 /* AR24 */

// Dumb buffer IOCTL
#define DRM_IOCTL_MODE_CREATE_DUMB     0xc02064b2

struct drm_mode_create_dumb {
    uint32_t height;
    uint32_t width;
    uint32_t bpp;
    uint32_t flags;
    uint32_t handle;
    uint32_t pitch;
    uint64_t size;
};

typedef struct _drmModeObjectProperties {
    uint32_t count_props;
    uint32_t *props;
    uint64_t *prop_values;
} drmModeObjectProperties, *drmModeObjectPropertiesPtr;

struct drm_mode_property_enum {
    uint64_t value;
    char name[DRM_PROP_NAME_LEN];
};

typedef struct _drmModeProperty {
    uint32_t prop_id;
    uint32_t flags;
    char name[DRM_PROP_NAME_LEN];
    int count_values;
    uint64_t *values;
    int count_enums;
    struct drm_mode_property_enum *enums;
    int count_blobs;
    uint32_t *blob_ids;
} drmModePropertyRes, *drmModePropertyPtr;

typedef void *drmModeAtomicReqPtr;

#define MAX_ITEMS_PER_REQ 32
#define RING_BUFFER_SIZE 2048
#define RING_BUFFER_MASK (RING_BUFFER_SIZE - 1)

enum CallSite {
    CALL_SITE_FRAME = 0,   // (a) frame present: DRM_MODE_PAGE_FLIP_EVENT (0x1)
    CALL_SITE_CURSOR = 1,  // (b) cursor move: DRM_MODE_ATOMIC_NONBLOCK (0x200), no user data
    CALL_SITE_OTHER = 2,
    CALL_SITE_COUNT = 3
};

enum EntryType {
    ENTRY_COMMIT = 0,
    ENTRY_STAGE2 = 1,
    ENTRY_MSG = 2
};

typedef struct {
    uint32_t object_id;
    uint32_t property_id;
    uint64_t value;
} ShimPropItem;

typedef struct {
    uint8_t type;            // EntryType
    uint64_t time_ns;        // CLOCK_MONOTONIC ns
    pid_t tid;               // gettid
    uint32_t flags;          // commit flags
    uint32_t item_count;     // drmModeAtomicGetCursor(req) or recorded count
    bool has_user_data;      // user_data != NULL
    int ret;                 // return value
    int err;                 // errno if ret < 0
    int call_site;           // enum CallSite
    uint64_t duration_ns;    // duration in ns
    bool has_items;          // whether items are populated
    uint32_t num_items;
    ShimPropItem items[MAX_ITEMS_PER_REQ];
    char msg[256];
} RingEntry;

// Intercepted public functions (exported from xl-commit-shim.so)
drmModeAtomicReqPtr drmModeAtomicAlloc(void);
void drmModeAtomicFree(drmModeAtomicReqPtr req);
int drmModeAtomicAddProperty(drmModeAtomicReqPtr req, uint32_t object_id, uint32_t property_id, uint64_t value);
int drmModeAtomicGetCursor(drmModeAtomicReqPtr req);
int drmModeAtomicCommit(int fd, drmModeAtomicReqPtr req, uint32_t flags, void *user_data);

#ifdef SHIM_TEST
void shim_test_reset(void);
void shim_test_set_stage2(bool enabled);
uint32_t shim_test_get_ring_count(void);
bool shim_test_get_ring_entry(uint32_t index, RingEntry *out);
void shim_test_drain_logger(FILE *fp);
void shim_test_set_last_stage2_ns(uint64_t ns);
void shim_test_write_summary(FILE *fp, uint64_t elapsed_ns);
void shim_test_set_log_bytes_written(size_t bytes);
bool shim_test_is_log_capped(void);
void shim_test_start_thread(void);
void shim_test_stop_thread(void);
#endif

#ifdef __cplusplus
}
#endif

#endif // XL_COMMIT_SHIM_H
