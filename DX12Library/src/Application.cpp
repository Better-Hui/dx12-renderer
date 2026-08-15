// ReSharper disable CppRedundantQualifier
#include "DX12LibPCH.h"
#include "Application.h"
#include "ApplicationResources.h"

#include "CommandQueue.h"
#include "DiagnosticReporter.h"
#include "Game.h"
//Modify Begin:2026-08-07 by BestHui
#include "StreamlineRuntime.h"
#include "D3D12DeviceContext.h"
//Modify End
#include "Window.h"
#include <ctime>
//Modify Begin:2026-07-28 by BestHui
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <stdexcept>
//Modify End

constexpr wchar_t WINDOW_CLASS_NAME[] = L"DX12RenderWindowClass";

using WindowPtr = std::shared_ptr<Window>;
using WindowMap = std::map<HWND, WindowPtr>;
using WindowNameMap = std::map<std::wstring, WindowPtr>;

static Application* gs_pSingelton = nullptr;
static WindowMap gs_Windows;
static WindowNameMap gs_WindowByName;

uint64_t Application::s_FrameCount = 0;

static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

//Modify Begin:2026-07-21 by BestHui
#if defined(DX12_RENDERER_ENABLE_D3D12_AGILITY) && DX12_RENDERER_ENABLE_D3D12_AGILITY
#if !defined(DX12_RENDERER_AGILITY_SDK_VERSION)
#error "DX12_RENDERER_AGILITY_SDK_VERSION must match the distributed Agility SDK."
#endif
static_assert(D3D12_SDK_VERSION == DX12_RENDERER_AGILITY_SDK_VERSION,
    "The Agility SDK headers and distributed runtime must use the same SDK version.");
extern "C"
{
    __declspec(dllexport) extern const UINT D3D12SDKVersion = DX12_RENDERER_AGILITY_SDK_VERSION;
    __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\";
}
#endif
//Modify End

// A wrapper struct to allow shared pointers for the window class.
// This is needed because the constructor and destructor for the Window
// class are protected and not accessible by the std::make_shared method.
struct MakeWindow : public Window
{
//Modify Begin:2026-08-12 by BestHui
    MakeWindow(
        HWND hWnd,
        const std::wstring& windowName,
        int clientWidth,
        int clientHeight,
        bool vSync,
        WindowD3D12Context d3d12Context)
        : Window(hWnd, windowName, clientWidth, clientHeight, vSync, std::move(d3d12Context))
    {}
//Modify End
};

//Modify Begin:2026-07-21 by BestHui
Application::Application(HINSTANCE hInst)
    : Application(hInst, nullptr)
{
}

Application::Application(HINSTANCE hInst, const ExternalD3D12Context* externalContext)
//Modify End
    : m_hInstance(hInst)
    , m_TearingSupported(false)
{
    // Windows 10 Creators update adds Per Monitor V2 DPI awareness context.
    // Using this awareness context allows the client area of the window
    // to achieve 100% scaling while still allowing non-client window content to
    // be rendered in a DPI sensitive fashion.
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    WNDCLASSEXW wndClass = { 0 };

    wndClass.cbSize = sizeof(WNDCLASSEX);
//Modify Begin:2026-07-30 by BestHui
    wndClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
//Modify End
    wndClass.lpfnWndProc = &WndProc;
    wndClass.hInstance = m_hInstance;
    wndClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wndClass.hIcon = LoadIcon(m_hInstance, MAKEINTRESOURCE(APP_ICON));
    wndClass.hbrBackground = (HBRUSH) (COLOR_WINDOW + 1);
    wndClass.lpszMenuName = nullptr;
    wndClass.lpszClassName = WINDOW_CLASS_NAME;
    wndClass.hIconSm = LoadIcon(m_hInstance, MAKEINTRESOURCE(APP_ICON));

    if (!RegisterClassExW(&wndClass))
    {
        MessageBoxA(nullptr, "Unable to register the window class.", "Error", MB_OK | MB_ICONERROR);
    }
}

//Modify Begin:2026-07-21 by BestHui
void Application::Initialize(
    const ExternalD3D12Context* externalContext,
    const ApplicationCreateDesc& createDesc)
