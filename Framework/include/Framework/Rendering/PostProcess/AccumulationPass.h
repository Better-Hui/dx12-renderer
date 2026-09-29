#pragma once

#include <RenderGraph/RenderPass.h>

#include <cstdint>
#include <functional>

class ComputeShader;
class FrameworkDeviceContext;

namespace RenderGraph
{
    class RenderGraphBuilder;
}

class AccumulationPass final : public RenderGraph::RenderPass
{
public:
    struct Inputs
    {
        RenderGraph::ResourceId SceneColor = 0;
        RenderGraph::ResourceId HistoryColor = 0;
        RenderGraph::ResourceId InputToken = 0;
        RenderGraph::ResourceId OutputToken = 0;
        uint32_t Width = 1u;
        uint32_t Height = 1u;
        std::function<uint32_t()> ResolvePreviousSampleCount;
    };

    AccumulationPass(FrameworkDeviceContext& deviceContext, Inputs inputs);
    ~AccumulationPass() override;

protected:
    void InitImpl(CommandList& commandList) override;
    void ExecuteImpl(const RenderGraph::RenderContext& context, RenderGraph::RenderPassContext& passContext) override;

private:
    FrameworkDeviceContext& m_DeviceContext;
    Inputs m_Inputs;
    std::unique_ptr<ComputeShader> m_Shader;
};
