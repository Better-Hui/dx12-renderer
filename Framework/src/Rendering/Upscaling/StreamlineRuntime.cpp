#include <Framework/Rendering/Upscaling/StreamlineRuntime.h>

#include <DX12Library/PresentationController.h>

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

//Modify Begin:2026-08-18 by Hui
StreamlineRuntime::~StreamlineRuntime()
{
    Shutdown();
}

void StreamlineRuntime::SetPresentationController(PresentationController* presentationController) noexcept
{
    m_PresentationController = presentationController;
}

bool StreamlineRuntime::InitializeBeforeD3D12(const std::wstring& dataDirectory)
{
    if (m_RuntimeInitialized)
    {
        return true;
    }

    const std::array<sl::Feature, 4> features = {
        sl::kFeatureDLSS,
        sl::kFeatureDLSS_RR,
        sl::kFeatureDLSS_G,
        sl::kFeatureReflex,
    };

    const std::filesystem::path logDirectory =
        std::filesystem::path(dataDirectory) / L"Logs" / L"Streamline";
    std::filesystem::create_directories(logDirectory);

    sl::Preferences preferences{};
    preferences.applicationId = 231313132u;
    preferences.renderAPI = sl::RenderAPI::eD3D12;
    preferences.featuresToLoad = features.data();
    preferences.numFeaturesToLoad = static_cast<uint32_t>(features.size());
    preferences.pathToLogsAndData = logDirectory.c_str();
    preferences.logMessageCallback = &StreamlineLogMessage;
    preferences.flags = sl::PreferenceFlags::eUseFrameBasedResourceTagging |
        sl::PreferenceFlags::eUseDXGIFactoryProxy;

    const sl::Result initializeResult = slInit(preferences);
    if (initializeResult != sl::Result::eOk)
    {
        m_StatusMessage = GetResultMessage("slInit", initializeResult);
        return false;
    }

    m_RuntimeInitialized = true;
    m_StatusMessage = "Streamline initialized before D3D12 device creation.";
    return true;
}

bool StreamlineRuntime::AttachDevice(ID3D12Device2* device)
{
    if (!m_RuntimeInitialized)
    {
        m_StatusMessage = "Streamline must be initialized before attaching a D3D12 device.";
        return false;
    }
    if (device == nullptr)
    {
        m_StatusMessage = "Streamline requires a D3D12 device.";
        return false;
    }
    if (m_DeviceAttached)
    {
        return true;
    }

    const sl::Result setDeviceResult = slSetD3DDevice(device);
    if (setDeviceResult != sl::Result::eOk)
    {
        m_StatusMessage = GetResultMessage("slSetD3DDevice", setDeviceResult);
        return false;
    }

    LUID adapterLuid = device->GetAdapterLuid();
    sl::AdapterInfo adapterInfo{};
    adapterInfo.deviceLUID = reinterpret_cast<uint8_t*>(&adapterLuid);
    adapterInfo.deviceLUIDSizeInBytes = sizeof(adapterLuid);
    m_RayReconstructionSupported = slIsFeatureSupported(sl::kFeatureDLSS_RR, adapterInfo) == sl::Result::eOk;
    m_FrameGenerationSupported = slIsFeatureSupported(sl::kFeatureDLSS_G, adapterInfo) == sl::Result::eOk;
    m_DeviceAttached = true;

    if (m_FrameGenerationSupported && !ApplyFrameGenerationState(false))
    {
        m_DeviceAttached = false;
        return false;
    }

    m_StatusMessage = "Streamline initialized with automatic D3D12 interposition.";
    return true;
}

void StreamlineRuntime::Shutdown()
{
    if (!m_RuntimeInitialized)
    {
        return;
    }

    m_FrameGenerationEnabled = false;
    m_RayReconstructionSupported = false;
    m_FrameGenerationSupported = false;
    m_DeviceAttached = false;
    const sl::Result shutdownResult = slShutdown();
    if (shutdownResult != sl::Result::eOk)
    {
        m_StatusMessage = GetResultMessage("slShutdown", shutdownResult);
    }
    m_RuntimeInitialized = false;
}

bool StreamlineRuntime::SetFrameGenerationEnabled(const bool enabled)
{
    if (!IsInitialized())
    {
        m_StatusMessage = "Frame Generation requires an initialized Streamline runtime.";
        return false;
    }
    if (enabled && !m_FrameGenerationSupported)
    {
        m_StatusMessage =
            "DLSS Frame Generation is not supported by the active adapter, driver, or operating-system configuration.";
        return false;
    }
    if (m_FrameGenerationEnabled == enabled)
    {
        return true;
    }
    if (m_PresentationController == nullptr)
    {
        m_StatusMessage = "Frame Generation requires a presentation controller.";
        return false;
    }

    return m_PresentationController->ReconfigurePresentation(
        [this, enabled]
        {
            return ApplyFrameGenerationState(enabled);
        });
}

bool StreamlineRuntime::ApplyFrameGenerationState(const bool enabled)
{
    const sl::Result result = slSetFeatureLoaded(sl::kFeatureDLSS_G, enabled);
    if (result != sl::Result::eOk)
    {
        m_StatusMessage = GetResultMessage("slSetFeatureLoaded(DLSS_G)", result);
        return false;
    }

    m_FrameGenerationEnabled = enabled;
    return true;
}
//Modify End
