#pragma once

#include <Windows.h>
#include <atomic>
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_2.h>
#include <thread>
#include <wrl/client.h>

namespace csp::d3d
{
    class D3DOverlayRenderer
    {
    public:
        D3DOverlayRenderer() = default;
        ~D3DOverlayRenderer();

        D3DOverlayRenderer(const D3DOverlayRenderer&) = delete;
        D3DOverlayRenderer& operator=(const D3DOverlayRenderer&) = delete;

        bool Initialize();
        void Shutdown();

        void Attach(HWND TargetWindow);
        void Detach();

        void Show();
        void Hide();

        void SetRefreshRate(int Fps);

        bool IsVisible() const { return Visible && HostWnd && IsWindowVisible(HostWnd); }
        bool IsInitialized() const { return Initialized; }
        HWND TargetWindow() const { return TargetWnd; }

    private:
        static LRESULT CALLBACK HostWndProc(HWND Hwnd, UINT Msg, WPARAM WParam, LPARAM LParam);

        bool CreateHostWindow();
        bool InitializeDevice();
        bool InitializeDeviceForMonitor(HMONITOR Monitor);
        bool CreateShaders();
        bool CreateSwapChain(int Width, int Height);
        bool ResizeSwapChain(int Width, int Height);
        bool EnsureDuplication();
        bool EnsureCaptureTexture(int Width, int Height);
        bool RenderFrame();
        bool UpdateTargetRect(RECT& TargetRect);

        void StartRefreshLoop();
        void StopRefreshLoop();

        void ReleaseDuplication();
        void ReleaseDeviceResources();
        void ReleaseSwapChainResources();

        UINT TimerIntervalMs = 16;
        std::atomic<bool> RefreshLoopRunning = false;
        std::atomic<bool> RefreshPending = false;
        std::thread RefreshThread;

        HWND HostWnd = nullptr;
        HWND TargetWnd = nullptr;
        bool Visible = false;
        bool Initialized = false;
        bool HasCapturedFrame = false;

        RECT LastRect = {};
        int BackBufferWidth = 0;
        int BackBufferHeight = 0;
        int CaptureWidth = 0;
        int CaptureHeight = 0;
        HMONITOR CaptureMonitor = nullptr;
        HMONITOR DeviceMonitor = nullptr;
        DXGI_OUTPUT_DESC OutputDesc = {};

        Microsoft::WRL::ComPtr<ID3D11Device> Device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> Context;
        Microsoft::WRL::ComPtr<IDXGISwapChain1> SwapChain;
        Microsoft::WRL::ComPtr<IDCompositionDevice> CompositionDevice;
        Microsoft::WRL::ComPtr<IDCompositionTarget> CompositionTarget;
        Microsoft::WRL::ComPtr<IDCompositionVisual> CompositionVisual;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> RenderTargetView;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> VertexShader;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> PixelShader;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> SamplerState;
        Microsoft::WRL::ComPtr<IDXGIOutputDuplication> Duplication;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> CaptureTexture;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> CaptureSrv;

        static D3DOverlayRenderer* Instance;
    };
}
