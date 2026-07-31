//Modify Begin:2026-07-28 by BestHui
#include <RaytracingDemo.h>

#include <DX12Library/Application.h>
#include <DX12Library/CommandList.h>
#include <DX12Library/CommandQueue.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>
#include <Passes/RaytracingDemoPasses.h>
#include <RenderGraph/RaytracingDemoGraphResources.h>
#include <RenderGraph/RenderContext.h>
#include <RenderGraph/RenderMetadata.h>
#include <RenderGraph/RenderPass.h>

std::unique_ptr<RenderGraph::RenderPass> RaytracingDemoPasses::Builder::CreateCudaBloomPass(
    RaytracingDemo& demo,
    const RenderGraph::ResourceId sceneReadyToken)
{
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;

    return RenderGraph::RenderPass::CreateExternal(
        L"CUDA Bloom",
        {
            { sceneReadyToken, RenderGraph::InputType::Token },
        },
        {
            { DemoResourceIds::SceneColor, RenderGraph::OutputType::ExternalAccess },
            { DemoResourceIds::CudaBloomFinishedToken, RenderGraph::OutputType::Token },
        },
        [&demo](const RenderGraph::RenderContext& context)
        {
            const auto& sceneColor = context.m_ResourcePool->GetTexture(DemoResourceIds::SceneColor);
            demo.m_CudaBloom.ExecuteInPlace(
                *sceneColor,
                context.m_Metadata.m_ScreenWidth,
                context.m_Metadata.m_ScreenHeight,
                Application::Get().GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT)->GetD3D12CommandQueue().Get());
        });
}

void RaytracingDemo::PresentDisplayOutput()
{
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;
    RenderGraph::ResourceId displayColor = DemoResourceIds::SceneColor;

//Modify Begin:2026-07-28 by BestHui
    m_RenderGraph->PresentWithOverlayBlit(
        PWindow,
        displayColor,
        [this](CommandList& cmd, const std::shared_ptr<Texture>& sourceTexture)
        {
//Modify Begin:2026-07-29 by BestHui
            CommandContext commandContext(cmd);
            commandContext.SetTexture(*m_DisplayCompositeShader, "SceneColor", ShaderResourceView(sourceTexture));
            commandContext.BindPipeline(*m_DisplayCompositeShader);
            commandContext.BindDescriptorSet(m_DisplayCompositeShader->GetDescriptorSet());
//Modify End
            m_DisplayBlitMesh->Draw(cmd);
        },
        [this](CommandList& cmd)
        {
            DrawPostBloomOverlays(cmd);
        });
//Modify End
}
//Modify End
