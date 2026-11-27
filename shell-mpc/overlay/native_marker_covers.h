// Small opaque patches for the host's marker/instance pixels in native pages.
#pragma once

#include <cstring>

#ifdef SYNTHYBOY_BACKGROUNDS_MANIFEST_H
#include SYNTHYBOY_BACKGROUNDS_MANIFEST_H
#else
#include "backgrounds_manifest.h"
#endif
#include "../ui/theme.h"
#include "gpu/scene.h"
#include "overlay_layout.h"

namespace synthyboy_mpc { namespace overlay {

inline void drawNativeMarkerCovers (gpu::SceneBuilder& sb, const char* name) noexcept
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
        sb.rect (left, top, right, bottom, gpu::rgba8FromRgb (ui::theme::kNativeCanvas, 255));
    }
    // The sentinel of a view that lies in the flat canvas strip above the page content (tab y < 10) is covered too; one lower in the page is
    // close to the colour of the panel it lies on (99_backgrounds.py), and a canvas patch there would be a visible dark square.
    if (const BackgroundTab* bg = backgroundOf (name))
        if (bg->sentinelY < 10)
        {
            const int x = bg->sentinelX, y = layout::kTabOriginY + bg->sentinelY;
            sb.rect (static_cast<float> (x), static_cast<float> (y), static_cast<float> (x + 5), static_cast<float> (y + 5), gpu::rgba8FromRgb (ui::theme::kNativeCanvas, 255));
        }
}

}} // namespace synthyboy_mpc::overlay
