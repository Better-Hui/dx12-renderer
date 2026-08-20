//Modify Begin:2026-08-19 by Hui
#include <RenderGraph/RaytracingDemoRenderGraphBuilder.h>

#include <RenderGraph/RaytracingDemoGraphResources.h>
#include <Passes/RaytracingDemoPasses.h>
#include <RaytracingDemo.h>
#include <RenderGraph/RenderGraphBuilder.h>

#include <utility>
#include <vector>

std::unique_ptr<RenderGraph::RenderGraphRoot> RaytracingDemoRenderGraphBuilder::Create(RaytracingDemo& demo)
{
    const RaytracingDemoPassResources resources = demo.CreatePassResources();
    const RaytracingDemoPassConfig config = demo.CreatePassConfig();
    const RaytracingDemoFrameState& frameState = *config.FrameState;
    RenderGraph::RenderGraphBuilder renderGraphBuilder({
        .AsyncComputeSupported = resources.AsyncComputeQueue != nullptr,
        .CopyQueueSupported = resources.CopyQueue != nullptr,
    });
    RaytracingDemoPasses::Builder::AddBaseResourcesPass(renderGraphBuilder, resources, config);
    const bool usePathTracingDirectLighting =
        frameState.UsesDirectLighting() &&
        frameState.DirectLightingTechnique == RaytracingDemoLightingTechnique::PathTracing;
    const bool usePathTracingIndirectLighting =
        frameState.UsesIndirectLighting() &&
        frameState.IndirectLightingTechnique == RaytracingDemoLightingTechnique::PathTracing;
    const bool useCompactedRayTracedPixels = frameState.UsesCompactedRayTracedPixelDispatch();
    const bool prepareCompactedComputeDispatch =
        (frameState.UsesDirectLighting() &&
            frameState.DirectLightingTechnique == RaytracingDemoLightingTechnique::ReSTIRDI) ||
        (frameState.UsesIndirectLighting() &&
            frameState.IndirectLightingTechnique == RaytracingDemoLightingTechnique::ReSTIRGI);
    if (useCompactedRayTracedPixels)
    {
        RaytracingDemoPasses::Builder::AddActivePixelCompactionPasses(
            renderGraphBuilder,
            resources,
            config,
            usePathTracingDirectLighting,
            usePathTracingIndirectLighting,
            prepareCompactedComputeDispatch);
    }
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
    RenderGraph::ResourceId sceneReadyToken = RaytracingDemoRenderGraph::ResourceIds::RayTracingFinishedToken;
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
    {
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
    }
    if (frameState.SkyboxEnabled)
    {
        RaytracingDemoPasses::Builder::AddSkyboxPass(renderGraphBuilder, resources, config, sceneReadyToken);
        sceneReadyToken = RaytracingDemoRenderGraph::ResourceIds::SkyboxFinishedToken;
    }
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

    std::vector<RenderGraph::ResourceId> externalOutputs = {
        displayColor,
        sceneReadyToken,
    };
    if (useCompactedRayTracedPixels)
    {
        externalOutputs.emplace_back(RaytracingDemoRenderGraph::ResourceIds::ActiveRayPixelCountReadbackFinishedToken);
    }
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
        RaytracingDemoRenderGraph::CreateTextureDescriptions(
            frameState.DLSSEnabled,
            frameState.FrameGenerationEnabled,
            frameState.DLSSEnabled && frameState.RayReconstructionEnabled,
            useFrameworkRasterBloom),
        RaytracingDemoRenderGraph::CreateBufferDescriptions(useCompactedRayTracedPixels),
        RaytracingDemoRenderGraph::CreateTokenDescriptions(
            frameState.DLSSEnabled,
            frameState.FrameGenerationEnabled,
            useCompactedRayTracedPixels),
        RenderGraph::RenderGraphOutputResources{
            .Presentation = displayColor,
            .External = std::move(externalOutputs),
        });
}
//Modify End
