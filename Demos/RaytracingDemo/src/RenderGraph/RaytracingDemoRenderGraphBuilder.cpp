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
//Modify Begin:2026-07-30 by BestHui
    const RaytracingDemoFrameState& frameState = *config.FrameState;
//Modify End
    std::vector<std::unique_ptr<RenderGraph::RenderPass>> renderPasses;
    renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateBaseResourcesPass(resources, config));
    renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateIndirectLightingPass(resources, config));
//Modify Begin:2026-08-06 by BestHui
    switch (frameState.DirectLightingTechnique)
    {
    case RaytracingDemoLightingTechnique::PathTracing:
        renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateDirectLightingPass(resources, config));
        break;
    case RaytracingDemoLightingTechnique::ReSTIRDI:
        if (frameState.Backend == PathTracingBackend::InlineRayQuery)
        {
            renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateReSTIRDIPass(resources, config));
            break;
        }
        [[fallthrough]];
    case RaytracingDemoLightingTechnique::None:
    default:
        renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateDisabledDirectLightingPass());
        break;
    }
//Modify End
    renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateLightingCompositePass(resources, config));
//Modify Begin:2026-07-28 by BestHui
    RenderGraph::ResourceId sceneReadyToken = RaytracingDemoRenderGraph::ResourceIds::RayTracingFinishedToken;
//Modify Begin:2026-07-31 by BestHui
    if (frameState.UseMeshletGBuffer && frameState.DebugMeshletClusters)
    {
        RenderGraph::ResourceId debugTarget = RaytracingDemoRenderGraph::ResourceIds::GBufferAlbedoOcclusion;
        const int debugTextureTarget = frameState.DebugTextureTarget;
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
        const int debugLightingTextureTarget = frameState.DebugLightingTextureTarget;
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
    if (frameState.SkyboxEnabled)
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

//Modify Begin:2026-08-07 by BestHui
    RenderGraph::ResourceId displayColor = RaytracingDemoRenderGraph::ResourceIds::SceneColor;
    if (frameState.DLSSEnabled)
    {
        renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateDLSSPass(resources, config, sceneReadyToken));
        sceneReadyToken = RaytracingDemoRenderGraph::ResourceIds::DLSSFinishedToken;
        displayColor = RaytracingDemoRenderGraph::ResourceIds::DLSSOutput;
    }
//Modify End

    return std::make_unique<RenderGraph::RenderGraphRoot>(
        resources.Device,
        resources.DirectQueue,
        resources.AsyncComputeQueue,
        std::move(renderPasses),
//Modify Begin:2026-08-07 by BestHui
        RaytracingDemoRenderGraph::CreateTextureDescriptions(frameState.DLSSEnabled),
//Modify End
        RaytracingDemoRenderGraph::CreateBufferDescriptions(),
//Modify Begin:2026-08-07 by BestHui
        RaytracingDemoRenderGraph::CreateTokenDescriptions(frameState.DLSSEnabled),
//Modify End
        std::vector<RenderGraph::ResourceId>{
            displayColor,
            sceneReadyToken
        });
}
//Modify End