//Modify End
{
//Modify Begin:2026-07-30 by BestHui
    m_DiagnosticReporter = std::make_unique<DiagnosticReporter>(createDesc.DiagnosticsDirectory);
//Modify End
//Modify Begin:2026-07-21 by BestHui
    const bool useExternalDevice = externalContext != nullptr && externalContext->Device != nullptr;
//Modify End
//Modify Begin:2026-07-28 by BestHui
    char* enableDred = nullptr;
    size_t enableDredLength = 0;
    _dupenv_s(&enableDred, &enableDredLength, "DX12_RENDERER_ENABLE_DRED");
    const bool forceDred = enableDred != nullptr && std::strcmp(enableDred, "0") != 0;
    std::free(enableDred);
    if (forceDred)
    {
        ComPtr<ID3D12Debug1> debugInterface;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugInterface))))
        {
            debugInterface->EnableDebugLayer();
        }

        ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> dredSettings;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dredSettings))))
        {
            dredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            dredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            dredSettings->SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        }
    }
//Modify End
#if defined(_DEBUG)
//Modify Begin:2026-07-21 by BestHui
    if (!useExternalDevice)
    {
//Modify End
    // Always enable the debug layer before doing anything DX12 related
    // so all possible errors generated while creating DX12 objects
    // are caught by the debug layer.
    ComPtr<ID3D12Debug1> debugInterface;
    ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugInterface)));
    debugInterface->EnableDebugLayer();
    // Enable these if you want full validation (will slow down rendering a lot).
    //debugInterface->SetEnableGPUBasedValidation(TRUE);
    //debugInterface->SetEnableSynchronizedCommandQueueValidation(TRUE);
//Modify Begin:2026-07-21 by BestHui
    }
//Modify End
#endif

//Modify Begin:2026-07-21 by BestHui
    D3D12RenderContextInitializationDesc renderContextDesc;
    renderContextDesc.EnableStreamlineInterposer = createDesc.EnableStreamlineInterposer;
    if (useExternalDevice)
    {
        m_RenderContext.InitializeExternal(*externalContext, renderContextDesc);
    }
    else
    {
        auto dxgiAdapter = GetAdapter(false);
        if (!dxgiAdapter)
        {
            // If no supporting DX12 adapters exist, fall back to WARP
            dxgiAdapter = GetAdapter(true);
        }

        if (dxgiAdapter)
        {
            m_RenderContext.InitializeOwned(CreateDevice(dxgiAdapter), renderContextDesc);
        }
        else
        {
            throw std::exception("DXGI adapter enumeration failed.");
        }
    }
//Modify Begin:2026-08-07 by BestHui
    m_RenderContext.SetFatalErrorHandler(
        [this](const CommandQueueFailure& failure)
        {
            std::ostringstream report;
            report << "QueueType=" << static_cast<uint32_t>(failure.QueueType) << std::endl;
            report << "Stage=" << failure.Stage << std::endl;
            report << "Error=" << failure.Message << std::endl;
            WriteDiagnostic("CommandQueueException", report.str());
            Quit(failure.ExitCode);
        });
//Modify End
    const auto device = m_RenderContext.GetDevice();
    D3D12_FEATURE_DATA_SHADER_MODEL shaderModel = { D3D_SHADER_MODEL_6_8 };
    ThrowIfFailed(device->CheckFeatureSupport(
        D3D12_FEATURE_SHADER_MODEL,
        &shaderModel,
        sizeof(shaderModel)));
    if (shaderModel.HighestShaderModel < D3D_SHADER_MODEL_6_8)
    {
        throw std::runtime_error("DX12 renderer requires Shader Model 6.8 support from the active D3D12 driver.");
    }
//Modify End

    m_TearingSupported = CheckTearingSupport();

    // Initialize frame counter
    s_FrameCount = 0;

    srand(static_cast<unsigned>(time(nullptr)));
}

void Application::Create(HINSTANCE hInst)
{
    ApplicationCreateDesc createDesc;
    Create(hInst, createDesc);
}

//Modify Begin:2026-08-07 by BestHui
void Application::Create(HINSTANCE hInst, const ApplicationCreateDesc& createDesc)
{
    if (!gs_pSingelton)
    {
        gs_pSingelton = new Application(hInst);
        gs_pSingelton->Initialize(nullptr, createDesc);
    }
}
//Modify End

