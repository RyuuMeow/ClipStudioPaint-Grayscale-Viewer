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
        void SetNotificationWindow(HWND Window, UINT Message) { NotifyWindow = Window; NotifyMessage = Message; }

        bool IsVisible() const { return Visible.load() && Presented.load(); }
        bool IsInitialized() const { return Initialized; }
        HWND TargetWindow() const { return RequestedTarget.load(); }
        bool HasFailed() const { return Failed.load(); }
        bool HasDeviceResources() const { return ResourcesAllocated.load(); }
        uint64_t PresentedFrameCount() const { return FramesPresented.load(); }

    private:
        static LRESULT CALLBACK HostWndProc(HWND Hwnd, UINT Msg, WPARAM WParam, LPARAM LParam);

        bool CreateHostWindow();
        void ApplyClickThroughStyles();
        bool InitializeDeviceForMonitor(HMONITOR Monitor);
        bool CreateShaders();
        bool CreateSwapChain(int Width, int Height);
        bool ResizeSwapChain(int Width, int Height);
        bool EnsureDuplication();
        bool EnsureCaptureTexture(int Width, int Height);
        bool RenderFrame();
        bool RevealFirstFrame();
        bool UpdateTargetRect(RECT& TargetRect);

        void StartRefreshLoop();
        void StopRefreshLoop();
        void RunWorker();
        void Wake();
        void Notify();
        void ReportFailure();

        void ReleaseDuplication();
        void ReleaseDeviceResources();
        void ReleaseSwapChainResources();

        std::atomic<bool> RefreshLoopRunning = false;
        std::atomic<UINT> RequestedFps = 60;
        std::atomic<HWND> RequestedTarget = nullptr;
        std::atomic<uint64_t> Revision = 0;
        std::atomic<bool> Presented = false;
        std::atomic<bool> Failed = false;
        std::atomic<bool> ResourcesAllocated = false;
        std::atomic<uint64_t> FramesPresented = 0;
        std::thread RefreshThread;
        HANDLE WakeEvent = nullptr;
        HANDLE ReadyEvent = nullptr;
        HANDLE PaceTimer = nullptr;
        HWND NotifyWindow = nullptr;
        UINT NotifyMessage = 0;
        bool WorkerReady = false;
        bool RecreateRequested = false;
        bool CaptureInvalid = true;
        bool PresentationPending = false;
        unsigned AccessLossCount = 0;
        uint64_t FrameRevision = 0;

        // Only the worker touches the HWND, D3D immediate context and COM resources.
        HWND HostWnd = nullptr;
        HWND TargetWnd = nullptr;
        std::atomic<bool> Visible = false;
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
