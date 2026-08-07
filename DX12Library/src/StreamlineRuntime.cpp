//Modify Begin:2026-08-07 by BestHui
#include "DX12LibPCH.h"

#include "StreamlineRuntime.h"

#include <sl.h>
#include <sl_core_api.h>
#include <sl_dlss.h>
#include <sl_dlss_d.h>
#include <sl_dlss_g.h>
#include <sl_reflex.h>

#include <array>
#include <filesystem>
#include <sstream>

namespace
{
    void StreamlineLogMessage(const sl::LogType type, const char* message)
    {
        if (type == sl::LogType::eError && message != nullptr)
        {
            OutputDebugStringA(message);
            OutputDebugStringA("\n");
        }
    }

    std::string GetResultMessage(const char* operation, const sl::Result result)
    {
        std::ostringstream message;
        message << operation << " failed (Streamline result " << static_cast<uint32_t>(result) << ").";
        return message.str();
    }
}

StreamlineRuntime::~StreamlineRuntime()
{
    Shutdown();
}

bool StreamlineRuntime::Initialize(ID3D12Device2* nativeDevice, const std::wstring& logDirectory)
{
    if (m_Initialized)
    {
        return true;
    }
    if (nativeDevice == nullptr)
    {
        m_StatusMessage = "Streamline requires a native D3D12 device.";
        return false;
    }

    const std::array<sl::Feature, 4> features = {
        sl::kFeatureDLSS,
        sl::kFeatureDLSS_RR,
        sl::kFeatureDLSS_G,
        sl::kFeatureReflex,
    };

    std::filesystem::create_directories(logDirectory);
    sl::Preferences preferences{};
    preferences.applicationId = 231313132u;
    preferences.renderAPI = sl::RenderAPI::eD3D12;
    preferences.featuresToLoad = features.data();
    preferences.numFeaturesToLoad = static_cast<uint32_t>(features.size());
    preferences.pathToLogsAndData = logDirectory.c_str();
    preferences.logMessageCallback = &StreamlineLogMessage;
    preferences.flags = sl::PreferenceFlags::eUseManualHooking |
        sl::PreferenceFlags::eUseFrameBasedResourceTagging |
        sl::PreferenceFlags::eUseDXGIFactoryProxy;

    const sl::Result initializeResult = slInit(preferences);
    if (initializeResult != sl::Result::eOk)
    {
        m_StatusMessage = GetResultMessage("slInit", initializeResult);
        return false;
    }

    m_Initialized = true;
    if (!UpgradeDevice(nativeDevice))
    {
        Shutdown();
        return false;
    }

    LUID adapterLuid = nativeDevice->GetAdapterLuid();
    sl::AdapterInfo adapterInfo{};
    adapterInfo.deviceLUID = reinterpret_cast<uint8_t*>(&adapterLuid);
    adapterInfo.deviceLUIDSizeInBytes = sizeof(adapterLuid);
    m_RayReconstructionSupported = slIsFeatureSupported(sl::kFeatureDLSS_RR, adapterInfo) == sl::Result::eOk;
    m_FrameGenerationSupported = slIsFeatureSupported(sl::kFeatureDLSS_G, adapterInfo) == sl::Result::eOk;

    if (m_FrameGenerationSupported)
    {
        const sl::Result disableFrameGenerationResult = slSetFeatureLoaded(sl::kFeatureDLSS_G, false);
        if (disableFrameGenerationResult != sl::Result::eOk)
        {
            m_StatusMessage = GetResultMessage("slSetFeatureLoaded(DLSS_G off)", disableFrameGenerationResult);
            Shutdown();
            return false;
        }
    }

    m_StatusMessage = "Streamline initialized with manual D3D12 hooks.";
    return true;
}

void StreamlineRuntime::Shutdown()
{
    if (!m_Initialized)
    {
        return;
    }

    m_FrameGenerationEnabled = false;
    m_RayReconstructionSupported = false;
    m_FrameGenerationSupported = false;
    m_ProxyDevice = nullptr;
    const sl::Result shutdownResult = slShutdown();
    if (shutdownResult != sl::Result::eOk)
    {
        m_StatusMessage = GetResultMessage("slShutdown", shutdownResult);
    }
    m_Initialized = false;
}

bool StreamlineRuntime::SetFrameGenerationEnabled(const bool enabled)
{
    if (!m_Initialized)
    {
        m_StatusMessage = "Frame Generation requires an initialized Streamline runtime.";
        return false;
    }
    if (enabled && !m_FrameGenerationSupported)
    {
        m_StatusMessage = "DLSS Frame Generation is not supported by the active adapter, driver, or operating-system configuration.";
        return false;
    }
    if (m_FrameGenerationEnabled == enabled)
    {
        return true;
    }

    const sl::Result result = slSetFeatureLoaded(sl::kFeatureDLSS_G, enabled);
    if (result != sl::Result::eOk)
    {
        m_StatusMessage = GetResultMessage("slSetFeatureLoaded(DLSS_G)", result);
        return false;
    }

    m_FrameGenerationEnabled = enabled;
    return true;
}

HRESULT StreamlineRuntime::CreateCommandQueue(
    ID3D12Device2* nativeDevice,
    const D3D12_COMMAND_QUEUE_DESC* desc,
    REFIID riid,
    void** commandQueue) const
{
    if (!m_Initialized || m_ProxyDevice == nullptr)
    {
        return nativeDevice->CreateCommandQueue(desc, riid, commandQueue);
    }
    return m_ProxyDevice->CreateCommandQueue(desc, riid, commandQueue);
}

HRESULT StreamlineRuntime::CreateSwapChainForHwnd(
    IDXGIFactory4* nativeFactory,
    IUnknown* commandQueue,
    const HWND window,
    const DXGI_SWAP_CHAIN_DESC1* desc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreenDesc,
    IDXGIOutput* restrictToOutput,
    IDXGISwapChain1** swapChain) const
{
    if (!m_Initialized)
    {
        return nativeFactory->CreateSwapChainForHwnd(
            commandQueue,
            window,
            desc,
            fullscreenDesc,
            restrictToOutput,
            swapChain);
    }

    IDXGIFactory4* proxyFactory = nativeFactory;
    const sl::Result upgradeResult = slUpgradeInterface(reinterpret_cast<void**>(&proxyFactory));
    if (upgradeResult != sl::Result::eOk)
    {
        return E_FAIL;
    }
    return proxyFactory->CreateSwapChainForHwnd(
        commandQueue,
        window,
        desc,
        fullscreenDesc,
        restrictToOutput,
        swapChain);
}

bool StreamlineRuntime::UpgradeDevice(ID3D12Device2* nativeDevice)
{
    m_ProxyDevice = nativeDevice;
    const sl::Result upgradeResult = slUpgradeInterface(reinterpret_cast<void**>(&m_ProxyDevice));
    if (upgradeResult != sl::Result::eOk)
    {
        m_StatusMessage = GetResultMessage("slUpgradeInterface(ID3D12Device)", upgradeResult);
        m_ProxyDevice = nullptr;
        return false;
    }
    return true;
}
//Modify End
