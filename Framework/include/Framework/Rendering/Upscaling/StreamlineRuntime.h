#pragma once

#include <DX12Library/D3D12RuntimeLifecycle.h>
#include <Framework/Rendering/Upscaling/FrameFeaturesRuntime.h>

#include <string>
#include <string_view>

class PresentationController;

//Modify Begin:2026-08-18 by Hui
class StreamlineRuntime final :
    public D3D12RuntimeLifecycle,
    public FrameFeaturesRuntime,
    public FrameGenerationController
{
public:
    StreamlineRuntime() = default;
    ~StreamlineRuntime() override;

    StreamlineRuntime(const StreamlineRuntime&) = delete;
    StreamlineRuntime& operator=(const StreamlineRuntime&) = delete;

    void SetPresentationController(PresentationController* presentationController) noexcept;

    bool InitializeBeforeD3D12(const std::wstring& dataDirectory) override;
    bool AttachDevice(ID3D12Device2* device) override;
    void Shutdown() override;
    [[nodiscard]] std::string_view GetName() const override { return "Streamline"; }
    [[nodiscard]] std::string_view GetInitializationStatus() const override { return m_StatusMessage; }

    [[nodiscard]] bool IsInitialized() const override { return m_RuntimeInitialized && m_DeviceAttached; }
    [[nodiscard]] bool IsRayReconstructionSupported() const override { return m_RayReconstructionSupported; }
    [[nodiscard]] bool IsFrameGenerationSupported() const override { return m_FrameGenerationSupported; }
    [[nodiscard]] const std::string& GetStatusMessage() const override { return m_StatusMessage; }

    bool SetFrameGenerationEnabled(bool enabled) override;

private:
    bool ApplyFrameGenerationState(bool enabled);

    PresentationController* m_PresentationController = nullptr;
    bool m_RuntimeInitialized = false;
    bool m_DeviceAttached = false;
    bool m_RayReconstructionSupported = false;
    bool m_FrameGenerationSupported = false;
    bool m_FrameGenerationEnabled = false;
    std::string m_StatusMessage;
};
//Modify End