//Modify Begin:2026-07-21 by BestHui
void Application::Create(HINSTANCE hInst, const ExternalD3D12Context& externalContext)
{
    ApplicationCreateDesc createDesc;
    Create(hInst, externalContext, createDesc);
}
//Modify End

//Modify Begin:2026-08-07 by BestHui
void Application::Create(
    HINSTANCE hInst,
    const ExternalD3D12Context& externalContext,
    const ApplicationCreateDesc& createDesc)
{
    Assert(externalContext.Device != nullptr, "External D3D12 device is required.");
    if (!gs_pSingelton)
    {
        gs_pSingelton = new Application(hInst, &externalContext);
        gs_pSingelton->Initialize(&externalContext, createDesc);
    }
}
//Modify End

Application& Application::Get()
{
    assert(gs_pSingelton);
    return *gs_pSingelton;
}

void Application::Destroy()
{
    if (gs_pSingelton)
    {
        assert(gs_Windows.empty() && gs_WindowByName.empty() &&
            "All windows should be destroyed before destroying the application instance.");

        delete gs_pSingelton;
        gs_pSingelton = nullptr;
    }
}

Application::~Application()
{
    Flush();
}

Microsoft::WRL::ComPtr<IDXGIAdapter4> Application::GetAdapter(bool bUseWarp)
{
    ComPtr<IDXGIFactory4> dxgiFactory;
    UINT createFactoryFlags = 0;
#if defined(_DEBUG)
    createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

    ThrowIfFailed(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory)));

    ComPtr<IDXGIAdapter1> dxgiAdapter1;
    ComPtr<IDXGIAdapter4> dxgiAdapter4;

    if (bUseWarp)
    {
        ThrowIfFailed(dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&dxgiAdapter1)));
        ThrowIfFailed(dxgiAdapter1.As(&dxgiAdapter4));
    }
    else
    {
        SIZE_T maxDedicatedVideoMemory = 0;
        for (UINT i = 0; dxgiFactory->EnumAdapters1(i, &dxgiAdapter1) != DXGI_ERROR_NOT_FOUND; ++i)
        {
            DXGI_ADAPTER_DESC1 dxgiAdapterDesc1;
            dxgiAdapter1->GetDesc1(&dxgiAdapterDesc1);

            // Check to see if the adapter can create a D3D12 device without actually
            // creating it. The adapter with the largest dedicated video memory
            // is favored.
            if ((dxgiAdapterDesc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
                SUCCEEDED(D3D12CreateDevice(dxgiAdapter1.Get(),
                    D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)) &&
                dxgiAdapterDesc1.DedicatedVideoMemory > maxDedicatedVideoMemory)
            {
                maxDedicatedVideoMemory = dxgiAdapterDesc1.DedicatedVideoMemory;
                ThrowIfFailed(dxgiAdapter1.As(&dxgiAdapter4));
            }
        }
    }

    return dxgiAdapter4;
}

Microsoft::WRL::ComPtr<ID3D12Device2> Application::CreateDevice(Microsoft::WRL::ComPtr<IDXGIAdapter4> adapter)
{
    ComPtr<ID3D12Device2> d3d12Device2;
    ThrowIfFailed(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d3d12Device2)));
    //    NAME_D3D12_OBJECT(d3d12Device2);

    // Enable debug messages in debug mode.
#if defined(_DEBUG)
    ComPtr<ID3D12InfoQueue> pInfoQueue;
    if (SUCCEEDED(d3d12Device2.As(&pInfoQueue)))
    {
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);
    }
#endif

    return d3d12Device2;
}

bool Application::CheckTearingSupport()
{
    BOOL allowTearing = FALSE;

    // Rather than create the DXGI 1.5 factory interface directly, we create the
    // DXGI 1.4 interface and query for the 1.5 interface. This is to enable the
    // graphics debugging tools which will not support the 1.5 factory interface
    // until a future update.
    ComPtr<IDXGIFactory4> factory4;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory4))))
    {
        ComPtr<IDXGIFactory5> factory5;
        if (SUCCEEDED(factory4.As(&factory5)))
        {
            factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                &allowTearing, sizeof(allowTearing));
        }
    }

    return allowTearing == TRUE;
}

