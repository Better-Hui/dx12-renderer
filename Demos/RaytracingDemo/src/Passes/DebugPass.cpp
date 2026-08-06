//Modify Begin:2026-07-31 by BestHui
#include <Passes/RaytracingDemoPasses.h>

#include <RenderGraph/RaytracingDemoGraphResources.h>

#include <Framework/Geometry/Mesh.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>
#include <RenderGraph/RenderContext.h>
#include <RenderGraph/RenderPass.h>

std::unique_ptr<RenderGraph::RenderPass> RaytracingDemoPasses::Builder::CreateDebugTexturePass(
    const RaytracingDemoPassResources& resources,
    const RenderGraph::ResourceId debugTarget,
    const RenderGraph::ResourceId debugTargetReadyToken)
{
    using namespace RenderGraph;
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;

    return RenderPass::Create(
        L"Debug Texture",
        {
            { debugTargetReadyToken, InputType::Token },
            { debugTarget, InputType::ShaderResource },
        },
        {
            { DemoResourceIds::SceneColor, OutputType::RenderTarget },
            { DemoResourceIds::DebugOutputFinishedToken, OutputType::Token },
        },
        [resources, debugTarget](const RenderContext& context, CommandList& cmd)
        {
            CommandContext commandContext(cmd);
            commandContext.SetTexture(
                *resources.DisplayCompositeShader,
                "SceneColor",
                ShaderResourceView(context.GetTexture(debugTarget)));
            commandContext.BindPipeline(*resources.DisplayCompositeShader);
            commandContext.BindDescriptorSet(resources.DisplayCompositeShader->GetDescriptorSet());
            resources.DisplayBlitMesh->Draw(cmd);
        });
}
//Modify End
