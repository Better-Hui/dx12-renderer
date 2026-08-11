//Modify Begin:2026-07-27 by BestHui
#include <RenderGraph/RaytracingDemoRenderGraphBuilder.h>

#include <RenderGraph/RaytracingDemoGraphResources.h>
#include <Passes/RaytracingDemoPasses.h>
#include <RaytracingDemo.h>
#include <RenderGraph/RenderPass.h>

#include <utility>
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
//Modify Begin:2026-08-10 by BestHui
    if (frameState.MaxBounces <= 1)
    {
        renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateDisabledIndirectLightingPass());
    }
    else
    {
        switch (frameState.IndirectLightingTechnique)
        {
        case RaytracingDemoLightingTechnique::PathTracing:
            renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateIndirectLightingPass(resources, config));
            break;
        case RaytracingDemoLightingTechnique::ReSTIRGI:
            if (frameState.Backend == PathTracingBackend::InlineRayQuery)
            {
                renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateReSTIRGIPass(resources, config));
                break;
            }
            [[fallthrough]];
        case RaytracingDemoLightingTechnique::None:
        default:
            renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateDisabledIndirectLightingPass());
            break;
        }
    }
//Modify End
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
        if (frameState.RayReconstructionEnabled)
        {
            renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateDLSSRayReconstructionPreparationPass(resources));
        }
        renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateDLSSPass(resources, config, sceneReadyToken));
        sceneReadyToken = RaytracingDemoRenderGraph::ResourceIds::DLSSFinishedToken;
        displayColor = RaytracingDemoRenderGraph::ResourceIds::DLSSOutput;
    }
    if (frameState.FrameGenerationEnabled)
    {
        renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateFrameGenerationHudLessPass(
            resources,
            displayColor,
            sceneReadyToken));
        sceneReadyToken = RaytracingDemoRenderGraph::ResourceIds::FrameGenerationHudLessFinishedToken;
        displayColor = RaytracingDemoRenderGraph::ResourceIds::FrameGenerationHudLess;
    }
//Modify End

    std::vector<RenderGraph::ResourceId> externalOutputs = {
        displayColor,
        sceneReadyToken,
    };
    if (frameState.FrameGenerationEnabled)
    {
        externalOutputs.emplace_back(RaytracingDemoRenderGraph::ResourceIds::DepthBuffer);
        externalOutputs.emplace_back(RaytracingDemoRenderGraph::ResourceIds::MotionVector);
    }

    return std::make_unique<RenderGraph::RenderGraphRoot>(
        resources.DeviceContext,
        resources.Device,
        resources.DirectQueue,
        resources.AsyncComputeQueue,
        std::move(renderPasses),
//Modify Begin:2026-08-07 by BestHui
        RaytracingDemoRenderGraph::CreateTextureDescriptions(
            frameState.DLSSEnabled,
            frameState.FrameGenerationEnabled,
            frameState.DLSSEnabled && frameState.RayReconstructionEnabled),
//Modify End
        RaytracingDemoRenderGraph::CreateBufferDescriptions(),
//Modify Begin:2026-08-07 by BestHui
        RaytracingDemoRenderGraph::CreateTokenDescriptions(
            frameState.DLSSEnabled,
            frameState.FrameGenerationEnabled),
//Modify End
        std::move(externalOutputs));
}
//Modify End
