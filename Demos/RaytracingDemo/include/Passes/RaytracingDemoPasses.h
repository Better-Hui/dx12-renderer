#pragma once

#include <memory>
#include <vector>

namespace RenderGraph
{
    class RenderPass;
}

class RaytracingDemo;

namespace RaytracingDemoPasses
{
    class Builder
    {
    public:
        static std::unique_ptr<RenderGraph::RenderPass> CreateBaseResourcesPass(RaytracingDemo& demo);
        static std::unique_ptr<RenderGraph::RenderPass> CreateSkyboxPass(RaytracingDemo& demo);
        static std::unique_ptr<RenderGraph::RenderPass> CreateDirectLightingPass(RaytracingDemo& demo);
        static std::unique_ptr<RenderGraph::RenderPass> CreateIndirectLightingPass(RaytracingDemo& demo);
        static std::unique_ptr<RenderGraph::RenderPass> CreateLightingCompositePass(RaytracingDemo& demo);
        static std::unique_ptr<RenderGraph::RenderPass> CreateDenoisePass(RaytracingDemo& demo);
        static std::unique_ptr<RenderGraph::RenderPass> CreateLightBillboardPass(RaytracingDemo& demo);
        static std::unique_ptr<RenderGraph::RenderPass> CreateImGuiPass(RaytracingDemo& demo);
    };
}
