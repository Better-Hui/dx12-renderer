#pragma once

#include <RenderGraph/ResourceId.h>

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
//Modify Begin:2026-07-28 by BestHui
        static std::unique_ptr<RenderGraph::RenderPass> CreateSkyboxPass(RaytracingDemo& demo, RenderGraph::ResourceId sceneReadyToken);
//Modify End
        static std::unique_ptr<RenderGraph::RenderPass> CreateDirectLightingPass(RaytracingDemo& demo);
        static std::unique_ptr<RenderGraph::RenderPass> CreateIndirectLightingPass(RaytracingDemo& demo);
        static std::unique_ptr<RenderGraph::RenderPass> CreateLightingCompositePass(RaytracingDemo& demo);
        static std::unique_ptr<RenderGraph::RenderPass> CreateDenoisePass(RaytracingDemo& demo);
    };
}
