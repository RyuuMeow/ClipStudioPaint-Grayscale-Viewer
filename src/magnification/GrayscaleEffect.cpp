#include "magnification/GrayscaleEffect.h"
#include "util/Logger.h"

namespace csp::magnification
{

    namespace
    {
        constexpr MAGCOLOREFFECT kIdentityMatrix = {{
            { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f },
            { 0.0f, 1.0f, 0.0f, 0.0f, 0.0f },
            { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f },
            { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f },
            { 0.0f, 0.0f, 0.0f, 0.0f, 1.0f }
        }};


        constexpr float kR = 0.299f;
        constexpr float kG = 0.587f;
        constexpr float kB = 0.114f;

        constexpr MAGCOLOREFFECT kGrayscaleMatrix = {{
            { kR, kR, kR, 0.0f, 0.0f },
            { kG, kG, kG, 0.0f, 0.0f },
            { kB, kB, kB, 0.0f, 0.0f },
            { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f },
            { 0.0f, 0.0f, 0.0f, 0.0f, 1.0f }
        }};
    }

    GrayscaleEffect::~GrayscaleEffect()
    {
        Shutdown();
    }

    bool GrayscaleEffect::Initialize()
    {
        if (bInitialized)
        {
            return true;
        }

        if (!MagInitialize())
        {
            LOG_ERROR(L"MagInitialize failed (error %u)", GetLastError());
            return false;
        }

        bInitialized = true;
        LOG_INFO(L"Magnification API initialized");
        return true;
    }

    void GrayscaleEffect::Shutdown()
    {
        if (!bInitialized)
        {
            return;
        }

        if (bActive)
        {
            RemoveGrayscale();
        }

        MagUninitialize();
        bInitialized = false;
        LOG_INFO(L"Magnification API shut down");
    }

    bool GrayscaleEffect::ApplyGrayscale()
    {
        if (!bInitialized)
        {
            return false;
        }
        if (bActive)
        {
            return true;
        }

        if (!MagSetFullscreenColorEffect(const_cast<MAGCOLOREFFECT*>(&kGrayscaleMatrix)))
        {
            LOG_ERROR(L"MagSetFullscreenColorEffect failed (error %u)", GetLastError());
            return false;
        }

        bActive = true;
        LOG_INFO(L"Grayscale effect applied");
        return true;
    }

    bool GrayscaleEffect::RemoveGrayscale()
    {
        if (!bInitialized)
        {
            return false;
        }
        if (!bActive)
        {
            return true;
        }

        if (!MagSetFullscreenColorEffect(const_cast<MAGCOLOREFFECT*>(&kIdentityMatrix)))
        {
            LOG_ERROR(L"MagSetFullscreenColorEffect (restore) failed (error %u)", GetLastError());
            return false;
        }

        bActive = false;
        LOG_INFO(L"Grayscale effect removed");
        return true;
    }

    bool GrayscaleEffect::Toggle()
    {
        if (bActive)
        {
            RemoveGrayscale();
        } else {
            ApplyGrayscale();
        }
        return bActive;
    }

}
