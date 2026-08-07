//Modify Begin:2026-07-28 by BestHui
#include <RaytracingDemo.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/CommandQueue.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>
#include <Passes/RaytracingDemoPasses.h>
#include <RenderGraph/RaytracingDemoGraphResources.h>
//Modify Begin:2026-08-07 by BestHui
#include <RenderGraph/ExternalFrameProcessor.h>
//Modify End
#include <RenderGraph/RenderContext.h>
#include <RenderGraph/RenderMetadata.h>
#include <RenderGraph/RenderPass.h>

#include <array>
//Modify Begin:2026-08-07 by BestHui
#include <span>
//Modify End

namespace
{
//Modify Begin:2026-08-07 by BestHui
    class DLSSFrameGenerationProcessor final : public RenderGraph::ExternalFrameProcessor
    {
    public:
        DLSSFrameGenerationProcessor(DLSS& dlss, DLSSFrameGenerationInputs& inputs)
            : m_DLSS(dlss)
            , m_Inputs(inputs)
        {
        }

        [[nodiscard]] std::span<const RenderGraph::ResourceId> GetRequiredResourceIds() const override
        {
            return m_RequiredResourceIds;
        }

        void Process(CommandList& commandList, const std::shared_ptr<Texture>& displayTexture) override
        {
            m_Inputs.HudLessColor = displayTexture;
            m_DLSS.TagFrameGenerationResources(commandList, m_Inputs);
        }

        void BeforePresent() override
        {
            m_DLSS.MarkFrameGenerationRenderSubmissionEnd();
            m_DLSS.MarkFrameGenerationPresentStart();
        }

        void AfterPresent() override
        {
            m_DLSS.MarkFrameGenerationPresentEnd();
        }

    private:
        DLSS& m_DLSS;
        DLSSFrameGenerationInputs& m_Inputs;
        const std::array<RenderGraph::ResourceId, 2> m_RequiredResourceIds = {
            RaytracingDemoRenderGraph::ResourceIds::DepthBuffer,
            RaytracingDemoRenderGraph::ResourceIds::MotionVector,
        };
    };
//Modify End
}

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
        DLSSFrameGenerationProcessor frameProcessor(m_DLSS, m_FrameGenerationInputs);
        m_RenderGraph->PresentWithExternalFrameProcessor(
            PWindow,
            DemoResourceIds::FrameGenerationHudLess,
            frameProcessor,
            [this](CommandList& commandList)
            {
                DrawPostBloomOverlays(commandList);
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
