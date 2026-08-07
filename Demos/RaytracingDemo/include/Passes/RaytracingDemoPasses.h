#pragma once

#include <RenderGraph/ResourceId.h>

//Modify Begin:2026-07-30 by BestHui
#include <Passes/RaytracingDemoPassResources.h>
//Modify End

#include <memory>
#include <vector>

namespace RenderGraph
{
    class RenderPass;
}

namespace RaytracingDemoPasses
{
    class Builder
    {
    public:
//Modify Begin:2026-07-30 by BestHui
        static std::unique_ptr<RenderGraph::RenderPass> CreateBaseResourcesPass(
            const RaytracingDemoPassResources& resources,
            const RaytracingDemoPassConfig& config);
//Modify Begin:2026-07-28 by BestHui
        static std::unique_ptr<RenderGraph::RenderPass> CreateSkyboxPass(
            const RaytracingDemoPassResources& resources,
            const RaytracingDemoPassConfig& config,
            RenderGraph::ResourceId sceneReadyToken);
//Modify End
        static std::unique_ptr<RenderGraph::RenderPass> CreateDirectLightingPass(
            const RaytracingDemoPassResources& resources,
            const RaytracingDemoPassConfig& config);
//Modify Begin:2026-08-06 by BestHui
        static std::unique_ptr<RenderGraph::RenderPass> CreateDisabledDirectLightingPass();
//Modify End
        static std::unique_ptr<RenderGraph::RenderPass> CreateIndirectLightingPass(
            const RaytracingDemoPassResources& resources,
            const RaytracingDemoPassConfig& config);
        static std::unique_ptr<RenderGraph::RenderPass> CreateReSTIRDIPass(
            const RaytracingDemoPassResources& resources,
            const RaytracingDemoPassConfig& config);
        static std::unique_ptr<RenderGraph::RenderPass> CreateLightingCompositePass(
            const RaytracingDemoPassResources& resources,
            const RaytracingDemoPassConfig& config);
//Modify Begin:2026-07-31 by BestHui
        static std::unique_ptr<RenderGraph::RenderPass> CreateDebugTexturePass(
            const RaytracingDemoPassResources& resources,
            RenderGraph::ResourceId debugTarget,
            RenderGraph::ResourceId debugTargetReadyToken);
//Modify End
        static std::unique_ptr<RenderGraph::RenderPass> CreateDenoisePass(
            const RaytracingDemoPassResources& resources,
            const RaytracingDemoPassConfig& config);
        static std::unique_ptr<RenderGraph::RenderPass> CreateCudaBloomPass(
            const RaytracingDemoPassResources& resources,
            RenderGraph::ResourceId sceneReadyToken);
//Modify Begin:2026-08-07 by BestHui
        static std::unique_ptr<RenderGraph::RenderPass> CreateDLSSPass(
            const RaytracingDemoPassResources& resources,
            const RaytracingDemoPassConfig& config,
            RenderGraph::ResourceId sceneReadyToken);
//Modify End
//Modify End
    };
}
