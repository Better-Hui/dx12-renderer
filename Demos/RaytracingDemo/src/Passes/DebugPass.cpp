//Modify Begin:2026-08-18 by Hui
#include <Passes/RaytracingDemoPasses.h>

#include <RenderGraph/RaytracingDemoGraphResources.h>

#include <Framework/Geometry/Mesh.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>
#include <RenderGraph/RenderContext.h>
#include <RenderGraph/RenderGraphBuilder.h>

namespace
{
    struct DebugTexturePassData
    {
        RaytracingDemoPassResourcesSnapshot Resources;
        RenderGraph::ResourceId DebugTarget = 0;
    };
}

void RaytracingDemoPasses::Builder::AddDebugTexturePass(
    RenderGraph::RenderGraphBuilder& renderGraphBuilder,
    const RaytracingDemoPassResources& resources,
    const RenderGraph::ResourceId debugTarget,
    const RenderGraph::ResourceId debugTargetReadyToken)
{
    using namespace RenderGraph;
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;

    renderGraphBuilder.AddPass<DebugTexturePassData>(
        L"Debug Texture",
        [&resources, debugTarget, debugTargetReadyToken](RenderGraphPassBuilder& passBuilder, DebugTexturePassData& passData)
        {
            passData.Resources.emplace(resources);
            passData.DebugTarget = debugTarget;
            passBuilder.ReadToken(debugTargetReadyToken);
            passBuilder.ReadTexture(debugTarget);
            passBuilder.WriteTexture(DemoResourceIds::SceneColor);
            passBuilder.WriteToken(DemoResourceIds::DebugOutputFinishedToken);
        },
        [](const DebugTexturePassData& passData, const RenderContext& context, CommandList& cmd)
        {
            const RaytracingDemoPassResources& resources = passData.Resources.value();
            CommandContext commandContext(cmd);
            commandContext.SetTexture(
                *resources.DisplayCompositeShader,
                "SceneColor",
                ShaderResourceView(context.GetTexture(passData.DebugTarget)));
            commandContext.BindPipeline(*resources.DisplayCompositeShader);
            commandContext.BindDescriptorSet(resources.DisplayCompositeShader->GetDescriptorSet());
            resources.DisplayBlitMesh->Draw(cmd);
        });
}
//Modify End
