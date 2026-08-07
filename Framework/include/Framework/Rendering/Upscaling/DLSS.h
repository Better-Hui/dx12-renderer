//Modify Begin:2026-08-07 by BestHui
#pragma once

#include <DirectXMath.h>

#include <cstdint>
#include <memory>
#include <string>

class CommandList;
class FrameworkDeviceContext;
class Texture;

enum class DLSSMode : uint32_t
{
    Disabled = 0,
    DLAA,
    Quality,
    Balanced,
    Performance,
    UltraPerformance,
};

struct DLSSInitializationDesc
{
    uint64_t ApplicationId = 231313132ull;
    std::wstring ApplicationDataPath;
};

struct DLSSOptimalSettings
{
    uint32_t RenderWidth = 1;
    uint32_t RenderHeight = 1;
    float Sharpness = 0.0f;
};

struct DLSSExecutionInputs
{
    std::shared_ptr<Texture> Color;
    std::shared_ptr<Texture> Depth;
    std::shared_ptr<Texture> MotionVectors;
    std::shared_ptr<Texture> Output;
    uint32_t RenderWidth = 1;
    uint32_t RenderHeight = 1;
    uint32_t DisplayWidth = 1;
    uint32_t DisplayHeight = 1;
    DirectX::XMFLOAT2 JitterOffset = { 0.0f, 0.0f };
    float Sharpness = 0.0f;
    bool Reset = false;
};

class DLSS final
{
public:
    explicit DLSS(FrameworkDeviceContext& deviceContext, DLSSInitializationDesc initializationDesc = {});
    ~DLSS();

    DLSS(const DLSS&) = delete;
    DLSS& operator=(const DLSS&) = delete;

    [[nodiscard]] bool IsSupported() const { return m_Supported; }
    [[nodiscard]] bool IsEnabled() const { return m_Supported && m_Mode != DLSSMode::Disabled; }
    [[nodiscard]] DLSSMode GetMode() const { return m_Mode; }
    [[nodiscard]] const std::string& GetStatusMessage() const { return m_StatusMessage; }

    void SetMode(DLSSMode mode);
    [[nodiscard]] DLSSOptimalSettings GetOptimalSettings(uint32_t displayWidth, uint32_t displayHeight);
    [[nodiscard]] DirectX::XMFLOAT2 GetJitterOffset(uint64_t frameIndex) const;
    void InvalidateHistory();
    void OnResourcesRecreated();
    void Execute(CommandList& commandList, const DLSSExecutionInputs& inputs);

private:
    struct InternalState;

    [[nodiscard]] bool Initialize(const DLSSInitializationDesc& initializationDesc);
    [[nodiscard]] bool EnsureFeature(CommandList& commandList, const DLSSExecutionInputs& inputs);
    void ReleaseFeature();
    void Shutdown();

    FrameworkDeviceContext& m_DeviceContext;
    std::unique_ptr<InternalState> m_InternalState;
    DLSSMode m_Mode = DLSSMode::Disabled;
    bool m_Initialized = false;
    bool m_Supported = false;
    bool m_HistoryReset = true;
    std::string m_StatusMessage;
};
//Modify End
