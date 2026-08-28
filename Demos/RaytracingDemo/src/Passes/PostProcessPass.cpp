//Modify Begin:2026-08-28 by Hui
#include <RaytracingDemo.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/CommandQueue.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/PostProcess/Bloom.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>
#include <Passes/RaytracingDemoPasses.h>
#include <RenderGraph/RaytracingDemoGraphResources.h>
#include <RenderGraph/ExternalFrameProcessor.h>
#include <RenderGraph/RenderContext.h>
#include <RenderGraph/RenderGraphBuilder.h>
#include <RenderGraph/RenderMetadata.h>

#include <algorithm>
#include <array>
#include <span>
#include <utility>

namespace
{
    struct CudaBloomPassData
    {
        RaytracingDemoPassResourcesSnapshot Resources;
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
            passBuilder.WriteToken(DemoResourceIds::BloomFinishedToken);
        },
        [](const CudaBloomPassData& passData, const RenderGraph::RenderContext& context)
        {
            const RaytracingDemoPassResources& resources = passData.Resources.value();
            const auto& sceneColor = context.GetTexture(DemoResourceIds::SceneColor);
            resources.Bloom.ExecuteInPlace(
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

    Bloom::GraphInputs inputs;
    inputs.Source = DemoResourceIds::SceneColor;
    inputs.Output = DemoResourceIds::BloomOutput;
    inputs.InputToken = sceneReadyToken;
    inputs.OutputToken = DemoResourceIds::BloomFinishedToken;
    inputs.WidthExpression = [](const RenderGraph::RenderMetadata& metadata) { return metadata.m_ScreenWidth; };
    inputs.HeightExpression = [](const RenderGraph::RenderMetadata& metadata) { return metadata.m_ScreenHeight; };
    inputs.ResolveParameters = [bloomController = &resources.Bloom]()
    {
        const BloomController::Settings settings = bloomController->GetSettings();
        BloomParameters parameters{};
        parameters.Intensity = settings.Intensity;
        parameters.Threshold = settings.Threshold;
        parameters.SoftThreshold = settings.SoftThreshold;
        return parameters;
    };
    inputs.DiagnosticNamePrefix = L"Framework.Bloom.RaytracingDemo";
    inputs.Format = RaytracingDemoRenderGraph::SCENE_COLOR_FORMAT;
    inputs.PyramidLevels = static_cast<size_t>((std::max)(1, resources.Bloom.GetPyramidLevels()));
    resources.Bloom.GetFrameworkBloom().AddPasses(renderGraphBuilder, std::move(inputs));
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

    AutoExposure::GraphInputs inputs;
    inputs.Source = inputColor;
    inputs.Output = DemoResourceIds::AutoExposureOutput;
    inputs.InputToken = sceneReadyToken;
    inputs.OutputToken = DemoResourceIds::AutoExposureFinishedToken;
    inputs.OutputWidth = 1u;
    inputs.OutputHeight = 1u;
    inputs.ResolveFrameInputs = [inputColor](const RenderContext& context)
    {
        AutoExposure::FrameInputs frameInputs;
        frameInputs.Source = context.GetTexture(inputColor);
        frameInputs.Output = context.GetTexture(DemoResourceIds::AutoExposureOutput);
        const D3D12_RESOURCE_DESC inputDesc = frameInputs.Source->GetD3D12ResourceDesc();
        const D3D12_RESOURCE_DESC outputDesc = frameInputs.Output->GetD3D12ResourceDesc();
        frameInputs.InputWidth = static_cast<uint32_t>(inputDesc.Width);
        frameInputs.InputHeight = inputDesc.Height;
        frameInputs.OutputWidth = static_cast<uint32_t>(outputDesc.Width);
        frameInputs.OutputHeight = outputDesc.Height;
        frameInputs.DeltaTime = context.GetMetadata().m_DeltaTime;
        return frameInputs;
    };
    resources.Exposure.AddPasses(renderGraphBuilder, std::move(inputs));
}

void RaytracingDemo::PresentDisplayOutput()
{
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;
    RenderGraph::RenderGraphRoot& renderGraph = m_RenderPipeline.GetRenderGraph();
    if (m_Hdr10OutputEnabled)
    {
        const std::shared_ptr<Shader>& hdr10PresentationShader =
            m_ShaderPipelineBootstrap.GetHdr10PresentationShader();
        Assert(hdr10PresentationShader != nullptr, "HDR10 presentation shader is not initialized.");
        const Hdr10PresentationConstants constants = {
            .PeakNits = m_Hdr10PeakNits,
        };
        renderGraph.PresentWithOverlayBlit(
            PWindow,
            renderGraph.GetPresentationResourceId(),
            [this, hdr10PresentationShader, constants](
                CommandList& commandList,
                const std::shared_ptr<Texture>& source)
            {
                CommandContext commandContext(commandList);
                commandContext.SetTexture(
                    *hdr10PresentationShader,
                    "SceneColor",
                    ShaderResourceView(source));
                commandContext.SetConstantBuffer(
                    *hdr10PresentationShader,
                    "HDR10PresentationConstants",
                    constants);
                commandContext.BindPipeline(*hdr10PresentationShader);
                commandContext.BindDescriptorSet(hdr10PresentationShader->GetDescriptorSet());
                m_DisplayBlitMesh->Draw(commandList);
            },
            [this](CommandList& commandList)
            {
                DrawPostBloomOverlays(commandList);
            });
        return;
    }
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
