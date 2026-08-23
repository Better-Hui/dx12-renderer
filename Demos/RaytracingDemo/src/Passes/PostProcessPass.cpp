//Modify Begin:2026-08-18 by Hui
#include <RaytracingDemo.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/CommandQueue.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>
#include <Passes/RaytracingDemoPasses.h>
#include <RenderGraph/RaytracingDemoGraphResources.h>
#include <RenderGraph/ExternalFrameProcessor.h>
#include <RenderGraph/RenderContext.h>
#include <RenderGraph/RenderGraphBuilder.h>
#include <RenderGraph/RenderMetadata.h>

#include <array>
#include <span>

namespace
{
    struct CudaBloomPassData
    {
        RaytracingDemoPassResourcesSnapshot Resources;
    };

    struct FrameworkBloomPassData
    {
        RaytracingDemoPassResourcesSnapshot Resources;
    };

    struct AutoExposurePassData
    {
        RaytracingDemoPassResourcesSnapshot Resources;
        RenderGraph::ResourceId InputColor = 0;
    };

    struct FrameGenerationHudLessPassData
    {
        RaytracingDemoPassResourcesSnapshot Resources;
        RenderGraph::ResourceId SceneColor = 0;
    };

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

void RaytracingDemoPasses::Builder::AddAutoExposurePass(
    RenderGraph::RenderGraphBuilder& renderGraphBuilder,
    const RaytracingDemoPassResources& resources,
    const RenderGraph::ResourceId inputColor,
    const RenderGraph::ResourceId sceneReadyToken)
{
    using namespace RenderGraph;
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;

    renderGraphBuilder.AddPass<AutoExposurePassData>(
        L"Auto Exposure",
        [&resources, inputColor, sceneReadyToken](RenderGraphPassBuilder& passBuilder, AutoExposurePassData& passData)
        {
            passData.Resources.emplace(resources);
            passData.InputColor = inputColor;
            passBuilder.ReadToken(sceneReadyToken);
            passBuilder.ReadBuffer(inputColor);
            passBuilder.WriteUav(DemoResourceIds::AutoExposureOutput);
            passBuilder.WriteToken(DemoResourceIds::AutoExposureFinishedToken);
        },
        [](const AutoExposurePassData& passData, const RenderContext& context, CommandList& commandList)
        {
            const RaytracingDemoPassResources& resources = passData.Resources.value();
            const std::shared_ptr<Texture>& input = context.GetTexture(passData.InputColor);
            const std::shared_ptr<Texture>& output = context.GetTexture(RaytracingDemoRenderGraph::ResourceIds::AutoExposureOutput);
            const D3D12_RESOURCE_DESC inputDesc = input->GetD3D12ResourceDesc();
            const D3D12_RESOURCE_DESC outputDesc = output->GetD3D12ResourceDesc();
            resources.Exposure.Execute(
                commandList,
                input,
                output,
                static_cast<uint32_t>(inputDesc.Width),
                inputDesc.Height,
                static_cast<uint32_t>(outputDesc.Width),
                outputDesc.Height,
                context.GetMetadata().m_DeltaTime);
        });
}

void RaytracingDemo::PresentDisplayOutput()
{
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;
    RenderGraph::RenderGraphRoot& renderGraph = m_RenderPipeline.GetRenderGraph();
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

    renderGraph.PresentWithOverlay(
        PWindow,
        renderGraph.GetPresentationResourceId(),
        [this](CommandList& cmd)
        {
            DrawPostBloomOverlays(cmd);
        });
}
//Modify End
