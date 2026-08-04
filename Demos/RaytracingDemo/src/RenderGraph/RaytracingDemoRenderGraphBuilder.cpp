//Modify Begin:2026-07-27 by BestHui
#include <RenderGraph/RaytracingDemoRenderGraphBuilder.h>

#include <RenderGraph/RaytracingDemoGraphResources.h>
#include <Passes/RaytracingDemoPasses.h>
#include <RaytracingDemo.h>
#include <RenderGraph/RenderPass.h>

#include <vector>

std::unique_ptr<RenderGraph::RenderGraphRoot> RaytracingDemoRenderGraphBuilder::Create(RaytracingDemo& demo)
{
    std::vector<std::unique_ptr<RenderGraph::RenderPass>> renderPasses;
    renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateBaseResourcesPass(demo));
    renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateIndirectLightingPass(demo));
    renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateDirectLightingPass(demo));
    renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateLightingCompositePass(demo));
//Modify Begin:2026-07-28 by BestHui
    RenderGraph::ResourceId sceneReadyToken = RaytracingDemoRenderGraph::ResourceIds::RayTracingFinishedToken;
//Modify Begin:2026-07-31 by BestHui
    if (demo.m_UseMeshletGBuffer && demo.m_DebugMeshletClusters)
    {
        RenderGraph::ResourceId debugTarget = RaytracingDemoRenderGraph::ResourceIds::GBufferAlbedoOcclusion;
        switch (demo.m_DebugTextureTarget)
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
            demo,
            debugTarget,
            sceneReadyToken));
        sceneReadyToken = RaytracingDemoRenderGraph::ResourceIds::DebugOutputFinishedToken;
    }
    else
//Modify End
    {
//Modify Begin:2026-07-30 by BestHui
        if (demo.IsDenoiserEnabled())
        {
            renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateDenoisePass(demo));
            sceneReadyToken = RaytracingDemoRenderGraph::ResourceIds::DenoiseFinishedToken;
        }

        RenderGraph::ResourceId debugTarget = 0;
        switch (demo.m_DebugLightingTextureTarget)
        {
        case 1:
            debugTarget = RaytracingDemoRenderGraph::ResourceIds::IndirectLighting;
            break;
        case 2:
            debugTarget = RaytracingDemoRenderGraph::ResourceIds::NRDNoisyRadiance;
            break;
        case 3:
            if (demo.m_Denoisers.IsNRDEnabled())
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
                demo,
                debugTarget,
                sceneReadyToken));
            sceneReadyToken = RaytracingDemoRenderGraph::ResourceIds::DebugOutputFinishedToken;
        }
//Modify End
    }
    if (demo.m_SkyboxEnabled)
    {
        renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateSkyboxPass(demo, sceneReadyToken));
        sceneReadyToken = RaytracingDemoRenderGraph::ResourceIds::SkyboxFinishedToken;
    }
    if (demo.m_CudaBloom.IsEnabled())
    {
        renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateCudaBloomPass(demo, sceneReadyToken));
        sceneReadyToken = RaytracingDemoRenderGraph::ResourceIds::CudaBloomFinishedToken;
    }
//Modify End

    return std::make_unique<RenderGraph::RenderGraphRoot>(
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
