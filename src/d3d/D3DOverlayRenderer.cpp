#include "d3d/D3DOverlayRenderer.h"
#include "util/Logger.h"
#include "d3d/RenderPolicy.h"
#include "window/WindowGeometry.h"

#include <algorithm>
#include <chrono>
#include <d3dcompiler.h>
#include <dwmapi.h>
#include <dxgi1_3.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dcomp.lib")

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

namespace csp::d3d
{
    D3DOverlayRenderer* D3DOverlayRenderer::Instance = nullptr;

    namespace
    {
        constexpr wchar_t kHostClassName[] = L"CSP_D3DOverlay";

        struct SetupTiming
        {
            const wchar_t* Stage;
            std::chrono::steady_clock::time_point Start = std::chrono::steady_clock::now();
            ~SetupTiming()
            {
                const auto ms = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - Start).count();
                LOG_DEBUG(L"D3D setup %s: %.2f ms", Stage, ms);
            }
        };

        bool IntersectRects(const RECT& A, const RECT& B, RECT& Out)
        {
            Out.left = std::max(A.left, B.left);
            Out.top = std::max(A.top, B.top);
            Out.right = std::min(A.right, B.right);
            Out.bottom = std::min(A.bottom, B.bottom);
            return Out.left < Out.right && Out.top < Out.bottom;
        }

        constexpr char kShaderSource[] = R"(
            Texture2D SourceTexture : register(t0);
            SamplerState LinearSampler : register(s0);

            struct VsOut
            {
                float4 Position : SV_Position;
                float2 Uv : TEXCOORD0;
            };

            VsOut VsMain(uint VertexId : SV_VertexID)
            {
                float2 Positions[3] =
                {
                    float2(-1.0f, -1.0f),
                    float2(-1.0f,  3.0f),
                    float2( 3.0f, -1.0f)
                };

                float2 Uvs[3] =
                {
                    float2(0.0f, 1.0f),
                    float2(0.0f, -1.0f),
                    float2(2.0f, 1.0f)
                };

                VsOut Out;
                Out.Position = float4(Positions[VertexId], 0.0f, 1.0f);
                Out.Uv = Uvs[VertexId];
                return Out;
            }