//Modify Begin:2026-07-30 by BestHui
void Application::WriteDiagnostic(
    const std::string_view reportName,
    const std::string_view contents) const noexcept
{
    if (m_DiagnosticReporter != nullptr)
    {
        m_DiagnosticReporter->Write(reportName, contents);
    }
}
//Modify End

void Application::AddWndProcHandler(const WndProcHandler handler)
{
    s_WndProcHandlers.push_back(handler);
}

void Application::RemoveWndProcHandler(const WndProcHandler handler)
{
    std::erase(s_WndProcHandlers, handler);
}

void Application::AddKeyDownListener(const KeyDownListener listener)
{
    s_KeyDownListeners.push_back(listener);
}

auto Application::RemoveKeyDownListener(KeyDownListener listener) -> void
{
    std::erase(s_KeyDownListeners, listener);
}

bool Application::IsTearingSupported() const
{
    return m_TearingSupported;
}

DXGI_SAMPLE_DESC Application::GetMultisampleQualityLevels(DXGI_FORMAT format, UINT numSamples,
    D3D12_MULTISAMPLE_QUALITY_LEVEL_FLAGS flags) const
{
    DXGI_SAMPLE_DESC sampleDesc = { 1, 0 };

    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS qualityLevels;
    qualityLevels.Format = format;
    qualityLevels.SampleCount = 1;
    qualityLevels.Flags = flags;
    qualityLevels.NumQualityLevels = 0;

//Modify Begin:2026-07-28 by BestHui
    const auto device = m_RenderContext.GetDevice();
    while (qualityLevels.SampleCount <= numSamples && SUCCEEDED(
        device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &qualityLevels, sizeof(
            D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS))) && qualityLevels.NumQualityLevels > 0)
//Modify End
    {
        // That works...
        sampleDesc.Count = qualityLevels.SampleCount;
        sampleDesc.Quality = qualityLevels.NumQualityLevels - 1;

        // But can we do better?
        qualityLevels.SampleCount *= 2;
    }

    return sampleDesc;
}


std::shared_ptr<Window> Application::CreateRenderWindow(const std::wstring& windowName, int clientWidth,
    int clientHeight, bool vSync)
{
    // First check if a window with the given name already exists.
    auto windowIter = gs_WindowByName.find(windowName);
    if (windowIter != gs_WindowByName.end())
    {
        return windowIter->second;
    }

    RECT windowRect = { 0, 0, clientWidth, clientHeight };
    AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hWnd = CreateWindowW(WINDOW_CLASS_NAME, windowName.c_str(),
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr, nullptr, m_hInstance, nullptr);

    if (!hWnd)
    {
        MessageBoxA(nullptr, "Could not create the render window.", "Error", MB_OK | MB_ICONERROR);
        return nullptr;
    }

//Modify Begin:2026-08-12 by BestHui
    WindowD3D12Context windowD3D12Context;
    windowD3D12Context.DeviceContext = m_RenderContext.GetD3D12DeviceContext();
    windowD3D12Context.DirectCommandQueue = m_RenderContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
    windowD3D12Context.ComputeCommandQueue = m_RenderContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE);
    windowD3D12Context.CopyCommandQueue = m_RenderContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COPY);
    windowD3D12Context.StreamlineRuntime = m_RenderContext.GetStreamlineRuntime();
    windowD3D12Context.IsTearingSupported = m_TearingSupported;
    WindowPtr pWindow = std::make_shared<MakeWindow>(
        hWnd,
        windowName,
        clientWidth,
        clientHeight,
        vSync,
        std::move(windowD3D12Context));
//Modify End
    pWindow->Initialize();

    gs_Windows.insert(WindowMap::value_type(hWnd, pWindow));
    gs_WindowByName.insert(WindowNameMap::value_type(windowName, pWindow));

    return pWindow;
}

void Application::DestroyWindow(std::shared_ptr<Window> window)
{
    if (window) window->Destroy();
}

void Application::DestroyWindow(const std::wstring& windowName)
{
    WindowPtr pWindow = GetWindowByName(windowName);
    if (pWindow)
    {
        DestroyWindow(pWindow);
    }
}

std::shared_ptr<Window> Application::GetWindowByName(const std::wstring& windowName)
{
    std::shared_ptr<Window> window;
    auto iter = gs_WindowByName.find(windowName);
    if (iter != gs_WindowByName.end())
    {
        window = iter->second;
    }

    return window;
}


