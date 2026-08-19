//Modify Begin:2026-08-18 by Hui
#include <Passes/RaytracingDemoPasses.h>

#include <RenderGraph/RaytracingDemoGraphResources.h>

#include <DX12Library/CommandList.h>
#include <Framework/Rendering/Upscaling/DLSS.h>
#include <RenderGraph/RenderContext.h>
#include <RenderGraph/RenderGraphBuilder.h>

namespace
{
    struct DLSSPassData
    {
        RaytracingDemoPassResourcesSnapshot Resources;
        RaytracingDemoPassConfig Config = {};
        RenderGraph::ResourceId InputColor = 0;
    };
}

void RaytracingDemoPasses::Builder::AddDLSSPass(
    RenderGraph::RenderGraphBuilder& renderGraphBuilder,
    const RaytracingDemoPassResources& resources,
    const RaytracingDemoPassConfig& config,
    const RenderGraph::ResourceId inputColor,
    const RenderGraph::ResourceId sceneReadyToken)
{
    using namespace RenderGraph;
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;

    renderGraphBuilder.AddPass<DLSSPassData>(
        L"DLSS Super Resolution",
        [&resources, config, inputColor, sceneReadyToken](RenderGraphPassBuilder& passBuilder, DLSSPassData& passData)
        {
            passData.Resources.emplace(resources);
            passData.Config = config;
            passData.InputColor = inputColor;
            passBuilder.ReadToken(sceneReadyToken);
            passBuilder.ReadBuffer(inputColor);
            passBuilder.ReadBuffer(DemoResourceIds::DepthBuffer);
            passBuilder.ReadBuffer(DemoResourceIds::MotionVector);
            if (config.FrameState->RayReconstructionEnabled)
            {
                passBuilder.ReadBuffer(DemoResourceIds::GBufferAlbedoOcclusion);
                passBuilder.ReadBuffer(DemoResourceIds::GBufferSpecularSmoothness);
                passBuilder.ReadBuffer(DemoResourceIds::DLSSNormalRoughness);
            }
            passBuilder.WriteUav(DemoResourceIds::DLSSOutput);
            passBuilder.WriteToken(DemoResourceIds::DLSSFinishedToken);
        },
        [](const DLSSPassData& passData, const RenderContext& context, CommandList& commandList)
        {
            const RaytracingDemoPassResources& resources = passData.Resources.value();
            const RaytracingDemoPassConfig& config = passData.Config;
            const RaytracingDemoFrameState& frameState = *config.FrameState;
            DLSSExecutionInputs inputs{};
            inputs.Color = context.GetTexture(passData.InputColor);
            inputs.Depth = context.GetTexture(DemoResourceIds::DepthBuffer);
            inputs.MotionVectors = context.GetTexture(DemoResourceIds::MotionVector);
            inputs.Output = context.GetTexture(DemoResourceIds::DLSSOutput);
            if (frameState.RayReconstructionEnabled)
            {
                inputs.DiffuseAlbedo = context.GetTexture(DemoResourceIds::GBufferAlbedoOcclusion);
                inputs.SpecularAlbedo = context.GetTexture(DemoResourceIds::GBufferSpecularSmoothness);
                inputs.NormalRoughness = context.GetTexture(DemoResourceIds::DLSSNormalRoughness);
            }
            inputs.RenderWidth = frameState.Width;
            inputs.RenderHeight = frameState.Height;
            inputs.DisplayWidth = frameState.DisplayWidth;
            inputs.DisplayHeight = frameState.DisplayHeight;
            inputs.JitterOffset = frameState.DLSSJitterOffset;
            inputs.Sharpness = frameState.DLSSSharpness;
            inputs.Reset = !frameState.HasPreviousViewProjection;
            inputs.FrameIndex = frameState.FrameIndex;
            inputs.HasPreviousViewProjection = frameState.HasPreviousViewProjection;
            inputs.View = frameState.View;
            inputs.Projection = frameState.Projection;
            inputs.ViewProjection = frameState.ViewProjection;
            inputs.PreviousViewProjection = frameState.PreviousViewProjection;
            resources.Dlss.Execute(commandList, inputs);
        });
}
//Modify End
