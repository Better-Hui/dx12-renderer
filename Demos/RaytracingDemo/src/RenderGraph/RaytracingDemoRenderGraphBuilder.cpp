//Modify Begin:2026-07-27 by Hui
#include <RenderGraph/RaytracingDemoRenderGraphBuilder.h>

#include <RenderGraph/RaytracingDemoGraphResources.h>
#include <Passes/RaytracingDemoPasses.h>
#include <RaytracingDemo.h>
#include <RenderGraph/RenderGraphBuilder.h>

#include <utility>
#include <vector>

std::unique_ptr<RenderGraph::RenderGraphRoot> RaytracingDemoRenderGraphBuilder::Create(RaytracingDemo& demo)
{
//Modify Begin:2026-07-30 by Hui
    const RaytracingDemoPassResources resources = demo.CreatePassResources();
    const RaytracingDemoPassConfig config = demo.CreatePassConfig();
//Modify End
//Modify Begin:2026-07-30 by Hui
    const RaytracingDemoFrameState& frameState = *config.FrameState;
//Modify End
//Modify Begin:2026-08-18 by Hui
    RenderGraph::RenderGraphBuilder renderGraphBuilder({
        .AsyncComputeSupported = resources.AsyncComputeQueue != nullptr,
        .CopyQueueSupported = resources.CopyQueue != nullptr,
    });
    RaytracingDemoPasses::Builder::AddBaseResourcesPass(renderGraphBuilder, resources, config);
//Modify End
    if (frameState.UsesIndirectLighting())
    {
        switch (frameState.IndirectLightingTechnique)
        {
        case RaytracingDemoLightingTechnique::PathTracing:
            RaytracingDemoPasses::Builder::AddIndirectLightingPass(renderGraphBuilder, resources, config);
            break;
        case RaytracingDemoLightingTechnique::ReSTIRGI:
            if (frameState.Backend == PathTracingBackend::InlineRayQuery)
            {
                RaytracingDemoPasses::Builder::AddReSTIRGIPass(renderGraphBuilder, resources, config);
                break;
            }
        default:
            break;
        }
    }
    if (frameState.UsesDirectLighting())
    {
        switch (frameState.DirectLightingTechnique)
        {
        case RaytracingDemoLightingTechnique::PathTracing:
            RaytracingDemoPasses::Builder::AddDirectLightingPass(renderGraphBuilder, resources, config);
            break;
        case RaytracingDemoLightingTechnique::ReSTIRDI:
            RaytracingDemoPasses::Builder::AddReSTIRDIPass(renderGraphBuilder, resources, config);
            break;
        default:
            break;
        }
    }
    RaytracingDemoPasses::Builder::AddLightingCompositePass(renderGraphBuilder, resources, config);
//Modify Begin:2026-07-28 by Hui
    RenderGraph::ResourceId sceneReadyToken = RaytracingDemoRenderGraph::ResourceIds::RayTracingFinishedToken;
//Modify Begin:2026-07-31 by Hui
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
        RaytracingDemoPasses::Builder::AddDebugTexturePass(
            renderGraphBuilder,
            resources,
            debugTarget,
            sceneReadyToken);
        sceneReadyToken = RaytracingDemoRenderGraph::ResourceIds::DebugOutputFinishedToken;
    }
    else
