//Modify Begin:2026-09-09 by Hui
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
    struct CopyQueueValidationDirectAliasPassData
    {};

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

    renderGraphBuilder.AddPass<CopyQueueValidationDirectAliasPassData>(
        L"Copy Queue Validation Direct Alias Occupant",
        [sceneReadyToken](RenderGraphPassBuilder& passBuilder, CopyQueueValidationDirectAliasPassData&)
        {
            passBuilder.ReadToken(sceneReadyToken);
            passBuilder.WriteUav(DemoResourceIds::CopyQueueAliasDirectScratch);
            passBuilder.WriteToken(DemoResourceIds::CopyQueueAliasDirectReadyToken);
        },
        [](const CopyQueueValidationDirectAliasPassData&, const RenderContext& context, CommandList& commandList)
        {
            constexpr UINT clearValues[4] = { 0x12345678u, 0u, 0u, 0u };
            commandList.ClearUnorderedAccessUint(
                context.GetResource(DemoResourceIds::CopyQueueAliasDirectScratch),
                clearValues);
        });

    renderGraphBuilder.AddCopyPass<CopyQueueValidationCopyPassData>(
        L"Copy Queue Validation Copy",
        [sourceColor](RenderGraphPassBuilder& passBuilder, CopyQueueValidationCopyPassData& passData)
        {
            passData.SourceColor = sourceColor;
            passBuilder.ReadToken(DemoResourceIds::CopyQueueAliasDirectReadyToken);
            passBuilder.ReadCopySource(sourceColor);
            passBuilder.WriteCopyDestination(DemoResourceIds::CopyQueueValidationColor);
            passBuilder.WriteCopyDestination(DemoResourceIds::CopyQueueAliasCopyScratch);
            passBuilder.WriteToken(DemoResourceIds::CopyQueueValidationFinishedToken);
            passBuilder.WriteToken(DemoResourceIds::CopyQueueAliasCopyReadyToken);
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
            passBuilder.ReadToken(DemoResourceIds::CopyQueueAliasCopyReadyToken);
            passBuilder.ReadTexture(DemoResourceIds::CopyQueueValidationColor);
            passBuilder.WriteUav(DemoResourceIds::CopyQueueValidationComputeColor);
            passBuilder.WriteUav(DemoResourceIds::CopyQueueAliasComputeScratch);
            passBuilder.WriteToken(DemoResourceIds::CopyQueueValidationComputeFinishedToken);
        },
        [](const CopyQueueValidationComputePassData& passData, const RenderContext& context, CommandList& commandList)
        {
            constexpr UINT clearValues[4] = { 0x87654321u, 0u, 0u, 0u };
            commandList.ClearUnorderedAccessUint(
                context.GetResource(DemoResourceIds::CopyQueueAliasComputeScratch),
                clearValues);
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
