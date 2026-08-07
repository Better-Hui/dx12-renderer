//Modify Begin:2026-07-28 by BestHui
#include <RaytracingDemo.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/CommandQueue.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>
#include <Passes/RaytracingDemoPasses.h>
#include <RenderGraph/RaytracingDemoGraphResources.h>
#include <RenderGraph/RenderContext.h>
#include <RenderGraph/RenderMetadata.h>
#include <RenderGraph/RenderPass.h>

#include <array>

std::unique_ptr<RenderGraph::RenderPass> RaytracingDemoPasses::Builder::CreateCudaBloomPass(
    const RaytracingDemoPassResources& resources,
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
        [resources](const RenderGraph::RenderContext& context)
        {
            const auto& sceneColor = context.GetTexture(DemoResourceIds::SceneColor);
            resources.CudaBloom.ExecuteInPlace(
                *sceneColor,
                context.GetMetadata().m_ScreenWidth,
                context.GetMetadata().m_ScreenHeight,
                resources.DirectQueue->GetD3D12CommandQueue().Get());
        });
}

//Modify Begin:2026-08-07 by BestHui
std::unique_ptr<RenderGraph::RenderPass> RaytracingDemoPasses::Builder::CreateFrameGenerationHudLessPass(
    const RaytracingDemoPassResources& resources,
    const RenderGraph::ResourceId sceneColor,
    const RenderGraph::ResourceId sceneReadyToken)
{
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;

    return RenderGraph::RenderPass::Create(
        L"Frame Generation HUD-less Color",
        {
            { sceneReadyToken, RenderGraph::InputType::Token },
            { sceneColor, RenderGraph::InputType::ShaderResource },
        },
        {
            { DemoResourceIds::FrameGenerationHudLess, RenderGraph::OutputType::RenderTarget },
            { DemoResourceIds::FrameGenerationHudLessFinishedToken, RenderGraph::OutputType::Token },
        },
        [resources, sceneColor](const RenderGraph::RenderContext& context, CommandList& commandList)
        {
            CommandContext commandContext(commandList);
            commandContext.SetTexture(
                *resources.DisplayCompositeShader,
                "SceneColor",
                ShaderResourceView(context.GetTexture(sceneColor)));
            commandContext.BindPipeline(*resources.DisplayCompositeShader);
            commandContext.BindDescriptorSet(resources.DisplayCompositeShader->GetDescriptorSet());
            resources.DisplayBlitMesh->Draw(commandList);
        });
}
//Modify End

void RaytracingDemo::PresentDisplayOutput()
{
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;
//Modify Begin:2026-08-07 by BestHui
    if (m_RenderGraphFrameState->FrameGenerationEnabled)
    {
        const std::array frameProcessorResources = {
            DemoResourceIds::DepthBuffer,
            DemoResourceIds::MotionVector,
        };
        m_RenderGraph->PresentWithExternalFrameProcessor(
            PWindow,
            DemoResourceIds::FrameGenerationHudLess,
            frameProcessorResources,
            [this](CommandList& commandList, const std::shared_ptr<Texture>& hudLessColor)
            {
                m_FrameGenerationInputs.HudLessColor = hudLessColor;
                m_DLSS.TagFrameGenerationResources(commandList, m_FrameGenerationInputs);
            },
            [this](CommandList& commandList)
            {
                DrawPostBloomOverlays(commandList);
            },
            [this]()
            {
                m_DLSS.MarkFrameGenerationRenderSubmissionEnd();
                m_DLSS.MarkFrameGenerationPresentStart();
            },
            [this]()
            {
                m_DLSS.MarkFrameGenerationPresentEnd();
            });
        return;
    }

    const RenderGraph::ResourceId displayColor = m_RenderGraphFrameState->DLSSEnabled
        ? DemoResourceIds::DLSSOutput
        : DemoResourceIds::SceneColor;
//Modify End

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
