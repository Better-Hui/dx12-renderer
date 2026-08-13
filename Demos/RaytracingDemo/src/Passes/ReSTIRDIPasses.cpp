#include <Passes/RaytracingDemoPasses.h>

//Modify Begin:2026-07-30 by BestHui
#include <PathTracing/PathTracingSceneBindings.h>
#include <RenderGraph/RaytracingDemoGraphResources.h>

#include <DX12Library/CommandList.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <RenderGraph/RenderContext.h>
#include <RenderGraph/RenderPass.h>

namespace
{
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;
}

std::unique_ptr<RenderGraph::RenderPass> RaytracingDemoPasses::Builder::CreateReSTIRDIPass(
    const RaytracingDemoPassResources& resources,
    const RaytracingDemoPassConfig& config)
{
    using namespace RenderGraph;
    return RenderPass::Create(
        L"ReSTIR DI",
        {
            { DemoResourceIds::BaseResourcesFinishedToken, InputType::Token },
            { DemoResourceIds::GBufferAlbedoOcclusion, InputType::NonPixelShaderResource },
            { DemoResourceIds::GBufferSpecularSmoothness, InputType::NonPixelShaderResource },
            { DemoResourceIds::GBufferNormal, InputType::NonPixelShaderResource },
            { DemoResourceIds::GBufferEmissionMetallic, InputType::NonPixelShaderResource },
            { DemoResourceIds::GBufferPosition, InputType::NonPixelShaderResource },
            { DemoResourceIds::MotionVector, InputType::NonPixelShaderResource },
            { DemoResourceIds::DepthBuffer, InputType::NonPixelShaderResource },
        },
        {
            { DemoResourceIds::DirectLighting, OutputType::UnorderedAccess },
//Modify Begin:2026-08-06 by BestHui
            { DemoResourceIds::DirectLightingFinishedToken, OutputType::Token },
//Modify End
        },
        [resources, config](const RenderContext& context, CommandList& commandList)
        {
            const RaytracingDemoRenderGraph::FrameGBufferResources gbuffer =
                RaytracingDemoRenderGraph::GetFrameGBufferResources(context);
            const RaytracingDemoCameraConstants camera =
                RaytracingDemoPassBindings::BuildPassCameraConstants(resources, config, context);

            ReSTIRDIExecutionInputs inputs;
            inputs.FrameState.Enabled = true;
            inputs.FrameState.UseSoftShadowVariant =
                resources.Pipelines.GetShadowMode() == PathTracingShadowMode::SoftShadows;
//Modify Begin:2026-07-30 by BestHui
            inputs.FrameState.ShadingModel = config.FrameState->ShadingModel;
//Modify End
//Modify Begin:2026-08-06 by BestHui
            inputs.FrameState.EnvironmentProjectionVariant =
                static_cast<uint32_t>(resources.Pipelines.GetLayout().EnvironmentProjection);
//Modify End
            inputs.FrameState.Width = config.FrameState->Width;
            inputs.FrameState.Height = config.FrameState->Height;
            inputs.FrameState.FrameIndex = config.FrameState->FrameIndex;
            inputs.FrameState.Constants = resources.DirectLightingReSTIRDI.GetFrameConstants(
                config.FrameState->ReSTIRDIHistoryValid);
            inputs.DirectLighting = context.GetTexture(DemoResourceIds::DirectLighting);
            inputs.MotionVector = gbuffer.MotionVector;
            inputs.PrepareCommandContext = [&resources](CommandContext& commandContext)
            {
                resources.Scene.TransitionRayTracingShaderResources(
                    commandContext.GetCommandList(),
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                commandContext.BindBindlessDescriptorHeap(resources.Scene.GetBindlessDescriptorHeap());
            };
            inputs.BindSceneInputs = [resources, gbuffer, camera](CommandContext& commandContext, ComputeShader& shader)
            {
                RaytracingDemoPassBindings::BindInlinePathTracingInputs(
                    resources,
                    commandContext,
                    shader,
                    gbuffer,
                    camera);
            };
            resources.DirectLightingReSTIRDIPass.Execute(commandList, inputs);
        });
}
//Modify End