            float4 PsMain(VsOut Input) : SV_Target
            {
                float4 Color = SourceTexture.Sample(LinearSampler, Input.Uv);
                float Gray = dot(Color.rgb, float3(0.299f, 0.587f, 0.114f));
                return float4(Gray, Gray, Gray, 1.0f);
            }
        )";
    }

    D3DOverlayRenderer::~D3DOverlayRenderer()
    {
        Shutdown();
    }

    bool D3DOverlayRenderer::Initialize()
    {
        if (Initialized) return true;
        // Earlier Windows versions accept the flag but capture a black rectangle.
        // WDA_EXCLUDEFROMCAPTURE requires Windows 10 2004 (build 19041).
        OSVERSIONINFOEXW minimum{sizeof(minimum)};
        minimum.dwBuildNumber = 19041;
        if (!VerifyVersionInfoW(&minimum, VER_BUILDNUMBER,
                VerSetConditionMask(0, VER_BUILDNUMBER, VER_GREATER_EQUAL)))
        {
            LOG_WARNING(L"D3D capture exclusion requires Windows 10 2004 or later");
            return false;
        }
        WakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        ReadyEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!WakeEvent || !ReadyEvent) { Shutdown(); return false; }
        Failed.store(false);
        RefreshLoopRunning.store(true);
        try { RefreshThread = std::thread([this] { RunWorker(); }); }
        catch (const std::exception&) { Shutdown(); return false; }
        WaitForSingleObject(ReadyEvent, INFINITE);
        Initialized = WorkerReady;
        if (!Initialized) { Shutdown(); return false; }
        return true;
    }

    void D3DOverlayRenderer::Shutdown()
    {
        Visible.store(false);
        Revision.fetch_add(1);
        StopRefreshLoop();
        if (ReadyEvent) { CloseHandle(ReadyEvent); ReadyEvent = nullptr; }
        if (WakeEvent) { CloseHandle(WakeEvent); WakeEvent = nullptr; }
        RequestedTarget.store(nullptr);
        Initialized = false;
        WorkerReady = false;
        Presented.store(false);
    }

    void D3DOverlayRenderer::Attach(HWND TargetWindow)
    {
        if (RequestedTarget.exchange(TargetWindow) != TargetWindow)
        {
            Revision.fetch_add(1);
            Wake();
        }
    }

    void D3DOverlayRenderer::Detach()
    {
        Hide();
        RequestedTarget.store(nullptr);
        Revision.fetch_add(1);
        Wake();
    }

    void D3DOverlayRenderer::Show()
    {
        if (!Initialized || !RequestedTarget.load() || Visible.exchange(true)) return;
        Failed.store(false);
        Revision.fetch_add(1);
        StartRefreshLoop();
    }

    void D3DOverlayRenderer::Hide()
    {
        if (!Visible.exchange(false)) return;
        Presented.store(false);
        Revision.fetch_add(1);
        Wake();
    }

    void D3DOverlayRenderer::SetRefreshRate(int Fps)
    {
        RequestedFps.store(static_cast<UINT>(std::clamp(Fps, 15, 144)));
        Wake();
    }

    bool D3DOverlayRenderer::CreateHostWindow()
    {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = HostWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kHostClassName;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);

        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            LOG_ERROR(L"D3D RegisterClassEx failed (error %u)", GetLastError());
            return false;
        }

        HostWnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED |
            WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP,
            kHostClassName,
            L"",
            WS_POPUP,
            0, 0, 1, 1,
            nullptr, nullptr,
            GetModuleHandleW(nullptr),
            nullptr
        );

        if (!HostWnd)
        {
            LOG_ERROR(L"D3D host window creation failed (error %u)", GetLastError());
            return false;
        }

        ApplyClickThroughStyles();
        return !Failed.load();
    }

    void D3DOverlayRenderer::ApplyClickThroughStyles()
    {
        if (!HostWnd)
        {
            return;
        }

        LONG_PTR exStyle = GetWindowLongPtrW(HostWnd, GWL_EXSTYLE);
        exStyle |= WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED |
                   WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP;
        SetWindowLongPtrW(HostWnd, GWL_EXSTYLE, exStyle);
        // Do not expose the opaque swap chain before its first frame is composed.
        SetLayeredWindowAttributes(HostWnd, 0, 0, LWA_ALPHA);
        if (!SetWindowDisplayAffinity(HostWnd, WDA_EXCLUDEFROMCAPTURE))
        {
            LOG_ERROR(L"Cannot exclude D3D overlay from capture (error %u)", GetLastError());
            ReportFailure();
        }
        SetWindowPos(
            HostWnd, HWND_TOPMOST,
            0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_FRAMECHANGED
        );
    }

    bool D3DOverlayRenderer::InitializeDeviceForMonitor(HMONITOR Monitor)
    {
        const SetupTiming timing{L"device"};
        ReleaseDeviceResources();

        Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
        HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(factory.GetAddressOf()));
        if (FAILED(hr))
        {
            LOG_ERROR(L"CreateDXGIFactory1 failed (hr=0x%08X)", hr);
            return false;
        }

        Microsoft::WRL::ComPtr<IDXGIAdapter1> selectedAdapter;
        for (UINT adapterIndex = 0; ; ++adapterIndex)
        {
            Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
            if (FAILED(factory->EnumAdapters1(adapterIndex, adapter.GetAddressOf())))
            {
                break;
            }

            for (UINT outputIndex = 0; ; ++outputIndex)
            {
                Microsoft::WRL::ComPtr<IDXGIOutput> output;
                if (FAILED(adapter->EnumOutputs(outputIndex, output.GetAddressOf())))
                {
                    break;
                }

                DXGI_OUTPUT_DESC desc = {};
                if (SUCCEEDED(output->GetDesc(&desc)) && desc.Monitor == Monitor)
                {
                    selectedAdapter = adapter;
                    OutputDesc = desc;
                    break;
                }
            }

            if (selectedAdapter)
            {
                break;
            }
        }

        if (!selectedAdapter)
        {
            LOG_ERROR(L"No DXGI adapter found for target monitor");
            return false;
        }

        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

        D3D_FEATURE_LEVEL levels[] =
        {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0
        };

        D3D_FEATURE_LEVEL createdLevel = {};
        hr = D3D11CreateDevice(
            selectedAdapter.Get(),
            D3D_DRIVER_TYPE_UNKNOWN,
            nullptr,
            flags,
            levels,
            _countof(levels),
            D3D11_SDK_VERSION,
            Device.GetAddressOf(),
            &createdLevel,
            Context.GetAddressOf()
        );

        if (FAILED(hr))
        {
            LOG_ERROR(L"D3D11CreateDevice failed (hr=0x%08X)", hr);
            return false;
        }

        DeviceMonitor = Monitor;
        DevicesCreated.fetch_add(1);
        ResourcesAllocated.store(true);
        return true;
    }

    bool D3DOverlayRenderer::CreateShaders()
    {
        const SetupTiming timing{L"shaders"};
        Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
        Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
        Microsoft::WRL::ComPtr<ID3DBlob> errors;

        HRESULT hr = D3DCompile(
            kShaderSource, sizeof(kShaderSource), nullptr, nullptr, nullptr,
            "VsMain", "vs_5_0", 0, 0, vsBlob.GetAddressOf(), errors.GetAddressOf()
        );
        if (FAILED(hr))
        {
            LOG_ERROR(L"D3D vertex shader compile failed (hr=0x%08X)", hr);
            return false;
        }

        errors.Reset();
        hr = D3DCompile(
            kShaderSource, sizeof(kShaderSource), nullptr, nullptr, nullptr,
            "PsMain", "ps_5_0", 0, 0, psBlob.GetAddressOf(), errors.GetAddressOf()
        );
        if (FAILED(hr))
        {
            LOG_ERROR(L"D3D pixel shader compile failed (hr=0x%08X)", hr);
            return false;
        }

        hr = Device->CreateVertexShader(
            vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, VertexShader.GetAddressOf()
        );
        if (FAILED(hr))
        {
            LOG_ERROR(L"CreateVertexShader failed (hr=0x%08X)", hr);
            return false;
        }

        hr = Device->CreatePixelShader(
            psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, PixelShader.GetAddressOf()
        );
        if (FAILED(hr))
        {
            LOG_ERROR(L"CreatePixelShader failed (hr=0x%08X)", hr);
            return false;
        }

        D3D11_SAMPLER_DESC sampler = {};
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sampler.MinLOD = 0.0f;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;

        hr = Device->CreateSamplerState(&sampler, SamplerState.GetAddressOf());
        if (FAILED(hr))
        {
            LOG_ERROR(L"CreateSamplerState failed (hr=0x%08X)", hr);
            return false;
        }

        return true;
    }

    bool D3DOverlayRenderer::CreateSwapChain(int Width, int Height)
    {
        const SetupTiming timing{L"composition"};
        Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
        Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        Microsoft::WRL::ComPtr<IDXGIFactory2> factory;

        if (FAILED(Device.As(&dxgiDevice)) ||
            FAILED(dxgiDevice->GetAdapter(adapter.GetAddressOf())) ||
            FAILED(adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(factory.GetAddressOf()))))
        {
            LOG_ERROR(L"Failed to get DXGI factory for swap chain");
            return false;
        }

        DXGI_SWAP_CHAIN_DESC1 desc = {};
        desc.Width = Width;
        desc.Height = Height;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.SampleDesc.Count = 1;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

        HRESULT hr = factory->CreateSwapChainForComposition(
            Device.Get(), &desc, nullptr, SwapChain.GetAddressOf()
        );
        if (FAILED(hr))
        {
            LOG_ERROR(L"CreateSwapChainForComposition failed (hr=0x%08X)", hr);
            return false;
        }
        FrameResourcesAllocated.store(true);

        hr = DCompositionCreateDevice(
            dxgiDevice.Get(),
            __uuidof(IDCompositionDevice),
            reinterpret_cast<void**>(CompositionDevice.GetAddressOf())
        );
        if (FAILED(hr))
        {
            LOG_ERROR(L"DCompositionCreateDevice failed (hr=0x%08X)", hr);
            return false;
        }

        hr = CompositionDevice->CreateTargetForHwnd(
            HostWnd, TRUE, CompositionTarget.GetAddressOf()
        );
        if (FAILED(hr))
        {
            LOG_ERROR(L"CreateTargetForHwnd failed (hr=0x%08X)", hr);
            return false;
        }

        hr = CompositionDevice->CreateVisual(CompositionVisual.GetAddressOf());
        if (FAILED(hr))
        {
            LOG_ERROR(L"CreateVisual failed (hr=0x%08X)", hr);
            return false;
        }

        hr = CompositionVisual->SetContent(SwapChain.Get());
        if (FAILED(hr))
        {
            LOG_ERROR(L"DirectComposition SetContent failed (hr=0x%08X)", hr);
            return false;
        }

        hr = CompositionTarget->SetRoot(CompositionVisual.Get());
        if (FAILED(hr))
        {
            LOG_ERROR(L"DirectComposition SetRoot failed (hr=0x%08X)", hr);
            return false;
        }

        hr = CompositionDevice->Commit();
        if (FAILED(hr))
        {
            LOG_ERROR(L"DirectComposition Commit failed (hr=0x%08X)", hr);
            return false;
        }

        BackBufferWidth = Width;
        BackBufferHeight = Height;
        return ResizeSwapChain(Width, Height);
    }

    bool D3DOverlayRenderer::ResizeSwapChain(int Width, int Height)
    {
        if (Width <= 0 || Height <= 0)
        {
            return false;
        }

        if (Width != BackBufferWidth || Height != BackBufferHeight)
        {
            ReleaseSwapChainResources();

            HRESULT hr = SwapChain->ResizeBuffers(
                2, Width, Height, DXGI_FORMAT_B8G8R8A8_UNORM, 0
            );
            if (FAILED(hr))
            {
                LOG_ERROR(L"ResizeBuffers failed (hr=0x%08X)", hr);
                return false;
            }

            if (CompositionDevice)
            {
                CompositionDevice->Commit();
            }

            BackBufferWidth = Width;
            BackBufferHeight = Height;
        }

        if (!RenderTargetView)
        {
            Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
            HRESULT hr = SwapChain->GetBuffer(
                0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(backBuffer.GetAddressOf())
            );
            if (FAILED(hr))
            {
                LOG_ERROR(L"SwapChain GetBuffer failed (hr=0x%08X)", hr);
                return false;
            }

            hr = Device->CreateRenderTargetView(
                backBuffer.Get(), nullptr, RenderTargetView.GetAddressOf()
            );
            if (FAILED(hr))
            {
                LOG_ERROR(L"CreateRenderTargetView failed (hr=0x%08X)", hr);
                return false;
            }
        }

        return true;
    }

    bool D3DOverlayRenderer::EnsureDuplication()
    {
        HMONITOR monitor = MonitorFromWindow(TargetWnd, MONITOR_DEFAULTTONEAREST);
        // A cached device can be lost while the renderer is idle.
        if (Device && !Duplication && FAILED(Device->GetDeviceRemovedReason()))
            ReleaseDeviceResources();
        if (!Device || monitor != DeviceMonitor)
        {
            if (!InitializeDeviceForMonitor(monitor) || !CreateShaders())
            {
                return false;
            }
        }
        // Idle trimming keeps the expensive device/shaders, but drops image buffers.
        if (!SwapChain && !CreateSwapChain(BackBufferWidth > 0 ? BackBufferWidth : 1,
                                           BackBufferHeight > 0 ? BackBufferHeight : 1))
            return false;

        if (Duplication && monitor == CaptureMonitor)
        {
            return true;
        }

        ReleaseDuplication();

        Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
        Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        if (FAILED(Device.As(&dxgiDevice)) || FAILED(dxgiDevice->GetAdapter(adapter.GetAddressOf())))
        {
            LOG_ERROR(L"Failed to get DXGI adapter for desktop duplication");
            return false;
        }

        for (UINT i = 0; ; ++i)
        {
            Microsoft::WRL::ComPtr<IDXGIOutput> output;
            if (FAILED(adapter->EnumOutputs(i, output.GetAddressOf())))
            { break; }
            DXGI_OUTPUT_DESC desc = {};
            if (FAILED(output->GetDesc(&desc)) || desc.Monitor != monitor)
            {
                continue;
            }

            Microsoft::WRL::ComPtr<IDXGIOutput1> output1;
            if (FAILED(output.As(&output1)))
            {
                continue;
            }

            const SetupTiming timing{L"duplication"};
            HRESULT hr = output1->DuplicateOutput(Device.Get(), Duplication.GetAddressOf());
            if (FAILED(hr))
            {
                LOG_ERROR(L"DuplicateOutput failed (hr=0x%08X)", hr);
                return false;
            }

            CaptureMonitor = monitor;
            OutputDesc = desc;
            return true;
        }

        LOG_ERROR(L"No matching DXGI output found for target window");
        return false;
    }

    bool D3DOverlayRenderer::EnsureCaptureTexture(int Width, int Height)
    {
        if (CaptureTexture && Width == CaptureWidth && Height == CaptureHeight)
        {
            return true;
        }

        CaptureSrv.Reset();
        CaptureTexture.Reset();

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = Width;
        desc.Height = Height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        HRESULT hr = Device->CreateTexture2D(&desc, nullptr, CaptureTexture.GetAddressOf());
        if (FAILED(hr))
        {
            LOG_ERROR(L"CreateTexture2D capture texture failed (hr=0x%08X)", hr);
            return false;
        }

        hr = Device->CreateShaderResourceView(
            CaptureTexture.Get(), nullptr, CaptureSrv.GetAddressOf()
        );
        if (FAILED(hr))
        {
            LOG_ERROR(L"CreateShaderResourceView failed (hr=0x%08X)", hr);
            return false;
        }

        CaptureWidth = Width;
        CaptureHeight = Height;
        HasCapturedFrame = false;
        CaptureInvalid = true;
        return true;
    }

    bool D3DOverlayRenderer::RenderFrame()
    {
        if (!Visible.load() || Revision.load() != FrameRevision) return false;
        if (!TargetWnd || !IsWindow(TargetWnd))
        {
            Visible.store(false);
            ShowWindow(HostWnd, SW_HIDE);
            Presented.store(false);
            Notify();
            return false;
        }

        RECT targetRect = {};
        if (!UpdateTargetRect(targetRect))
        {
            return false;
        }

        const int width = targetRect.right - targetRect.left;
        const int height = targetRect.bottom - targetRect.top;
        if (!EnsureDuplication() || !ResizeSwapChain(width, height) || !EnsureCaptureTexture(width, height))
        {
            ReportFailure();
            return false;
        }

        RECT outputRect = OutputDesc.DesktopCoordinates;
        if (!CanCaptureWindow(targetRect, outputRect,
                OutputDesc.Rotation == DXGI_MODE_ROTATION_IDENTITY ? DMDO_DEFAULT : DMDO_90))
        {
            ReportFailure();
            return false;
        }
        RECT captureRect = {};
        if (!IntersectRects(targetRect, outputRect, captureRect))
        {
            LOG_WARNING(L"D3D target rect is outside duplicated output");
            return false;
        }

        DXGI_OUTDUPL_FRAME_INFO frameInfo = {};
        Microsoft::WRL::ComPtr<IDXGIResource> desktopResource;
        HRESULT hr = Duplication->AcquireNextFrame(
            0, &frameInfo, desktopResource.GetAddressOf()
        );
        if (hr == DXGI_ERROR_WAIT_TIMEOUT)
        {
            if (!HasCapturedFrame || !PresentationPending)
            {
                return false;
            }
        }
        else if (hr == DXGI_ERROR_ACCESS_LOST)
        {
            ReleaseDuplication();
            HasCapturedFrame = false;
            CaptureInvalid = true;
            ShowWindow(HostWnd, SW_HIDE);
            Presented.store(false);
            if (++AccessLossCount >= 3) ReportFailure();
            return false;
        }
        else if (FAILED(hr))
        {
            LOG_WARNING(L"AcquireNextFrame failed (hr=0x%08X)", hr);
            ReleaseDeviceResources();
            ReportFailure();
            return false;
        }

        if (desktopResource)
        {
            AccessLossCount = 0;
            // A new duplication session can first return only a pointer update,
            // with no initialized desktop image. Never copy it, even when the
            // crop is invalid or we have not captured our first frame yet.
            if (frameInfo.LastPresentTime.QuadPart == 0)
            {
                Duplication->ReleaseFrame();
                if (!PresentationPending) return false;
            }
            else
            {
            Microsoft::WRL::ComPtr<ID3D11Texture2D> desktopTexture;
            hr = desktopResource.As(&desktopTexture);
            if (SUCCEEDED(hr))
            {
                D3D11_BOX sourceBox = {};
                sourceBox.left = static_cast<UINT>(captureRect.left - outputRect.left);
                sourceBox.top = static_cast<UINT>(captureRect.top - outputRect.top);
                sourceBox.right = static_cast<UINT>(captureRect.right - outputRect.left);
                sourceBox.bottom = static_cast<UINT>(captureRect.bottom - outputRect.top);
                sourceBox.front = 0;
                sourceBox.back = 1;

                const UINT destX = static_cast<UINT>(captureRect.left - targetRect.left);
                const UINT destY = static_cast<UINT>(captureRect.top - targetRect.top);
                Context->CopySubresourceRegion(
                    CaptureTexture.Get(), 0, destX, destY, 0,
                    desktopTexture.Get(), 0, &sourceBox
                );
                HasCapturedFrame = true;
                CaptureInvalid = false;
                PresentationPending = true;
            }

            Duplication->ReleaseFrame();
            }
        }

        if (!HasCapturedFrame || !Visible.load() || Revision.load() != FrameRevision)
        {
            return false;
        }

        FLOAT clear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        Context->OMSetRenderTargets(1, RenderTargetView.GetAddressOf(), nullptr);
        Context->ClearRenderTargetView(RenderTargetView.Get(), clear);

        D3D11_VIEWPORT viewport = {};
        viewport.Width = static_cast<float>(width);
        viewport.Height = static_cast<float>(height);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        Context->RSSetViewports(1, &viewport);

        ID3D11ShaderResourceView* srvs[] = { CaptureSrv.Get() };
        ID3D11SamplerState* samplers[] = { SamplerState.Get() };
        Context->VSSetShader(VertexShader.Get(), nullptr, 0);
        Context->PSSetShader(PixelShader.Get(), nullptr, 0);
        Context->PSSetShaderResources(0, 1, srvs);
        Context->PSSetSamplers(0, 1, samplers);
        Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        Context->Draw(3, 0);

        ID3D11ShaderResourceView* nullSrvs[] = { nullptr };
        Context->PSSetShaderResources(0, 1, nullSrvs);

        // A present submitted while hidden can be discarded by the compositor.
        // Prime the host at minimum nonzero opacity before queueing the first one.
        if (!IsWindowVisible(HostWnd))
        {
            if (!SetLayeredWindowAttributes(HostWnd, 0, 1, LWA_ALPHA))
            {
                LOG_ERROR(L"Cannot prepare transparent D3D host (error %u)", GetLastError());
                ReportFailure();
                return false;
            }
            ShowWindow(HostWnd, SW_SHOWNOACTIVATE);
        }
        // Never wait for the GPU in the control/UI thread (or build a frame backlog).
        hr = SwapChain->Present(1, DXGI_PRESENT_DO_NOT_WAIT);
        if (hr == DXGI_ERROR_WAS_STILL_DRAWING || hr == DXGI_STATUS_OCCLUDED) return false;
        if (FAILED(hr))
        {
            LOG_WARNING(L"D3D Present failed (hr=0x%08X)", hr);
            if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
            {
                HRESULT reason = Device ? Device->GetDeviceRemovedReason() : hr;
                LOG_WARNING(L"D3D device removed reason: hr=0x%08X", reason);
                ReleaseDeviceResources();
            }
            ReportFailure();
            return false;
        }

        PresentationPending = false;
        FramesPresented.fetch_add(1);
        if (!Visible.load() || Revision.load() != FrameRevision) return false;
        if (!Presented.load() && !RevealFirstFrame()) return false;
        if (!Presented.exchange(true)) Notify();

        return true;
    }

    bool D3DOverlayRenderer::RevealFirstFrame()
    {
        // A successful Present is a queued frame, not a completed composition.
        // Synchronize only this reveal; normal drawing keeps its nonblocking path.
        // DXGI present counters are not reliable across multiple monitors. DwmFlush
        // waits for this application's queued DirectX updates to reach the screen.
        const auto start = std::chrono::steady_clock::now();
        HRESULT hr = CompositionDevice->WaitForCommitCompletion();
        if (SUCCEEDED(hr)) hr = DwmFlush();
        if (!RefreshLoopRunning.load() || !Visible.load() || Revision.load() != FrameRevision)
        {
            ShowWindow(HostWnd, SW_HIDE);
            return false;
        }
        if (SUCCEEDED(hr) && !SetLayeredWindowAttributes(HostWnd, 0, 255, LWA_ALPHA))
            hr = HRESULT_FROM_WIN32(GetLastError());
        if (FAILED(hr))
        {
            LOG_WARNING(L"Cannot complete first D3D composition (hr=0x%08X)", hr);
            ReportFailure();
            return false;
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        LOG_DEBUG(L"First valid frame flushed: wait=%lld ms", elapsed);
        return true;
    }

    bool D3DOverlayRenderer::UpdateTargetRect(RECT& TargetRect)
    {
        if (IsIconic(TargetWnd))
        {
            ShowWindow(HostWnd, SW_HIDE);
            if (Presented.exchange(false)) Notify();
            return false;
        }

        if (FAILED(DwmGetWindowAttribute(
            TargetWnd, DWMWA_EXTENDED_FRAME_BOUNDS, &TargetRect, sizeof(TargetRect))))
        {
            if (!GetWindowRect(TargetWnd, &TargetRect))
            {
                return false;
            }
        }

        MONITORINFO monitor{sizeof(monitor)};
        if (GetMonitorInfoW(MonitorFromWindow(TargetWnd, MONITOR_DEFAULTTONEAREST), &monitor))
            TargetRect = window::VisibleCaptureBounds(TargetWnd, TargetRect, monitor.rcMonitor);

        const int width = TargetRect.right - TargetRect.left;
        const int height = TargetRect.bottom - TargetRect.top;
        if (width <= 0 || height <= 0)
        {
            return false;
        }

        const bool rectChanged =
            TargetRect.left != LastRect.left ||
            TargetRect.top != LastRect.top ||
            TargetRect.right != LastRect.right ||
            TargetRect.bottom != LastRect.bottom;

        if (rectChanged)
        {
            CaptureInvalid = true;
            LastRect = TargetRect;
            SetWindowPos(
                HostWnd, HWND_TOPMOST,
                TargetRect.left, TargetRect.top, width, height,
                SWP_NOACTIVATE
            );
        }

        return true;
    }

    void D3DOverlayRenderer::Wake()
    {
        if (WakeEvent) SetEvent(WakeEvent);
    }

    void D3DOverlayRenderer::Notify()
    {
        if (NotifyWindow) PostMessageW(NotifyWindow, NotifyMessage, 0, 0);
    }

    void D3DOverlayRenderer::ReportFailure()
    {
        if (!Failed.exchange(true))
        {
            Visible.store(false);
            Presented.store(false);
            ShowWindow(HostWnd, SW_HIDE);
            ReleaseDeviceResources();
            LOG_WARNING(L"D3D unavailable; requesting Magnification compatibility mode");
            Notify();
        }
    }

    void D3DOverlayRenderer::StartRefreshLoop()
    {
        Wake();
    }

    void D3DOverlayRenderer::StopRefreshLoop()
    {
        RefreshLoopRunning.store(false);
        Wake();
        if (RefreshThread.joinable()) RefreshThread.join();
    }

    void D3DOverlayRenderer::RunWorker()
    {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        Instance = this;
        LastRect = {};
        // A one-shot high-resolution timer paces active frames; inactive mode has no tick.
        PaceTimer = CreateWaitableTimerExW(nullptr, nullptr, 0x2, TIMER_ALL_ACCESS);
        if (!PaceTimer) PaceTimer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
        WorkerReady = SUCCEEDED(com) && PaceTimer && CreateHostWindow();
        SetEvent(ReadyEvent);
        if (WorkerReady)
        {
            using Clock = std::chrono::steady_clock;
            auto nextFrame = Clock::now();
            auto idleSince = Clock::now();
            bool wasActive = false;
            uint64_t lastRevision = 0;
            while (RefreshLoopRunning.load())
            {
                MSG msg{};
                // A bounded drain prevents unrelated window traffic from starving commands.
                for (int i = 0; i < 32 && PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE); ++i)
                {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
                const auto revision = Revision.load();
                const bool active = Visible.load() && RequestedTarget.load() && !Failed.load();
                if (revision != lastRevision)
                {
                    // Invalidate all work from the previous target/visibility generation.
                    TargetWnd = RequestedTarget.load();
                    HasCapturedFrame = false;
                    CaptureInvalid = true;
                    PresentationPending = false;
                    ReleaseDuplication();
                    ShowWindow(HostWnd, SW_HIDE);
                    Presented.store(false);
                    AccessLossCount = 0;
                    nextFrame = Clock::now();
                    lastRevision = revision;
                }
                if (RecreateRequested)
                {
                    ReleaseDeviceResources();
                    RecreateRequested = false;
                }
                if (!active)
                {
                    if (wasActive)
                    {
                        ShowWindow(HostWnd, SW_HIDE);
                        Presented.store(false);
                        CancelWaitableTimer(PaceTimer);
                        idleSince = Clock::now();
                        Notify();
                    }
                    wasActive = false;
                    DWORD wait = INFINITE;
                    if (SwapChain)
                    {
                        const auto idleMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - idleSince).count();
                        if (idleMs >= 5000)
                        {
                            ReleaseFrameResources();
                            // Trim driver caches without paying device creation again.
                            Microsoft::WRL::ComPtr<IDXGIDevice3> dxgiDevice;
                            if (SUCCEEDED(Device.As(&dxgiDevice))) dxgiDevice->Trim();
                            else Context->Flush();
                        }
                        else wait = static_cast<DWORD>(5000 - idleMs);
                    }
                    MsgWaitForMultipleObjectsEx(1, &WakeEvent, wait, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                    continue;
                }
                wasActive = true;
                const UINT fps = RequestedFps.load();
                if (Clock::now() >= nextFrame)
                {
                    FrameRevision = revision;
                    RenderFrame();
                    const auto period = std::chrono::nanoseconds(1000000000LL / fps);
                    nextFrame += period;
                    if (nextFrame < Clock::now()) nextFrame = Clock::now() + period;
                }
                const auto remaining = std::chrono::duration_cast<std::chrono::nanoseconds>(nextFrame - Clock::now()).count();
                LARGE_INTEGER due{};
                due.QuadPart = -std::max<long long>(1, remaining / 100);
                if (!SetWaitableTimer(PaceTimer, &due, 0, nullptr, nullptr, FALSE))
                {
                    LOG_ERROR(L"Cannot arm D3D pacing timer (error %u)", GetLastError());
                    ReportFailure();
                    continue;
                }
                HANDLE waits[]{WakeEvent, PaceTimer};
                MsgWaitForMultipleObjectsEx(2, waits, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            }
        }
        if (HostWnd) ShowWindow(HostWnd, SW_HIDE);
        ReleaseDeviceResources();
        if (HostWnd) { DestroyWindow(HostWnd); HostWnd = nullptr; }
        if (PaceTimer) { CloseHandle(PaceTimer); PaceTimer = nullptr; }
        TargetWnd = nullptr;
        Instance = nullptr;
        Presented.store(false);
        if (SUCCEEDED(com)) CoUninitialize();
    }

    void D3DOverlayRenderer::ReleaseDuplication()
    {
        Duplication.Reset();
        CaptureMonitor = nullptr;
    }

    void D3DOverlayRenderer::ReleaseDeviceResources()
    {
        ReleaseFrameResources();
        SamplerState.Reset();
        PixelShader.Reset();
        VertexShader.Reset();
        Context.Reset();
        Device.Reset();
        DeviceMonitor = nullptr;
        ResourcesAllocated.store(false);
    }

    void D3DOverlayRenderer::ReleaseFrameResources()
    {
        // A rebuilt composition surface must pass the first-frame gate again.
        if (HostWnd) ShowWindow(HostWnd, SW_HIDE);
        Presented.store(false);
        // Clear context references before releasing textures or trimming the driver.
        if (Context) Context->ClearState();
        ReleaseDuplication();
        ReleaseSwapChainResources();

        CaptureSrv.Reset();
        CaptureTexture.Reset();
        CaptureWidth = 0;
        CaptureHeight = 0;
        HasCapturedFrame = false;

        SwapChain.Reset();
        CompositionVisual.Reset();
        CompositionTarget.Reset();
        CompositionDevice.Reset();
        FrameResourcesAllocated.store(false);
        CaptureInvalid = true;
        PresentationPending = false;
    }

    void D3DOverlayRenderer::ReleaseSwapChainResources()
    {
        if (Context)
        {
            ID3D11RenderTargetView* nullTargets[] = { nullptr };
            Context->OMSetRenderTargets(1, nullTargets, nullptr);
        }
        RenderTargetView.Reset();
    }

    LRESULT CALLBACK D3DOverlayRenderer::HostWndProc(
        HWND Hwnd, UINT Msg, WPARAM WParam, LPARAM LParam)
    {
        switch (Msg)
        {
        case WM_DISPLAYCHANGE:
            if (Instance) { Instance->RecreateRequested = true; Instance->Wake(); }
            return 0;

        case WM_NCHITTEST:
            return HTTRANSPARENT;

        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;

        case WM_SETCURSOR:
            SetCursor(nullptr);
            return TRUE;

        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MOUSEMOVE:
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
            return 0;

        case WM_DESTROY:
            return 0;
        }

        return DefWindowProcW(Hwnd, Msg, WParam, LParam);
    }
}
