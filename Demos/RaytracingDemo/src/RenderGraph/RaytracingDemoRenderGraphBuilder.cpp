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
    renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateDirectLightingPass(demo));
    renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateIndirectLightingPass(demo));
    renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateLightingCompositePass(demo));
//Modify Begin:2026-07-28 by BestHui
    RenderGraph::ResourceId sceneReadyToken = RaytracingDemoRenderGraph::ResourceIds::RayTracingFinishedToken;
    if (demo.IsDenoiserEnabled())
    {
        renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateDenoisePass(demo));
        sceneReadyToken = RaytracingDemoRenderGraph::ResourceIds::DenoiseFinishedToken;
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
