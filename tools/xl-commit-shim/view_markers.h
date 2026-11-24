#pragma once
#include <stdbool.h>
#include "view_marker_pairs.h"

static inline bool color_close(uint32_t a, uint32_t b) {
    int r_diff = (int)((a >> 16) & 0xff) - (int)((b >> 16) & 0xff);
    int g_diff = (int)((a >> 8) & 0xff) - (int)((b >> 8) & 0xff);
    int b_diff = (int)(a & 0xff) - (int)(b & 0xff);
    if (r_diff < 0) r_diff = -r_diff;
    if (g_diff < 0) g_diff = -g_diff;
    if (b_diff < 0) b_diff = -b_diff;
    return (r_diff <= 2) && (g_diff <= 2) && (b_diff <= 2);
}

static inline int shim_view_index(uint32_t left, uint32_t right) {
    for (unsigned i = 0; i < sizeof(shim_marker_pairs) / sizeof(shim_marker_pairs[0]); ++i) {
        if (color_close(left, shim_marker_pairs[i][0]) && color_close(right, shim_marker_pairs[i][1]))
            return (int)i;
    }
    return -1;
}
