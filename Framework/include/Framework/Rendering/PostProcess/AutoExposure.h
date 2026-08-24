//Modify Begin:2026-08-24 by Hui
#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include <RenderGraph/ResourceId.h>

class ByteAddressBuffer;
class CommandList;
class ComputeShader;
class FrameworkDeviceContext;
class Texture;

namespace RenderGraph
{
    class FrameContext;
    using RenderContext = FrameContext;
    class RenderGraphBuilder;
}

class AutoExposure final
{
public:
    struct Settings
    {
        bool Enabled = true;
        float Tau = 1.1f;
        float MinLogLuminance = -10.0f;
        float MaxLogLuminance = 2.0f;
    };

    explicit AutoExposure(FrameworkDeviceContext& deviceContext);

    struct FrameInputs
    {
        std::shared_ptr<Texture> Source;
        std::shared_ptr<Texture> Output;
        uint32_t InputWidth = 1u;
        uint32_t InputHeight = 1u;
        uint32_t OutputWidth = 1u;
        uint32_t OutputHeight = 1u;
        float DeltaTime = 0.0f;
    };

    struct GraphInputs
    {
        RenderGraph::ResourceId Source = 0;
        RenderGraph::ResourceId Output = 0;
        RenderGraph::ResourceId InputToken = 0;
        RenderGraph::ResourceId OutputToken = 0;
        uint32_t OutputWidth = 1u;
        uint32_t OutputHeight = 1u;
        std::function<FrameInputs(const RenderGraph::RenderContext&)> ResolveFrameInputs;
    };

    void SetSettings(const Settings& settings);
    const Settings& GetSettings() const;

    void AddPasses(RenderGraph::RenderGraphBuilder& builder, GraphInputs inputs);

    void ResetHistory();

private:
    void EnsureResources(uint32_t outputWidth, uint32_t outputHeight);
    void RecordPrepare(CommandList& commandList, const FrameInputs& inputs);
    void RecordBuildHistogram(CommandList& commandList, const FrameInputs& inputs);
    void RecordAverageHistogram(CommandList& commandList, const FrameInputs& inputs);
    void RecordApply(CommandList& commandList, const FrameInputs& inputs);

    FrameworkDeviceContext& m_DeviceContext;
    std::unique_ptr<ComputeShader> m_BuildHistogramShader;
    std::unique_ptr<ComputeShader> m_AverageHistogramShader;
    std::unique_ptr<ComputeShader> m_ApplyShader;
    std::shared_ptr<ByteAddressBuffer> m_Histogram;
    std::shared_ptr<Texture> m_AdaptedLuminance;
    uint32_t m_OutputWidth = 0;
    uint32_t m_OutputHeight = 0;
    bool m_HistoryValid = false;
    Settings m_Settings;
};
//Modify End
