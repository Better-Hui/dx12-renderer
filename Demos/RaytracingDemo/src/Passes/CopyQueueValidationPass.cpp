//Modify Begin:2026-08-25 by Hui
#include <Passes/RaytracingDemoPasses.h>

#include <RenderGraph/RaytracingDemoGraphResources.h>

#include <DX12Library/Helpers.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>
#include <Framework/Rendering/Texture/UnorderedAccessView.h>
#include <RenderGraph/RenderContext.h>
#include <RenderGraph/RenderGraphBuilder.h>

namespace
{
    struct CopyQueueValidationCopyPassData
    {
        RenderGraph::ResourceId SourceColor = 0;
    };

    struct CopyQueueValidationComputePassData
    {
        RaytracingDemoPassResourcesSnapshot Resources;
    };
}

void RaytracingDemoPasses::Builder::AddCopyQueueValidationPass(
    RenderGraph::RenderGraphBuilder& renderGraphBuilder,
    const RaytracingDemoPassResources& resources,
    const RenderGraph::ResourceId sourceColor,
    const RenderGraph::ResourceId sceneReadyToken)
{
    using namespace RenderGraph;
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;

    renderGraphBuilder.AddCopyPass<CopyQueueValidationCopyPassData>(
        L"Copy Queue Validation Copy",
        [sourceColor, sceneReadyToken](RenderGraphPassBuilder& passBuilder, CopyQueueValidationCopyPassData& passData)
        {
            passData.SourceColor = sourceColor;
            passBuilder.ReadToken(sceneReadyToken);
            passBuilder.ReadCopySource(sourceColor);
            passBuilder.WriteCopyDestination(DemoResourceIds::CopyQueueValidationColor);
            passBuilder.WriteToken(DemoResourceIds::CopyQueueValidationFinishedToken);
        },
        [](const CopyQueueValidationCopyPassData& passData, const RenderContext& context, CommandList& commandList)
        {
            commandList.CopyResource(
                *context.GetTexture(DemoResourceIds::CopyQueueValidationColor),
                *context.GetTexture(passData.SourceColor));
        });

    renderGraphBuilder.AddComputePass<CopyQueueValidationComputePassData>(
        L"Copy Queue Validation Compute Consume",
        [&resources](RenderGraphPassBuilder& passBuilder, CopyQueueValidationComputePassData& passData)
        {
            passData.Resources.emplace(resources);
            passBuilder.ReadToken(DemoResourceIds::CopyQueueValidationFinishedToken);
            passBuilder.ReadTexture(DemoResourceIds::CopyQueueValidationColor);
            passBuilder.WriteUav(DemoResourceIds::CopyQueueValidationComputeColor);
            passBuilder.WriteToken(DemoResourceIds::CopyQueueValidationComputeFinishedToken);
        },
        [](const CopyQueueValidationComputePassData& passData, const RenderContext& context, CommandList& commandList)
        {
            ComputeShader& shader = *passData.Resources->CopyQueueValidationShader;
            CommandContext commandContext(commandList);
            commandContext.SetTexture(
                shader,
                "Source",
                ShaderResourceView(context.GetTexture(DemoResourceIds::CopyQueueValidationColor)));
            commandContext.SetUnorderedAccessView(
                shader,
                "Destination",
                UnorderedAccessView(context.GetTexture(DemoResourceIds::CopyQueueValidationComputeColor)));
            commandContext.BindPipeline(shader);
            commandContext.BindDescriptorSet(shader.GetDescriptorSet());
            commandContext.Dispatch(
                Math::DivideByMultiple(context.GetMetadata().m_DisplayWidth, 8u),
                Math::DivideByMultiple(context.GetMetadata().m_DisplayHeight, 8u),
                1u);
        });
}
//Modify End
