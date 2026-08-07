//Modify Begin:2026-08-07 by BestHui
#include <Passes/RaytracingDemoPasses.h>

#include <RenderGraph/RaytracingDemoGraphResources.h>

#include <DX12Library/CommandList.h>
#include <Framework/Rendering/Upscaling/DLSS.h>
#include <RenderGraph/RenderContext.h>
#include <RenderGraph/RenderPass.h>

//Modify Begin:2026-08-07 by BestHui
#include <vector>
//Modify End

std::unique_ptr<RenderGraph::RenderPass> RaytracingDemoPasses::Builder::CreateDLSSPass(
    const RaytracingDemoPassResources& resources,
    const RaytracingDemoPassConfig& config,
    const RenderGraph::ResourceId sceneReadyToken)
{
    using namespace RenderGraph;
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;

    std::vector<Input> inputs = {
        { sceneReadyToken, InputType::Token },
        { DemoResourceIds::SceneColor, InputType::NonPixelShaderResource },
        { DemoResourceIds::DepthBuffer, InputType::NonPixelShaderResource },
        { DemoResourceIds::MotionVector, InputType::NonPixelShaderResource },
    };
    if (config.FrameState->RayReconstructionEnabled)
    {
        inputs.emplace_back(DemoResourceIds::GBufferAlbedoOcclusion, InputType::NonPixelShaderResource);
        inputs.emplace_back(DemoResourceIds::GBufferSpecularSmoothness, InputType::NonPixelShaderResource);
        inputs.emplace_back(DemoResourceIds::DLSSNormalRoughness, InputType::NonPixelShaderResource);
    }

    return RenderPass::Create(
        L"DLSS Super Resolution",
        inputs,
        {
            { DemoResourceIds::DLSSOutput, OutputType::UnorderedAccess },
            { DemoResourceIds::DLSSFinishedToken, OutputType::Token },
        },
        [resources, config](const RenderContext& context, CommandList& commandList)
        {
            const RaytracingDemoFrameState& frameState = *config.FrameState;
            DLSSExecutionInputs inputs{};
            inputs.Color = context.GetTexture(DemoResourceIds::SceneColor);
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
