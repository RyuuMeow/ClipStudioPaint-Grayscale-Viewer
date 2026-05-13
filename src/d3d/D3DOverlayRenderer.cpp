#include "d3d/D3DOverlayRenderer.h"
#include "util/Logger.h"

#include <algorithm>
#include <chrono>
#include <d3dcompiler.h>
#include <dwmapi.h>

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
        constexpr UINT kRefreshMessage = WM_APP + 2;

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
        if (Initialized)
        {
            return true;
        }

        Instance = this;

        if (!CreateHostWindow())
        {
            Instance = nullptr;
            return false;
        }

        if (!InitializeDevice() || !CreateShaders() || !CreateSwapChain(1, 1))
        {
            Shutdown();
            return false;
        }

        Initialized = true;
        LOG_INFO(L"D3D overlay renderer initialized");
        return true;
    }

    void D3DOverlayRenderer::Shutdown()
    {
        if (!Initialized && !HostWnd)
        {
            return;
        }

        Hide();
        StopRefreshLoop();
        ReleaseDeviceResources();

        if (HostWnd)
        {
            DestroyWindow(HostWnd);
            HostWnd = nullptr;
        }

        TargetWnd = nullptr;
        CaptureMonitor = nullptr;
        DeviceMonitor = nullptr;
        Initialized = false;
        Instance = nullptr;
        LOG_INFO(L"D3D overlay renderer shut down");
    }

    void D3DOverlayRenderer::Attach(HWND TargetWindow)
    {
        TargetWnd = TargetWindow;
    }

    void D3DOverlayRenderer::Detach()
    {
        Hide();
        TargetWnd = nullptr;
    }

    void D3DOverlayRenderer::Show()
    {
        if (!Initialized || !TargetWnd || Visible)
        {
            return;
        }

        Visible = true;
        HasCapturedFrame = false;
        RenderFrame();
        StartRefreshLoop();
        LOG_INFO(L"D3D grayscale overlay shown");
    }

    void D3DOverlayRenderer::Hide()
    {
        if (!Visible)
        {
            return;
        }

        StopRefreshLoop();
        ShowWindow(HostWnd, SW_HIDE);
        Visible = false;
        HasCapturedFrame = false;
        LOG_INFO(L"D3D grayscale overlay hidden");
    }

    void D3DOverlayRenderer::SetRefreshRate(int Fps)
    {
        if (Fps < 1)
        {
            Fps = 1;
        }
        if (Fps > 240)
        {
            Fps = 240;
        }

        TimerIntervalMs = static_cast<UINT>(1000 / Fps);
        if (TimerIntervalMs < 1)
        {
            TimerIntervalMs = 1;
        }

        LOG_INFO(L"D3D refresh rate set to %d fps (%u ms)", Fps, TimerIntervalMs);
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
        return true;
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
        SetLayeredWindowAttributes(HostWnd, 0, 255, LWA_ALPHA);
        SetWindowDisplayAffinity(HostWnd, WDA_EXCLUDEFROMCAPTURE);
        SetWindowPos(
            HostWnd, HWND_TOPMOST,
            0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_FRAMECHANGED
        );
    }

    bool D3DOverlayRenderer::InitializeDevice()
    {
        HMONITOR monitor = MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
        return InitializeDeviceForMonitor(monitor);
    }

    bool D3DOverlayRenderer::InitializeDeviceForMonitor(HMONITOR Monitor)
    {
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
            if (factory->EnumAdapters1(adapterIndex, adapter.GetAddressOf()) == DXGI_ERROR_NOT_FOUND)
            {
                break;
            }

            for (UINT outputIndex = 0; ; ++outputIndex)
            {
                Microsoft::WRL::ComPtr<IDXGIOutput> output;
                if (adapter->EnumOutputs(outputIndex, output.GetAddressOf()) == DXGI_ERROR_NOT_FOUND)
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
        return true;
    }

    bool D3DOverlayRenderer::CreateShaders()
    {
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
        if (!Device || monitor != DeviceMonitor)
        {
            if (!InitializeDeviceForMonitor(monitor) ||
                !CreateShaders() ||
                !CreateSwapChain(BackBufferWidth > 0 ? BackBufferWidth : 1,
                                 BackBufferHeight > 0 ? BackBufferHeight : 1))
            {
                return false;
            }
        }

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
            if (adapter->EnumOutputs(i, output.GetAddressOf()) == DXGI_ERROR_NOT_FOUND)
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
        return true;
    }

    bool D3DOverlayRenderer::RenderFrame()
    {
        if (!TargetWnd || !IsWindow(TargetWnd))
        {
            Hide();
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
            return false;
        }

        RECT outputRect = OutputDesc.DesktopCoordinates;
        RECT captureRect = {};
        if (!IntersectRects(targetRect, outputRect, captureRect))
        {
            LOG_WARNING(L"D3D target rect is outside duplicated output");
            return false;
        }

        DXGI_OUTDUPL_FRAME_INFO frameInfo = {};
        Microsoft::WRL::ComPtr<IDXGIResource> desktopResource;
        HRESULT hr = Duplication->AcquireNextFrame(
            TimerIntervalMs, &frameInfo, desktopResource.GetAddressOf()
        );
        if (hr == DXGI_ERROR_WAIT_TIMEOUT)
        {
            if (!HasCapturedFrame)
            {
                return false;
            }
        }
        else if (hr == DXGI_ERROR_ACCESS_LOST)
        {
            ReleaseDuplication();
            return false;
        }
        else if (FAILED(hr))
        {
            LOG_WARNING(L"AcquireNextFrame failed (hr=0x%08X)", hr);
            return false;
        }

        if (desktopResource)
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
            }

            Duplication->ReleaseFrame();
        }

        if (!HasCapturedFrame)
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

        hr = SwapChain->Present(1, 0);
        if (FAILED(hr))
        {
            LOG_WARNING(L"D3D Present failed (hr=0x%08X)", hr);
            if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
            {
                HRESULT reason = Device ? Device->GetDeviceRemovedReason() : hr;
                LOG_WARNING(L"D3D device removed reason: hr=0x%08X", reason);
                ReleaseDeviceResources();
            }
            return false;
        }

        if (!IsWindowVisible(HostWnd))
        {
            ApplyClickThroughStyles();
            ShowWindow(HostWnd, SW_SHOWNOACTIVATE);
        }

        return true;
    }

    bool D3DOverlayRenderer::UpdateTargetRect(RECT& TargetRect)
    {
        if (IsIconic(TargetWnd))
        {
            ShowWindow(HostWnd, SW_HIDE);
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
            LastRect = TargetRect;
            SetWindowPos(
                HostWnd, HWND_TOPMOST,
                TargetRect.left, TargetRect.top, width, height,
                SWP_NOACTIVATE
            );
        }

        return true;
    }

    void D3DOverlayRenderer::StartRefreshLoop()
    {
        if (RefreshLoopRunning.exchange(true))
        {
            return;
        }

        RefreshPending.store(false);
        RefreshThread = std::thread([this]()
        {
            auto nextFrame = std::chrono::steady_clock::now();

            while (RefreshLoopRunning.load())
            {
                nextFrame += std::chrono::milliseconds(TimerIntervalMs);

                if (!RefreshPending.exchange(true))
                {
                    PostMessageW(HostWnd, kRefreshMessage, 0, 0);
                }

                std::this_thread::sleep_until(nextFrame);

                const auto now = std::chrono::steady_clock::now();
                if (nextFrame + std::chrono::milliseconds(TimerIntervalMs) < now)
                {
                    nextFrame = now;
                }
            }
        });
    }

    void D3DOverlayRenderer::StopRefreshLoop()
    {
        if (!RefreshLoopRunning.exchange(false))
        {
            return;
        }

        if (RefreshThread.joinable())
        {
            RefreshThread.join();
        }

        RefreshPending.store(false);
    }

    void D3DOverlayRenderer::ReleaseDuplication()
    {
        Duplication.Reset();
        CaptureMonitor = nullptr;
    }

    void D3DOverlayRenderer::ReleaseDeviceResources()
    {
        ReleaseDuplication();
        ReleaseSwapChainResources();

        CaptureSrv.Reset();
        CaptureTexture.Reset();
        CaptureWidth = 0;
        CaptureHeight = 0;
        HasCapturedFrame = false;

        SamplerState.Reset();
        PixelShader.Reset();
        VertexShader.Reset();
        SwapChain.Reset();
        CompositionVisual.Reset();
        CompositionTarget.Reset();
        CompositionDevice.Reset();
        Context.Reset();
        Device.Reset();
        DeviceMonitor = nullptr;
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
        case kRefreshMessage:
            if (Instance)
            {
                Instance->RefreshPending.store(false);
                Instance->RenderFrame();
            }
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
