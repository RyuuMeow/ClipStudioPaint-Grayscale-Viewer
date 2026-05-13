#pragma once

#include <Windows.h>
#include <magnification.h>

#pragma comment(lib, "Magnification.lib")

namespace csp::magnification
{

    class GrayscaleEffect
    {
    public:
        GrayscaleEffect() = default;
        ~GrayscaleEffect();

        GrayscaleEffect(const GrayscaleEffect&) = delete;
        GrayscaleEffect& operator=(const GrayscaleEffect&) = delete;

        bool Initialize();
        void Shutdown();

        bool ApplyGrayscale();
        bool RemoveGrayscale();

        bool Toggle();

        bool IsActive() const
        { return bActive; }
        bool IsInitialized() const
        { return bInitialized; }

    private:
        bool bInitialized = false;
        bool bActive = false;
    };

}