int Application::Run(std::shared_ptr<Game> pGame)
{
//Modify Begin:2026-07-30 by BestHui
    MSG msg = { nullptr };
    PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE);
    m_MessageThreadId.store(GetCurrentThreadId(), std::memory_order_release);
//Modify End
    if (!pGame->Initialize())
    {
        m_MessageThreadId.store(0, std::memory_order_release);
        return 1;
    }
    if (!pGame->LoadContent())
    {
        m_MessageThreadId.store(0, std::memory_order_release);
        return 2;
    }

    while (msg.message != WM_QUIT)
    {
//Modify Begin:2026-07-30 by BestHui
        if (m_QuitRequested.exchange(false, std::memory_order_acq_rel))
        {
            msg.message = WM_QUIT;
            msg.wParam = static_cast<WPARAM>(m_RequestedExitCode.load(std::memory_order_acquire));
            continue;
        }
//Modify End
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    // Flush any commands in the commands queues before quiting.
    Flush();

    pGame->UnloadContent();
    pGame->Destroy();

    m_MessageThreadId.store(0, std::memory_order_release);
    m_QuitRequested.store(false, std::memory_order_release);
    return static_cast<int>(msg.wParam);
}

void Application::Quit(int exitCode)
{
//Modify Begin:2026-07-30 by BestHui
    m_RequestedExitCode.store(exitCode, std::memory_order_release);
    m_QuitRequested.store(true, std::memory_order_release);
    const DWORD messageThreadId = m_MessageThreadId.load(std::memory_order_acquire);
    if (messageThreadId == 0)
    {
        return;
    }

    if (messageThreadId == GetCurrentThreadId())
    {
        PostQuitMessage(exitCode);
        return;
    }

    PostThreadMessageW(messageThreadId, WM_QUIT, static_cast<WPARAM>(exitCode), 0);
//Modify End
}

Microsoft::WRL::ComPtr<ID3D12Device2> Application::GetDevice() const
{
//Modify Begin:2026-07-28 by BestHui
    return m_RenderContext.GetDevice();
//Modify End
}

//Modify Begin:2026-08-07 by BestHui
std::shared_ptr<D3D12DeviceContext> Application::GetD3D12DeviceContext() const
{
//Modify Begin:2026-08-07 by BestHui
    return m_RenderContext.GetD3D12DeviceContext();
//Modify End
}
//Modify End

std::shared_ptr<StreamlineRuntime> Application::GetStreamlineRuntime() const
{
    return m_RenderContext.GetStreamlineRuntime();
}

//Modify Begin:2026-07-30 by BestHui
std::shared_ptr<ResourceStateRegistry> Application::GetResourceStateRegistry() const
{
    return m_RenderContext.GetResourceStateRegistry();
}
//Modify End

//Modify Begin:2026-08-07 by BestHui
std::shared_ptr<FrameFeaturesRuntime> Application::GetFrameFeaturesRuntime() const
{
    return m_RenderContext.GetStreamlineRuntime();
}
//Modify End

bool Application::SetFrameGenerationEnabled(const bool enabled)
{
    const std::shared_ptr<StreamlineRuntime> streamlineRuntime = GetStreamlineRuntime();
    if (streamlineRuntime == nullptr)
    {
        return false;
    }
    if (streamlineRuntime->IsFrameGenerationEnabled() == enabled)
    {
        return true;
    }

    Flush();
    for (const auto& [windowHandle, window] : gs_Windows)
    {
        (void)windowHandle;
        window->ReleaseSwapChainResources();
    }
    if (!streamlineRuntime->SetFrameGenerationEnabled(enabled))
    {
        for (const auto& [windowHandle, window] : gs_Windows)
        {
            (void)windowHandle;
            window->RecreateSwapChain();
        }
        return false;
    }
    for (const auto& [windowHandle, window] : gs_Windows)
    {
        (void)windowHandle;
        window->RecreateSwapChain();
    }
    return true;
}

//Modify Begin:2026-07-21 by BestHui
bool Application::UsesExternalDevice() const
{
//Modify Begin:2026-07-28 by BestHui
    return m_RenderContext.UsesExternalDevice();
//Modify End
}
//Modify End

