//Modify Begin:2026-08-24 by Hui
#pragma once

#include <RenderGraph/ResourceDescription.h>
#include <RenderGraph/ResourceId.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

class CommandList;
class ComputeShader;
class FrameworkDeviceContext;
class Texture;

namespace RenderGraph
{
    class RenderGraphBuilder;
}

class SVGF
{
public:
    struct Settings
    {
        uint32_t AtrousIterations = 1;
        float TemporalAlpha = 0.08f;
        float MomentsAlpha = 0.2f;
        float PhiColor = 4.0f;
        float PhiNormal = 64.0f;
        float PhiDepth = 1.0f;
    };

    struct GraphInputs
    {
        RenderGraph::ResourceId NoisyRadiance = 0;
        RenderGraph::ResourceId GBufferNormal = 0;
        RenderGraph::ResourceId GBufferPosition = 0;
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

private:
    struct TemporalConstants
    {
        uint32_t Width = 1;
        uint32_t Height = 1;
        uint32_t ResetHistory = 1;
        uint32_t Padding1 = 0;
        float TemporalAlpha = 0.08f;
        float MomentsAlpha = 0.2f;
        float PhiNormal = 64.0f;
        float PhiDepth = 1.0f;
    };

    struct AtrousConstants
    {
        uint32_t Width = 1;
        uint32_t Height = 1;
        uint32_t StepSize = 1;
        uint32_t Direction = 0;
        float PhiColor = 4.0f;
        float PhiNormal = 64.0f;
        float PhiDepth = 1.0f;
        float Padding1 = 0.0f;
    };

    struct CompositeConstants
    {
        uint32_t Width = 1;
        uint32_t Height = 1;
        uint32_t Padding0 = 0;
        uint32_t Padding1 = 0;
    };

    bool EnsureCreated(uint32_t width, uint32_t height);

    void RecordTemporal(
        CommandList& commandList,
        const std::shared_ptr<Texture>& noisyRadiance,
        const std::shared_ptr<Texture>& gBufferNormal,
        const std::shared_ptr<Texture>& gBufferPosition,
        const std::shared_ptr<Texture>& motionVector,
        const std::shared_ptr<Texture>& depthTexture,
        const std::shared_ptr<Texture>& historyColor,
        const std::shared_ptr<Texture>& historyMoments,
        const std::shared_ptr<Texture>& temporalColor,
        const std::shared_ptr<Texture>& temporalMoments,
        const std::shared_ptr<Texture>& variance,
        const std::shared_ptr<Texture>& outputHistoryColor,
        const std::shared_ptr<Texture>& outputHistoryMoments,
        uint32_t width,
        uint32_t height);
    void RecordAtrous(
        CommandList& commandList,
        const std::shared_ptr<Texture>& input,
        const std::shared_ptr<Texture>& output,
        const std::shared_ptr<Texture>& variance,
        const std::shared_ptr<Texture>& gBufferNormal,
        const std::shared_ptr<Texture>& gBufferPosition,
        const std::shared_ptr<Texture>& depthTexture,
        uint32_t width,
        uint32_t height,
        uint32_t stepSize,
        uint32_t direction);
    void RecordComposite(
        CommandList& commandList,
        const std::shared_ptr<Texture>& input,
        const std::shared_ptr<Texture>& depthTexture,
        const std::shared_ptr<Texture>& output,
        uint32_t width,
        uint32_t height);

    std::unique_ptr<ComputeShader> m_TemporalShader;
    std::unique_ptr<ComputeShader> m_AtrousShader;
    std::unique_ptr<ComputeShader> m_CompositeShader;
    FrameworkDeviceContext& m_DeviceContext;
    Settings m_Settings = {};
    bool m_Enabled = false;
    bool m_HistoryValid = false;
    uint32_t m_Width = 0;
    uint32_t m_Height = 0;
    std::shared_ptr<Texture> m_HistoryColor[2];
    std::shared_ptr<Texture> m_HistoryMoments[2];
};
//Modify End
