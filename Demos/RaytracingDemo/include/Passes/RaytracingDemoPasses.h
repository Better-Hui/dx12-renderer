#pragma once

#include <RenderGraph/ResourceId.h>

//Modify Begin:2026-07-30 by Hui
#include <Passes/RaytracingDemoPassResources.h>
//Modify End

namespace RenderGraph
{
    class RenderGraphBuilder;
}

namespace RaytracingDemoPasses
{
    class Builder
    {
    public:
//Modify Begin:2026-08-18 by Hui
        static void AddBaseResourcesPass(
            RenderGraph::RenderGraphBuilder& renderGraphBuilder,
            const RaytracingDemoPassResources& resources,
            const RaytracingDemoPassConfig& config);
        static void AddSkyboxPass(
            RenderGraph::RenderGraphBuilder& renderGraphBuilder,
            const RaytracingDemoPassResources& resources,
            const RaytracingDemoPassConfig& config,
            RenderGraph::ResourceId sceneReadyToken);
        static void AddDirectLightingPass(
            RenderGraph::RenderGraphBuilder& renderGraphBuilder,
            const RaytracingDemoPassResources& resources,
            const RaytracingDemoPassConfig& config);
        static void AddPathTracingCompactedDispatchPasses(
            RenderGraph::RenderGraphBuilder& renderGraphBuilder,
            const RaytracingDemoPassResources& resources,
            const RaytracingDemoPassConfig& config,
            bool prepareDirectLighting,
            bool prepareIndirectLighting);
        static void AddIndirectLightingPass(
            RenderGraph::RenderGraphBuilder& renderGraphBuilder,
            const RaytracingDemoPassResources& resources,
            const RaytracingDemoPassConfig& config);
        static void AddReSTIRGIPass(
            RenderGraph::RenderGraphBuilder& renderGraphBuilder,
            const RaytracingDemoPassResources& resources,
            const RaytracingDemoPassConfig& config);
        static void AddReSTIRDIPass(
            RenderGraph::RenderGraphBuilder& renderGraphBuilder,
            const RaytracingDemoPassResources& resources,
            const RaytracingDemoPassConfig& config);
        static void AddLightingCompositePass(
            RenderGraph::RenderGraphBuilder& renderGraphBuilder,
            const RaytracingDemoPassResources& resources,
            const RaytracingDemoPassConfig& config);
        static void AddDebugTexturePass(
            RenderGraph::RenderGraphBuilder& renderGraphBuilder,
            const RaytracingDemoPassResources& resources,
            RenderGraph::ResourceId debugTarget,
            RenderGraph::ResourceId debugTargetReadyToken);
        static void AddDenoisePass(
            RenderGraph::RenderGraphBuilder& renderGraphBuilder,
            const RaytracingDemoPassResources& resources,
            const RaytracingDemoPassConfig& config);
        static void AddCudaBloomPass(
            RenderGraph::RenderGraphBuilder& renderGraphBuilder,
            const RaytracingDemoPassResources& resources,
            RenderGraph::ResourceId sceneReadyToken);
        static void AddFrameworkBloomPass(
            RenderGraph::RenderGraphBuilder& renderGraphBuilder,
            const RaytracingDemoPassResources& resources,
            RenderGraph::ResourceId sceneReadyToken);
        static void AddDLSSPass(
            RenderGraph::RenderGraphBuilder& renderGraphBuilder,
            const RaytracingDemoPassResources& resources,
            const RaytracingDemoPassConfig& config,
            RenderGraph::ResourceId inputColor,
            RenderGraph::ResourceId sceneReadyToken);
        static void AddDLSSRayReconstructionPreparationPass(
            RenderGraph::RenderGraphBuilder& renderGraphBuilder,
            const RaytracingDemoPassResources& resources);
        static void AddFrameGenerationHudLessPass(
            RenderGraph::RenderGraphBuilder& renderGraphBuilder,
            const RaytracingDemoPassResources& resources,
            RenderGraph::ResourceId sceneColor,
            RenderGraph::ResourceId sceneReadyToken);
//Modify End
    };
}
