#include <Passes/RaytracingDemoPasses.h>

#include <Passes/RaytracingDemoPassResources.h>
#include <RaytracingDemo.h>

#include <Framework/ImGuiImpl.h>
#include <RenderGraph/RenderPass.h>

std::unique_ptr<RenderGraph::RenderPass> RaytracingDemoPasses::Builder::CreateImGuiPass(RaytracingDemo& demo)
{
    using namespace RenderGraph;
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;

    return RenderPass::Create(
        L"ImGui",
        {
            { DemoResourceIds::LightBillboardFinishedToken, InputType::Token },
        },
        {
            { DemoResourceIds::DisplayColor, OutputType::RenderTarget },
        },
        [&demo](const RenderContext&, CommandList& cmd)
        {
            demo.m_ImGui->DrawToRenderTarget(cmd);
        });
}
