// Small opaque patches for the host's marker/instance pixels in native pages.
#pragma once

#include <cstring>

#include "gpu/scene.h"
#include "overlay_layout.h"

namespace synthyboy_mpc { namespace overlay {

inline void drawNativeMarkerCovers (gpu::SceneBuilder& sb, const char* name, unsigned topRgb, unsigned bottomRgb) noexcept
{
    const layout::View* view = nullptr;
    for (const layout::View& candidate : layout::kViews)
        if (std::strcmp (candidate.name, name) == 0)
        {
            view = &candidate;
            break;
        }
    if (view == nullptr)
        return;
    const layout::Rect spots[3] = { view->markers[0].rect, view->instance, view->markers[1].rect };
    for (const layout::Rect& r : spots)
    {
        const float left = static_cast<float> (r.x - 1), right = static_cast<float> (r.x + r.w + 1);
        const float top = static_cast<float> (r.y - 1), bottom = static_cast<float> (r.y + r.h + 1);
        const auto first = gpu::rgba8FromRgb (topRgb, 255), last = gpu::rgba8FromRgb (bottomRgb, 255);
        if (first == last)
            sb.rect (left, top, right, bottom, first);
        else
            sb.gradientRect (left, top, right, bottom, first, last);
    }
}

}} // namespace synthyboy_mpc::overlay
