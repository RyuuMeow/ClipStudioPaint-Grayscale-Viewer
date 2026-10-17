#pragma once
#include <Windows.h>
#include <algorithm>

namespace csp::d3d
{
    // Desktop duplication uses unrotated, per-output coordinates. Unsupported
    // geometry must use Magnification rather than displaying an incomplete texture.
    inline bool CanCaptureWindow(const RECT& target, const RECT& output, DWORD orientation)
    {
        return orientation == DMDO_DEFAULT && target.right > target.left && target.bottom > target.top &&
            target.left >= output.left && target.top >= output.top &&
            target.right <= output.right && target.bottom <= output.bottom;
    }

    inline int EffectiveRefreshRate(int requested, int monitorRate, bool magnification)
    {
        monitorRate = monitorRate > 1 ? monitorRate : 60;
        int fps = requested == 0 ? monitorRate : requested;
        return std::clamp(fps, 15, std::min(magnification ? 100 : 144, std::max(15, monitorRate)));
    }
}
