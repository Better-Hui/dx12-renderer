//Modify Begin:2026-07-28 by Hui
#include <RaytracingDemo.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/CommandQueue.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>
#include <Passes/RaytracingDemoPasses.h>
#include <RenderGraph/RaytracingDemoGraphResources.h>
//Modify Begin:2026-08-07 by Hui
#include <RenderGraph/ExternalFrameProcessor.h>
//Modify End
#include <RenderGraph/RenderContext.h>
#include <RenderGraph/RenderGraphBuilder.h>
#include <RenderGraph/RenderMetadata.h>

#include <array>
//Modify Begin:2026-08-07 by Hui
#include <span>
//Modify End

namespace
{
//Modify Begin:2026-08-18 by Hui
    struct CudaBloomPassData
    {
        RaytracingDemoPassResourcesSnapshot Resources;
    };

    struct FrameworkBloomPassData
    {
        RaytracingDemoPassResourcesSnapshot Resources;
    };

    struct FrameGenerationHudLessPassData
    {
        RaytracingDemoPassResourcesSnapshot Resources;
        RenderGraph::ResourceId SceneColor = 0;
    };
//Modify End

//Modify Begin:2026-08-07 by Hui
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

void RaytracingDemoPasses::Builder::AddCudaBloomPass(
    RenderGraph::RenderGraphBuilder& renderGraphBuilder,
    const RaytracingDemoPassResources& resources,
    const RenderGraph::ResourceId sceneReadyToken)
{
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;

    renderGraphBuilder.AddExternalPass<CudaBloomPassData>(
        L"CUDA Bloom",
        [&resources, sceneReadyToken](RenderGraph::RenderGraphPassBuilder& passBuilder, CudaBloomPassData& passData)
        {
            passData.Resources.emplace(resources);
            passBuilder.ReadToken(sceneReadyToken);
            passBuilder.WriteExternal(DemoResourceIds::SceneColor);
            passBuilder.WriteToken(DemoResourceIds::CudaBloomFinishedToken);
        },
        [](const CudaBloomPassData& passData, const RenderGraph::RenderContext& context)
        {
            const RaytracingDemoPassResources& resources = passData.Resources.value();
            const auto& sceneColor = context.GetTexture(DemoResourceIds::SceneColor);
            resources.CudaBloom.ExecuteInPlace(
                *sceneColor,
                context.GetMetadata().m_ScreenWidth,
                context.GetMetadata().m_ScreenHeight,
                resources.DirectQueue->GetD3D12CommandQueue().Get());
        });
}

//Modify Begin:2026-08-16 by Hui
void RaytracingDemoPasses::Builder::AddFrameworkBloomPass(
    RenderGraph::RenderGraphBuilder& renderGraphBuilder,
    const RaytracingDemoPassResources& resources,
    const RenderGraph::ResourceId sceneReadyToken)
{
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;

    renderGraphBuilder.AddPass<FrameworkBloomPassData>(
        L"Built-in Raster Bloom",
        [&resources, sceneReadyToken](RenderGraph::RenderGraphPassBuilder& passBuilder, FrameworkBloomPassData& passData)
        {
            passData.Resources.emplace(resources);
            passBuilder.ReadToken(sceneReadyToken);
            passBuilder.ReadTexture(DemoResourceIds::SceneColor);
            passBuilder.WriteTexture(DemoResourceIds::BloomOutput);
            passBuilder.WriteToken(DemoResourceIds::CudaBloomFinishedToken);
        },
        [](const FrameworkBloomPassData& passData, const RenderGraph::RenderContext& context, CommandList& commandList)
        {
            const RaytracingDemoPassResources& resources = passData.Resources.value();
            const auto& sceneColor = context.GetTexture(DemoResourceIds::SceneColor);
            const auto& bloomOutput = context.GetTexture(DemoResourceIds::BloomOutput);
            resources.CudaBloom.ExecuteFrameworkBloom(
                sceneColor,
                bloomOutput,
                commandList,
                context.GetMetadata().m_ScreenWidth,
                context.GetMetadata().m_ScreenHeight);
        });
}
//Modify End

//Modify Begin:2026-08-07 by Hui
void RaytracingDemoPasses::Builder::AddFrameGenerationHudLessPass(
    RenderGraph::RenderGraphBuilder& renderGraphBuilder,
    const RaytracingDemoPassResources& resources,
    const RenderGraph::ResourceId sceneColor,
    const RenderGraph::ResourceId sceneReadyToken)
{
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;

    renderGraphBuilder.AddPass<FrameGenerationHudLessPassData>(
        L"Frame Generation HUD-less Color",
        [&resources, sceneColor, sceneReadyToken](RenderGraph::RenderGraphPassBuilder& passBuilder, FrameGenerationHudLessPassData& passData)
        {
            passData.Resources.emplace(resources);
            passData.SceneColor = sceneColor;
            passBuilder.ReadToken(sceneReadyToken);
            passBuilder.ReadTexture(sceneColor);
            passBuilder.WriteTexture(DemoResourceIds::FrameGenerationHudLess);
            passBuilder.WriteToken(DemoResourceIds::FrameGenerationHudLessFinishedToken);
        },
        [](const FrameGenerationHudLessPassData& passData, const RenderGraph::RenderContext& context, CommandList& commandList)
        {
            const RaytracingDemoPassResources& resources = passData.Resources.value();
            CommandContext commandContext(commandList);
            commandContext.SetTexture(
                *resources.DisplayCompositeShader,
                "SceneColor",
                ShaderResourceView(context.GetTexture(passData.SceneColor)));
            commandContext.BindPipeline(*resources.DisplayCompositeShader);
            commandContext.BindDescriptorSet(resources.DisplayCompositeShader->GetDescriptorSet());
            resources.DisplayBlitMesh->Draw(commandList);
        });
}
//Modify End

void RaytracingDemo::PresentDisplayOutput()
{
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;
    RenderGraph::RenderGraphRoot& renderGraph = m_RenderPipeline.GetRenderGraph();
//Modify Begin:2026-08-07 by Hui
    if (m_RenderGraphFrameState->FrameGenerationEnabled)
    {
        DLSSFrameGenerationProcessor frameProcessor(m_DLSS, m_FrameGenerationInputs);
        renderGraph.PresentWithExternalFrameProcessor(
            PWindow,
            renderGraph.GetPresentationResourceId(),
            frameProcessor,
            [this](CommandList& commandList)
            {
                DrawPostBloomOverlays(commandList);
            });
        return;
    }

    const RenderGraph::ResourceId displayColor = renderGraph.GetPresentationResourceId();
//Modify End

//Modify Begin:2026-07-28 by Hui
    renderGraph.PresentWithOverlayBlit(
        PWindow,
        displayColor,
        [this](CommandList& cmd, const std::shared_ptr<Texture>& sourceTexture)
        {
//Modify Begin:2026-07-29 by Hui
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