//Modify End
    {
//Modify Begin:2026-07-30 by Hui
        if (frameState.DenoiserEnabled)
        {
            RaytracingDemoPasses::Builder::AddDenoisePass(renderGraphBuilder, resources, config);
            sceneReadyToken = RaytracingDemoRenderGraph::ResourceIds::DenoiseFinishedToken;
        }

        RenderGraph::ResourceId debugTarget = 0;
        const int debugLightingTextureTarget = frameState.DebugLightingTextureTarget;
        switch (debugLightingTextureTarget)
        {
        case 1:
            if (frameState.UsesIndirectLighting())
            {
                debugTarget = RaytracingDemoRenderGraph::ResourceIds::IndirectLighting;
            }
            break;
        case 2:
            if (frameState.DenoiserAlgorithm == DenoiserController::Algorithm::NRD)
            {
                debugTarget = RaytracingDemoRenderGraph::ResourceIds::NRDNoisyRadiance;
            }
            break;
        case 3:
            if (frameState.DenoiserAlgorithm == DenoiserController::Algorithm::NRD)
            {
                debugTarget = RaytracingDemoRenderGraph::ResourceIds::NRDDenoisedRadiance;
            }
            break;
        default:
            break;
        }

        if (debugTarget != 0)
        {
            RaytracingDemoPasses::Builder::AddDebugTexturePass(
                renderGraphBuilder,
                resources,
                debugTarget,
                sceneReadyToken);
            sceneReadyToken = RaytracingDemoRenderGraph::ResourceIds::DebugOutputFinishedToken;
        }
//Modify End
    }
    if (frameState.SkyboxEnabled)
    {
        RaytracingDemoPasses::Builder::AddSkyboxPass(renderGraphBuilder, resources, config, sceneReadyToken);
        sceneReadyToken = RaytracingDemoRenderGraph::ResourceIds::SkyboxFinishedToken;
    }
//Modify Begin:2026-08-17 by Hui
    const bool useFrameworkRasterBloom = resources.CudaBloom.IsEnabled() && resources.CudaBloom.IsFrameworkRaster();
    if (resources.CudaBloom.IsEnabled())
    {
        if (useFrameworkRasterBloom)
        {
            RaytracingDemoPasses::Builder::AddFrameworkBloomPass(renderGraphBuilder, resources, sceneReadyToken);
        }
        else
        {
            RaytracingDemoPasses::Builder::AddCudaBloomPass(renderGraphBuilder, resources, sceneReadyToken);
        }
        sceneReadyToken = RaytracingDemoRenderGraph::ResourceIds::CudaBloomFinishedToken;
    }
//Modify End

//Modify Begin:2026-08-07 by Hui
    RenderGraph::ResourceId displayColor = useFrameworkRasterBloom
        ? RaytracingDemoRenderGraph::ResourceIds::BloomOutput
        : RaytracingDemoRenderGraph::ResourceIds::SceneColor;
    if (frameState.DLSSEnabled)
    {
        if (frameState.RayReconstructionEnabled)
        {
            RaytracingDemoPasses::Builder::AddDLSSRayReconstructionPreparationPass(renderGraphBuilder, resources);
        }
        RaytracingDemoPasses::Builder::AddDLSSPass(
            renderGraphBuilder,
            resources,
            config,
            displayColor,
            sceneReadyToken);
        sceneReadyToken = RaytracingDemoRenderGraph::ResourceIds::DLSSFinishedToken;
        displayColor = RaytracingDemoRenderGraph::ResourceIds::DLSSOutput;
    }
    if (frameState.FrameGenerationEnabled)
    {
        RaytracingDemoPasses::Builder::AddFrameGenerationHudLessPass(
            renderGraphBuilder,
            resources,
            displayColor,
            sceneReadyToken);
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
        resources.CopyQueue,
        renderGraphBuilder.ReleasePasses(),
//Modify Begin:2026-08-07 by Hui
        RaytracingDemoRenderGraph::CreateTextureDescriptions(
            frameState.DLSSEnabled,
            frameState.FrameGenerationEnabled,
            frameState.DLSSEnabled && frameState.RayReconstructionEnabled,
            useFrameworkRasterBloom),
//Modify End
        RaytracingDemoRenderGraph::CreateBufferDescriptions(),
//Modify Begin:2026-08-07 by Hui
        RaytracingDemoRenderGraph::CreateTokenDescriptions(
            frameState.DLSSEnabled,
            frameState.FrameGenerationEnabled),
//Modify End
        RenderGraph::RenderGraphOutputResources{
            .Presentation = displayColor,
            .External = std::move(externalOutputs),
        });
}
//Modify End
