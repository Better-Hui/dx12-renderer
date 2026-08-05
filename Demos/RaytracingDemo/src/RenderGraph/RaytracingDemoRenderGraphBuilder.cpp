//Modify Begin:2026-07-27 by BestHui
#include <RenderGraph/RaytracingDemoRenderGraphBuilder.h>

#include <RenderGraph/RaytracingDemoGraphResources.h>
#include <Passes/RaytracingDemoPasses.h>
#include <RaytracingDemo.h>
#include <RenderGraph/RenderPass.h>

#include <vector>

std::unique_ptr<RenderGraph::RenderGraphRoot> RaytracingDemoRenderGraphBuilder::Create(RaytracingDemo& demo)
{
//Modify Begin:2026-07-30 by BestHui
    const RaytracingDemoPassResources resources = demo.CreatePassResources();
    const RaytracingDemoPassConfig config = demo.CreatePassConfig();
//Modify End
    std::vector<std::unique_ptr<RenderGraph::RenderPass>> renderPasses;
    renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateBaseResourcesPass(resources, config));
    renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateIndirectLightingPass(resources, config));
    renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateDirectLightingPass(resources, config));
//Modify Begin:2026-08-05 by BestHui
    renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateReSTIRDIRISPass(resources, config));
    renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateReSTIRDITemporalPass(resources, config));
    renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateReSTIRDIBoilingPass(resources, config));
    renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateReSTIRDISpatialPass(resources, config));
    renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateReSTIRDIShadePass(resources, config));
//Modify End
    renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateLightingCompositePass(resources, config));
//Modify Begin:2026-07-28 by BestHui
    RenderGraph::ResourceId sceneReadyToken = RaytracingDemoRenderGraph::ResourceIds::RayTracingFinishedToken;
//Modify Begin:2026-07-31 by BestHui
    if (config.UseMeshletGBuffer != nullptr && *config.UseMeshletGBuffer &&
        config.DebugMeshletClusters != nullptr && *config.DebugMeshletClusters)
    {
        RenderGraph::ResourceId debugTarget = RaytracingDemoRenderGraph::ResourceIds::GBufferAlbedoOcclusion;
        const int debugTextureTarget = config.DebugTextureTarget != nullptr
            ? *config.DebugTextureTarget
            : 0;
        switch (debugTextureTarget)
        {
        case 1:
            debugTarget = RaytracingDemoRenderGraph::ResourceIds::GBufferNormal;
            break;
        case 2:
            debugTarget = RaytracingDemoRenderGraph::ResourceIds::GBufferPosition;
            break;
        case 3:
            debugTarget = RaytracingDemoRenderGraph::ResourceIds::MotionVector;
            break;
        default:
            debugTarget = RaytracingDemoRenderGraph::ResourceIds::GBufferAlbedoOcclusion;
            break;
        }
        renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateDebugTexturePass(
            resources,
            debugTarget,
            sceneReadyToken));
        sceneReadyToken = RaytracingDemoRenderGraph::ResourceIds::DebugOutputFinishedToken;
    }
    else
//Modify End
    {
//Modify Begin:2026-07-30 by BestHui
        if (resources.Denoisers.IsEnabled())
        {
            renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateDenoisePass(resources, config));
            sceneReadyToken = RaytracingDemoRenderGraph::ResourceIds::DenoiseFinishedToken;
        }

        RenderGraph::ResourceId debugTarget = 0;
        const int debugLightingTextureTarget = config.DebugLightingTextureTarget != nullptr
            ? *config.DebugLightingTextureTarget
            : 0;
        switch (debugLightingTextureTarget)
        {
        case 1:
            debugTarget = RaytracingDemoRenderGraph::ResourceIds::IndirectLighting;
            break;
        case 2:
            debugTarget = RaytracingDemoRenderGraph::ResourceIds::NRDNoisyRadiance;
            break;
        case 3:
            if (resources.Denoisers.IsNRDEnabled())
            {
                debugTarget = RaytracingDemoRenderGraph::ResourceIds::NRDDenoisedRadiance;
            }
            break;
        default:
            break;
        }

        if (debugTarget != 0)
        {
            renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateDebugTexturePass(
                resources,
                debugTarget,
                sceneReadyToken));
            sceneReadyToken = RaytracingDemoRenderGraph::ResourceIds::DebugOutputFinishedToken;
        }
//Modify End
    }
    if (config.SkyboxEnabled != nullptr && *config.SkyboxEnabled)
    {
        renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateSkyboxPass(resources, config, sceneReadyToken));
        sceneReadyToken = RaytracingDemoRenderGraph::ResourceIds::SkyboxFinishedToken;
    }
    if (resources.CudaBloom.IsEnabled())
    {
        renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateCudaBloomPass(resources, sceneReadyToken));
        sceneReadyToken = RaytracingDemoRenderGraph::ResourceIds::CudaBloomFinishedToken;
    }
//Modify End

    return std::make_unique<RenderGraph::RenderGraphRoot>(
        resources.Device,
        resources.DirectQueue,
        resources.AsyncComputeQueue,
        std::move(renderPasses),
        RaytracingDemoRenderGraph::CreateTextureDescriptions(),
        RaytracingDemoRenderGraph::CreateBufferDescriptions(),
        RaytracingDemoRenderGraph::CreateTokenDescriptions(),
        std::vector<RenderGraph::ResourceId>{
            RaytracingDemoRenderGraph::ResourceIds::SceneColor,
            sceneReadyToken
        });
}
//Modify End