std::shared_ptr<CommandQueue> Application::GetCommandQueue(D3D12_COMMAND_LIST_TYPE type) const
{
//Modify Begin:2026-07-28 by BestHui
    return m_RenderContext.GetCommandQueue(type);
//Modify End
}

void Application::Flush()
{
//Modify Begin:2026-07-28 by BestHui
    m_RenderContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT)->Flush();
    m_RenderContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE)->Flush();
    m_RenderContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COPY)->Flush();
//Modify End
}

DescriptorAllocation Application::AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors)
{
    return m_RenderContext.GetD3D12DeviceContext()->AllocateDescriptors(type, numDescriptors);
}

void Application::ReleaseStaleDescriptors(uint64_t finishedFrame)
{
    m_RenderContext.GetD3D12DeviceContext()->ReleaseStaleDescriptors(finishedFrame);
}

Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> Application::CreateDescriptorHeap(
    UINT numDescriptors, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = type;
    desc.NumDescriptors = numDescriptors;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    desc.NodeMask = 0;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap;
//Modify Begin:2026-07-28 by BestHui
    ThrowIfFailed(m_RenderContext.GetDevice()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptorHeap)));
//Modify End

    return descriptorHeap;
}

UINT Application::GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE type) const
{
//Modify Begin:2026-07-28 by BestHui
    return m_RenderContext.GetDevice()->GetDescriptorHandleIncrementSize(type);
//Modify End
}


// Remove a window from our window lists.
static void RemoveWindow(HWND hWnd)
{
    auto windowIter = gs_Windows.find(hWnd);
    if (windowIter != gs_Windows.end())
    {
        WindowPtr pWindow = windowIter->second;
        gs_WindowByName.erase(pWindow->GetWindowName());
        gs_Windows.erase(windowIter);
    }
}

