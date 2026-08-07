//Modify Begin:2026-08-07 by BestHui
#include <Passes/RaytracingDemoPasses.h>

#include <RenderGraph/RaytracingDemoGraphResources.h>

#include <DX12Library/Helpers.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>
#include <Framework/Rendering/Texture/UnorderedAccessView.h>
#include <RenderGraph/RenderContext.h>
#include <RenderGraph/RenderPass.h>

std::unique_ptr<RenderGraph::RenderPass> RaytracingDemoPasses::Builder::CreateDLSSRayReconstructionPreparationPass(
    const RaytracingDemoPassResources& resources)
{
    using namespace RenderGraph;
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;

    return RenderPass::Create(
        L"DLSS Ray Reconstruction Inputs",
        {
            { DemoResourceIds::BaseResourcesFinishedToken, InputType::Token },
            { DemoResourceIds::GBufferNormal, InputType::NonPixelShaderResource },
            { DemoResourceIds::GBufferSpecularSmoothness, InputType::NonPixelShaderResource },
            { DemoResourceIds::DepthBuffer, InputType::NonPixelShaderResource },
        },
        {
            { DemoResourceIds::DLSSNormalRoughness, OutputType::UnorderedAccess },
        },
        [resources](const RenderContext& context, CommandList& commandList)
        {
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
