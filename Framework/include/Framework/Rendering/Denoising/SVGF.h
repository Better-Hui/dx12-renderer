//Modify Begin:2026-10-14 by Hui
#pragma once

#include <RenderGraph/ResourceDescription.h>
#include <RenderGraph/ResourceId.h>
#include <DirectXMath.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

class CommandContext;
class ComputeShader;
class FrameworkDeviceContext;
class Texture;
namespace RenderGraph { class RenderGraphBuilder; }
class SVGFGraphPass;

class SVGF
{
public:
    struct Settings
    {
        uint32_t AtrousIterations = 4;
        // Zero-based wavelet iteration fed back into the next frame; -1 uses
        // the unfiltered temporal result. Falcor defaults to the second tap.
        int32_t FeedbackTap = 1;
        float TemporalAlpha = 0.05f;
        float MomentsAlpha = 0.2f;
        float PhiColor = 10.0f;
        float PhiNormal = 128.0f;
        float PhiDepth = 1.0f;
    };

    struct GraphInputs
    {
        RenderGraph::ResourceId NoisyRadiance = 0;
        RenderGraph::ResourceId GBufferNormal = 0;
        RenderGraph::ResourceId GBufferPosition = 0;
        RenderGraph::ResourceId GBufferAlbedoOcclusion = 0;
        RenderGraph::ResourceId GBufferEmissionMetallic = 0;
        RenderGraph::ResourceId MotionVector = 0;
        RenderGraph::ResourceId Depth = 0;
        RenderGraph::ResourceId Output = 0;
        RenderGraph::ResourceId InputToken = 0;
        RenderGraph::ResourceId OutputToken = 0;
        uint32_t Width = 1;
        uint32_t Height = 1;
        RenderGraph::RenderMetadataExpression<uint32_t> WidthExpression;
        RenderGraph::RenderMetadataExpression<uint32_t> HeightExpression;
        std::function<uint64_t()> ResolveFrameIndex;
        std::function<DirectX::XMMATRIX()> ResolveWorldToView;
        std::wstring DiagnosticNamePrefix = L"Framework.SVGF";
    };

    explicit SVGF(FrameworkDeviceContext& deviceContext);
    ~SVGF();
    Settings& GetSettings() { return m_Settings; }
    const Settings& GetSettings() const { return m_Settings; }
    void SetEnabled(bool enabled) { m_Enabled = enabled; }
    bool IsEnabled() const { return m_Enabled; }
    void ResetHistory();
    void AddPasses(RenderGraph::RenderGraphBuilder& builder, GraphInputs inputs);

    // Host-owned recording allocates/submits nothing. All textures must be
    // distinct, equally sized, and declared by the caller before recording.
    // Color alpha carries variance; history length lives in its own texture.
    struct RecordingResources
    {
        std::shared_ptr<Texture> NoisyRadiance, GBufferNormal, GBufferPosition;
        std::shared_ptr<Texture> GBufferAlbedoOcclusion, GBufferEmissionMetallic;
        std::shared_ptr<Texture> MotionVector, Depth, Output;
        std::shared_ptr<Texture> HistoryColorRead, HistoryMomentsRead, HistorySurfaceRead, HistoryLengthRead;
        std::shared_ptr<Texture> HistoryColorWrite, HistoryMomentsWrite, HistorySurfaceWrite, HistoryLengthWrite;
        std::shared_ptr<Texture> Illumination, TemporalColor, Ping, Pong;
        DirectX::XMMATRIX WorldToView = DirectX::XMMatrixIdentity();
        DirectX::XMMATRIX PreviousWorldToView = DirectX::XMMatrixIdentity();
        // True only after the preceding history-producing recording is accepted.
        bool HistoryValid = false;
    };
    void Record(CommandContext& context, const RecordingResources& resources);

private:
    friend class SVGFGraphPass;
    struct PrepareConstants
    {
        DirectX::XMMATRIX WorldToView = DirectX::XMMatrixIdentity();
        uint32_t Width = 1, Height = 1, Padding0 = 0, Padding1 = 0;
    };
    struct TemporalConstants
    {
        DirectX::XMMATRIX PreviousWorldToView = DirectX::XMMatrixIdentity();
        uint32_t Width = 1, Height = 1, ResetHistory = 1, Padding = 0;
        float TemporalAlpha = 0.05f, MomentsAlpha = 0.2f, Padding0 = 0.0f, Padding1 = 0.0f;
    };
    struct FilterConstants
    {
        uint32_t Width = 1, Height = 1, StepSize = 1, Padding = 0;
        float PhiColor = 10.0f, PhiNormal = 128.0f, PhiDepth = 1.0f, Padding1 = 0.0f;
    };
    struct CompositeConstants
    {
        uint32_t Width = 1, Height = 1, Padding0 = 0, Padding1 = 0;
    };

    bool EnsureCreated(uint32_t width, uint32_t height);
    void RecordPrepare(CommandContext& context, const RecordingResources& resources, uint32_t width, uint32_t height);
    void RecordTemporal(CommandContext& context, const RecordingResources& resources, uint32_t width, uint32_t height);
    void RecordFilter(CommandContext& context, const RecordingResources& resources,
        const std::shared_ptr<Texture>& input, const std::shared_ptr<Texture>& output,
        uint32_t width, uint32_t height, uint32_t stepSize, bool filterMoments);
    void RecordComposite(CommandContext& context, const RecordingResources& resources,
        const std::shared_ptr<Texture>& input, uint32_t width, uint32_t height);

    std::unique_ptr<ComputeShader> m_PrepareShader, m_TemporalShader, m_FilterMomentsShader, m_AtrousShader, m_CompositeShader;
    FrameworkDeviceContext& m_DeviceContext;
    Settings m_Settings = {};
    bool m_Enabled = false;
    bool m_HistoryValid = false;
    uint32_t m_Width = 0, m_Height = 0;
    DirectX::XMMATRIX m_PreviousView = DirectX::XMMatrixIdentity();
    std::shared_ptr<Texture> m_HistoryColor[2], m_HistoryMoments[2], m_HistorySurface[2], m_HistoryLength[2];
};
//Modify End