// Convert the message ID into a MouseButton ID
MouseButtonEventArgs::MouseButton DecodeMouseButton(UINT messageID)
{
    MouseButtonEventArgs::MouseButton mouseButton = MouseButtonEventArgs::None;
    switch (messageID)
    {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
        {
            mouseButton = MouseButtonEventArgs::Left;
        }
        break;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
        {
            mouseButton = MouseButtonEventArgs::Right;
        }
        break;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
        {
            mouseButton = MouseButtonEventArgs::Middle;
        }
        break;
    }

    return mouseButton;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
//Modify Begin:2026-07-28 by BestHui
    try
    {
//Modify End
    for (const auto& handler : Application::s_WndProcHandlers)
    {
        if (handler(hwnd, message, wParam, lParam))
        {
            return true;
        }
    }

    WindowPtr pWindow;
    {
        auto iter = gs_Windows.find(hwnd);
        if (iter != gs_Windows.end())
        {
            pWindow = iter->second;
        }
    }

    if (pWindow)
    {
        switch (message)
        {
        case WM_PAINT:
            {
                ++Application::s_FrameCount;
                pWindow->BeginFrame(Application::s_FrameCount);

                // Delta time will be filled in by the Window.
                UpdateEventArgs updateEventArgs(0.0f, 0.0f, Application::s_FrameCount);
                pWindow->OnUpdate(updateEventArgs);
                RenderEventArgs renderEventArgs(0.0f, 0.0f, Application::s_FrameCount);
                // Delta time will be filled in by the Window.
                pWindow->OnRender(renderEventArgs);
            }
            break;
        case WM_SYSKEYDOWN:
        case WM_KEYDOWN:
            {
                MSG charMsg;
                // Get the Unicode character (UTF-16)
                unsigned int c = 0;
                // For printable characters, the next message will be WM_CHAR.
                // This message contains the character code we need to send the KeyPressed event.
                // Inspired by the SDL 1.2 implementation.
                if (PeekMessage(&charMsg, hwnd, 0, 0, PM_NOREMOVE) && charMsg.message == WM_CHAR)
                {
                    GetMessage(&charMsg, hwnd, 0, 0);
                    c = static_cast<unsigned int>(charMsg.wParam);

                    if (charMsg.wParam > 0 && charMsg.wParam < 0x10000)
                    {
                        for (const auto listener : Application::s_KeyDownListeners)
                        {
                            listener(c);
                        }
                    }

                }
                bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                bool control = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
                bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
                auto key = static_cast<KeyCode::Key>(wParam);
                unsigned int scanCode = (lParam & 0x00FF0000) >> 16;
                KeyEventArgs keyEventArgs(key, c, KeyEventArgs::Pressed, shift, control, alt);
                pWindow->OnKeyPressed(keyEventArgs);
            }
            break;
        case WM_SYSKEYUP:
        case WM_KEYUP:
            {
                bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                bool control = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
                bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
                auto key = static_cast<KeyCode::Key>(wParam);
                unsigned int c = 0;
                unsigned int scanCode = (lParam & 0x00FF0000) >> 16;

                // Determine which key was released by converting the key code and the scan code
                // to a printable character (if possible).
                // Inspired by the SDL 1.2 implementation.
                unsigned char keyboardState[256];
                GetKeyboardState(keyboardState);
                wchar_t translatedCharacters[4];
                if (int result = ToUnicodeEx(static_cast<UINT>(wParam), scanCode, keyboardState, translatedCharacters,
                    4, 0, nullptr) > 0)
                {
                    c = translatedCharacters[0];
                }

                KeyEventArgs keyEventArgs(key, c, KeyEventArgs::Released, shift, control, alt);
                pWindow->OnKeyReleased(keyEventArgs);
            }
            break;
        // The default window procedure will play a system notification sound
        // when pressing the Alt+Enter keyboard combination if this message is
        // not handled.
        case WM_SYSCHAR:
            break;
        case WM_MOUSEMOVE:
            {
                bool lButton = (wParam & MK_LBUTTON) != 0;
                bool rButton = (wParam & MK_RBUTTON) != 0;
                bool mButton = (wParam & MK_MBUTTON) != 0;
                bool shift = (wParam & MK_SHIFT) != 0;
                bool control = (wParam & MK_CONTROL) != 0;

                int x = static_cast<short>(LOWORD(lParam));
                int y = static_cast<short>(HIWORD(lParam));

                MouseMotionEventArgs mouseMotionEventArgs(lButton, mButton, rButton, control, shift, x, y);
                pWindow->OnMouseMoved(mouseMotionEventArgs);
            }
            break;
        case WM_LBUTTONDOWN:
//Modify Begin:2026-07-30 by BestHui
        case WM_LBUTTONDBLCLK:
//Modify End
        case WM_RBUTTONDOWN:
//Modify Begin:2026-07-30 by BestHui
        case WM_RBUTTONDBLCLK:
//Modify End
        case WM_MBUTTONDOWN:
//Modify Begin:2026-07-30 by BestHui
        case WM_MBUTTONDBLCLK:
//Modify End
            {
                bool lButton = (wParam & MK_LBUTTON) != 0;
                bool rButton = (wParam & MK_RBUTTON) != 0;
                bool mButton = (wParam & MK_MBUTTON) != 0;
                bool shift = (wParam & MK_SHIFT) != 0;
                bool control = (wParam & MK_CONTROL) != 0;

                int x = static_cast<short>(LOWORD(lParam));
                int y = static_cast<short>(HIWORD(lParam));

//Modify Begin:2026-08-06 by BestHui
                const bool doubleClick =
                    message == WM_LBUTTONDBLCLK ||
                    message == WM_RBUTTONDBLCLK ||
                    message == WM_MBUTTONDBLCLK;
                MouseButtonEventArgs mouseButtonEventArgs(DecodeMouseButton(message), MouseButtonEventArgs::Pressed,
                    lButton, mButton, rButton, control, shift, x, y, doubleClick);
//Modify End
                pWindow->OnMouseButtonPressed(mouseButtonEventArgs);
            }
            break;
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
            {
                bool lButton = (wParam & MK_LBUTTON) != 0;
                bool rButton = (wParam & MK_RBUTTON) != 0;
                bool mButton = (wParam & MK_MBUTTON) != 0;
                bool shift = (wParam & MK_SHIFT) != 0;
                bool control = (wParam & MK_CONTROL) != 0;

                int x = static_cast<short>(LOWORD(lParam));
                int y = static_cast<short>(HIWORD(lParam));

                MouseButtonEventArgs mouseButtonEventArgs(DecodeMouseButton(message), MouseButtonEventArgs::Released,
                    lButton, mButton, rButton, control, shift, x, y);
                pWindow->OnMouseButtonReleased(mouseButtonEventArgs);
            }
            break;
        case WM_MOUSEWHEEL:
            {
                // The distance the mouse wheel is rotated.
                // A positive value indicates the wheel was rotated to the right.
                // A negative value indicates the wheel was rotated to the left.
                float zDelta = static_cast<int>(static_cast<short>(HIWORD(wParam))) / static_cast<float>(WHEEL_DELTA);
                short keyStates = static_cast<short>(LOWORD(wParam));

                bool lButton = (keyStates & MK_LBUTTON) != 0;
                bool rButton = (keyStates & MK_RBUTTON) != 0;
                bool mButton = (keyStates & MK_MBUTTON) != 0;
                bool shift = (keyStates & MK_SHIFT) != 0;
                bool control = (keyStates & MK_CONTROL) != 0;

                int x = static_cast<short>(LOWORD(lParam));
                int y = static_cast<short>(HIWORD(lParam));

                // Convert the screen coordinates to client coordinates.
                POINT clientToScreenPoint;
                clientToScreenPoint.x = x;
                clientToScreenPoint.y = y;
                ScreenToClient(hwnd, &clientToScreenPoint);

                MouseWheelEventArgs mouseWheelEventArgs(zDelta, lButton, mButton, rButton, control, shift,
                    clientToScreenPoint.x, clientToScreenPoint.y);
                pWindow->OnMouseWheel(mouseWheelEventArgs);
            }
            break;
        case WM_SIZE:
            {
                int width = static_cast<short>(LOWORD(lParam));
                int height = static_cast<short>(HIWORD(lParam));

                ResizeEventArgs resizeEventArgs(width, height);
                pWindow->OnResize(resizeEventArgs);
            }
            break;
        case WM_DESTROY:
            {
                // If a window is being destroyed, remove it from the
                // window maps.
                RemoveWindow(hwnd);

                if (gs_Windows.empty())
                {
                    // If there are no more windows, quit the application.
                    PostQuitMessage(0);
                }
            }
            break;
        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }
    }
    else
    {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    return 0;
//Modify Begin:2026-07-28 by BestHui
    }
    catch (const std::exception& exception)
    {
        std::ostringstream report;
        report << "Message=" << message << std::endl;
        report << exception.what() << std::endl;
//Modify Begin:2026-08-03 by BestHui
        const auto device = gs_pSingelton != nullptr ? gs_pSingelton->GetDevice() : nullptr;
        if (device != nullptr)
        {
            report << "DeviceRemovedReason=" << device->GetDeviceRemovedReason() << std::endl;

            Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;
            if (SUCCEEDED(device.As(&infoQueue)))
            {
                const UINT64 messageCount = infoQueue->GetNumStoredMessages();
                const UINT64 firstMessage = messageCount > 32u ? messageCount - 32u : 0u;
                for (UINT64 messageIndex = firstMessage; messageIndex < messageCount; ++messageIndex)
                {
                    SIZE_T messageLength = 0;
                    if (FAILED(infoQueue->GetMessage(messageIndex, nullptr, &messageLength)) || messageLength == 0u)
                    {
                        continue;
                    }

                    std::vector<char> storage(messageLength);
                    auto* debugMessage = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
                    if (SUCCEEDED(infoQueue->GetMessage(messageIndex, debugMessage, &messageLength)))
                    {
                        report << "D3D12[" << messageIndex << "]="
                            << (debugMessage->pDescription != nullptr ? debugMessage->pDescription : "")
                            << std::endl;
                    }
                }
            }
        }
//Modify End
        if (gs_pSingelton != nullptr)
        {
            gs_pSingelton->WriteDiagnostic("WindowCallbackException", report.str());
        }
        PostQuitMessage(4);
        return 0;
    }
//Modify Begin:2026-08-07 by BestHui
    catch (...)
    {
        std::ostringstream report;
        report << "Message=" << message << std::endl;
        report << "Unhandled non-std exception escaped the window callback." << std::endl;
        if (gs_pSingelton != nullptr)
        {
            gs_pSingelton->WriteDiagnostic("WindowCallbackException", report.str());
        }
        PostQuitMessage(4);
        return 0;
    }
//Modify End
//Modify End
}
