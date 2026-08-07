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
    std::shared_ptr<Texture> DiffuseAlbedo;
    std::shared_ptr<Texture> SpecularAlbedo;
    std::shared_ptr<Texture> NormalRoughness;
    uint32_t RenderWidth = 1;
    uint32_t RenderHeight = 1;
    uint32_t DisplayWidth = 1;
    uint32_t DisplayHeight = 1;
    DirectX::XMFLOAT2 JitterOffset = { 0.0f, 0.0f };
    float Sharpness = 0.0f;
    bool Reset = false;
    uint32_t FrameIndex = 0;
    bool HasPreviousViewProjection = false;
    DirectX::XMMATRIX View = DirectX::XMMatrixIdentity();
    DirectX::XMMATRIX Projection = DirectX::XMMatrixIdentity();
    DirectX::XMMATRIX ViewProjection = DirectX::XMMatrixIdentity();
    DirectX::XMMATRIX PreviousViewProjection = DirectX::XMMatrixIdentity();
};

//Modify Begin:2026-08-07 by BestHui
struct DLSSFrameGenerationInputs
{
    std::shared_ptr<Texture> Depth;
    std::shared_ptr<Texture> MotionVectors;
    std::shared_ptr<Texture> HudLessColor;
    uint32_t RenderWidth = 1;
    uint32_t RenderHeight = 1;
    uint32_t DisplayWidth = 1;
    uint32_t DisplayHeight = 1;
    uint32_t FrameIndex = 0;
    bool HasPreviousViewProjection = false;
    DirectX::XMFLOAT2 JitterOffset = { 0.0f, 0.0f };
    DirectX::XMMATRIX View = DirectX::XMMatrixIdentity();
    DirectX::XMMATRIX Projection = DirectX::XMMatrixIdentity();
    DirectX::XMMATRIX ViewProjection = DirectX::XMMatrixIdentity();
    DirectX::XMMATRIX PreviousViewProjection = DirectX::XMMatrixIdentity();
};
//Modify End

class DLSS final
{
public:
    explicit DLSS(FrameworkDeviceContext& deviceContext, DLSSInitializationDesc initializationDesc = {});
    ~DLSS();

    DLSS(const DLSS&) = delete;
    DLSS& operator=(const DLSS&) = delete;

    [[nodiscard]] bool IsSupported() const { return m_Supported; }
    [[nodiscard]] bool IsEnabled() const { return m_Supported && m_Mode != DLSSMode::Disabled; }
    [[nodiscard]] bool IsRayReconstructionSupported() const;
    [[nodiscard]] bool IsFrameGenerationSupported() const;
    [[nodiscard]] bool IsRayReconstructionEnabled() const { return m_RayReconstructionEnabled; }
    [[nodiscard]] bool IsFrameGenerationEnabled() const { return m_FrameGenerationEnabled; }
    [[nodiscard]] DLSSMode GetMode() const { return m_Mode; }
    [[nodiscard]] const std::string& GetStatusMessage() const { return m_StatusMessage; }

    void SetMode(DLSSMode mode);
    void SetRayReconstructionEnabled(bool enabled);
    void SetFrameGenerationEnabled(bool enabled);
    [[nodiscard]] DLSSOptimalSettings GetOptimalSettings(uint32_t displayWidth, uint32_t displayHeight);
    [[nodiscard]] DirectX::XMFLOAT2 GetJitterOffset(uint64_t frameIndex) const;
    void InvalidateHistory();
    void OnResourcesRecreated();
    void Execute(CommandList& commandList, const DLSSExecutionInputs& inputs);
//Modify Begin:2026-08-07 by BestHui
    void BeginFrameGeneration(uint32_t frameIndex);
    void PrepareFrameGeneration(const DLSSFrameGenerationInputs& inputs);
    void TagFrameGenerationResources(CommandList& commandList, const DLSSFrameGenerationInputs& inputs);
    void MarkFrameGenerationRenderSubmissionEnd();
    void MarkFrameGenerationPresentStart();
    void MarkFrameGenerationPresentEnd();
//Modify End

private:
    struct InternalState;

    [[nodiscard]] bool Initialize(const DLSSInitializationDesc& initializationDesc);
    [[nodiscard]] bool EnsureFeature(CommandList& commandList, const DLSSExecutionInputs& inputs);
    void ExecuteRayReconstruction(CommandList& commandList, const DLSSExecutionInputs& inputs);
//Modify Begin:2026-08-07 by BestHui
    void* AcquireStreamlineFrameToken(uint32_t frameIndex);
    void ReleaseStreamlineFrameToken();
//Modify End
    void ReleaseFeature();
    void Shutdown();

    FrameworkDeviceContext& m_DeviceContext;
    std::unique_ptr<InternalState> m_InternalState;
    DLSSMode m_Mode = DLSSMode::Disabled;
    bool m_Initialized = false;
    bool m_Supported = false;
    bool m_HistoryReset = true;
    bool m_RayReconstructionEnabled = false;
    bool m_FrameGenerationEnabled = false;
//Modify Begin:2026-08-07 by BestHui
    bool m_FrameGenerationPrepared = false;
    uint32_t m_ActiveStreamlineFrameIndex = UINT32_MAX;
    void* m_ActiveStreamlineFrameToken = nullptr;
//Modify End
    std::string m_StatusMessage;
};
//Modify End
