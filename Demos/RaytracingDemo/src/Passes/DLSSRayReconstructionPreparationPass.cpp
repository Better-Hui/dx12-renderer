//Modify Begin:2026-08-07 by Hui
#include <Passes/RaytracingDemoPasses.h>

#include <RenderGraph/RaytracingDemoGraphResources.h>

#include <DX12Library/Helpers.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>
#include <Framework/Rendering/Texture/UnorderedAccessView.h>
#include <RenderGraph/RenderContext.h>
#include <RenderGraph/RenderGraphBuilder.h>

//Modify Begin:2026-08-18 by Hui
namespace
{
    struct DLSSRayReconstructionPreparationPassData
    {
        RaytracingDemoPassResourcesSnapshot Resources;
    };
}
//Modify End

void RaytracingDemoPasses::Builder::AddDLSSRayReconstructionPreparationPass(
    RenderGraph::RenderGraphBuilder& renderGraphBuilder,
    const RaytracingDemoPassResources& resources)
{
    using namespace RenderGraph;
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;

    renderGraphBuilder.AddPass<DLSSRayReconstructionPreparationPassData>(
        L"DLSS Ray Reconstruction Inputs",
        [&resources](RenderGraphPassBuilder& passBuilder, DLSSRayReconstructionPreparationPassData& passData)
        {
            passData.Resources.emplace(resources);
            passBuilder.ReadToken(DemoResourceIds::BaseResourcesFinishedToken);
            passBuilder.ReadBuffer(DemoResourceIds::GBufferNormal);
            passBuilder.ReadBuffer(DemoResourceIds::GBufferSpecularSmoothness);
            passBuilder.ReadBuffer(DemoResourceIds::DepthBuffer);
            passBuilder.WriteUav(DemoResourceIds::DLSSNormalRoughness);
        },
        [](const DLSSRayReconstructionPreparationPassData& passData, const RenderContext& context, CommandList& commandList)
        {
            const RaytracingDemoPassResources& resources = passData.Resources.value();
            ComputeShader& shader = *resources.DLSSRayReconstructionPrepareShader;
            CommandContext commandContext(commandList);
            commandContext.SetTexture(
                shader,
                "GBufferNormal",
                ShaderResourceView(context.GetTexture(DemoResourceIds::GBufferNormal)));
            commandContext.SetTexture(
                shader,
                "GBufferSpecularSmoothness",
                ShaderResourceView(context.GetTexture(DemoResourceIds::GBufferSpecularSmoothness)));
            commandContext.SetTexture(
                shader,
                "DepthBuffer",
                ShaderResourceView::DepthAsFloat(context.GetTexture(DemoResourceIds::DepthBuffer)));
            commandContext.SetUnorderedAccessView(
                shader,
                "NormalRoughness",
                UnorderedAccessView(context.GetTexture(DemoResourceIds::DLSSNormalRoughness)));
            commandContext.BindPipeline(shader);
            commandContext.BindDescriptorSet(shader.GetDescriptorSet());
            commandContext.Dispatch(
                Math::DivideByMultiple(context.GetMetadata().m_ScreenWidth, 8u),
                Math::DivideByMultiple(context.GetMetadata().m_ScreenHeight, 8u),
                1u);
            commandContext.InsertDescriptorSetOutputBarriers(shader.GetDescriptorSet());
        });
}
//Modify End
