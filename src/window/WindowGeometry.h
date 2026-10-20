#pragma once
#include <Windows.h>

namespace csp::window
{
    inline RECT ClipMaximizedFrame(const RECT& bounds, const RECT& monitor, bool maximized, SIZE border)
    {
        // Some custom-framed maximized windows retain their invisible sizing
        // border even in DWMWA_EXTENDED_FRAME_BOUNDS. Only remove that border;
        // do not silently crop a genuinely spanning or restored window.
        if (!maximized || border.cx < 0 || border.cy < 0 ||
            bounds.left < monitor.left - border.cx || bounds.top < monitor.top - border.cy ||
            bounds.right > monitor.right + border.cx || bounds.bottom > monitor.bottom + border.cy)
            return bounds;
        RECT clipped{};
        return IntersectRect(&clipped, &bounds, &monitor) ? clipped : bounds;
    }

    inline SIZE ResizeBorderForWindow(HWND window)
    {
        // Resolve DPI APIs at runtime so older compatibility-only Windows
        // versions can still load the executable.
        using ReadDpi = UINT(WINAPI*)(HWND);
        using ReadMetric = int(WINAPI*)(int, UINT);
        static const auto dpiForWindow = reinterpret_cast<ReadDpi>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
        static const auto metricForDpi = reinterpret_cast<ReadMetric>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetSystemMetricsForDpi"));
        const UINT dpi = dpiForWindow ? dpiForWindow(window) : 0;
        const auto metric = [&](int index)
        { return dpi && metricForDpi ? metricForDpi(index, dpi) : GetSystemMetrics(index); };
        return {metric(SM_CXSIZEFRAME) + metric(SM_CXPADDEDBORDER),
                metric(SM_CYSIZEFRAME) + metric(SM_CXPADDEDBORDER)};
    }

    inline RECT VisibleCaptureBounds(HWND window, const RECT& bounds, const RECT& monitor)
    {
        if (!IsZoomed(window)) return bounds;
        return ClipMaximizedFrame(bounds, monitor, true, ResizeBorderForWindow(window));
    }
}
