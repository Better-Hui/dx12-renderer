//Modify Begin:2026-07-27 by BestHui
#include <RenderGraph/RaytracingDemoRenderGraphBuilder.h>

#include <Passes/RaytracingDemoPassResources.h>
#include <Passes/RaytracingDemoPasses.h>
#include <RaytracingDemo.h>
#include <RenderGraph/RenderPass.h>

#include <vector>

std::unique_ptr<RenderGraph::RenderGraphRoot> RaytracingDemoRenderGraphBuilder::Create(RaytracingDemo& demo, CommandList&)
{
    std::vector<std::unique_ptr<RenderGraph::RenderPass>> renderPasses;
    renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateBaseResourcesPass(demo));
    renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateDirectLightingPass(demo));
    renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateIndirectLightingPass(demo));
    renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateLightingCompositePass(demo));
    renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateDenoisePass(demo));
    renderPasses.emplace_back(RaytracingDemoPasses::Builder::CreateSkyboxPass(demo));

    return std::make_unique<RenderGraph::RenderGraphRoot>(
        std::move(renderPasses),
        RaytracingDemoRenderGraph::CreateTextureDescriptions(),
        RaytracingDemoRenderGraph::CreateBufferDescriptions(),
        RaytracingDemoRenderGraph::CreateTokenDescriptions(),
        std::vector<RenderGraph::ResourceId>{
            RaytracingDemoRenderGraph::ResourceIds::SceneColor
        });
}
//Modify End
