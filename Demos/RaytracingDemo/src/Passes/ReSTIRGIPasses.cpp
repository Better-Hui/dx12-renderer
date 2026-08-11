#include <Passes/RaytracingDemoPasses.h>

#include <PathTracing/PathTracingSceneBindings.h>
#include <RenderGraph/RaytracingDemoGraphResources.h>

#include <DX12Library/CommandList.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <RenderGraph/RenderContext.h>
#include <RenderGraph/RenderPass.h>

//Modify Begin:2026-08-10 by BestHui
namespace
{
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;
}

std::unique_ptr<RenderGraph::RenderPass> RaytracingDemoPasses::Builder::CreateReSTIRGIPass(
    const RaytracingDemoPassResources& resources,
    const RaytracingDemoPassConfig& config)
{
    using namespace RenderGraph;
    auto pass = RenderPass::Create(
        L"ReSTIR GI",
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
            { DemoResourceIds::IndirectLighting, OutputType::UnorderedAccess },
            { DemoResourceIds::IndirectLightingFinishedToken, OutputType::Token },
        },
        [resources, config](const RenderContext& context, CommandList& commandList)
        {
            const RaytracingDemoRenderGraph::FrameGBufferResources gbuffer =
                RaytracingDemoRenderGraph::GetFrameGBufferResources(context);
            const RaytracingDemoCameraConstants camera =
                RaytracingDemoPassBindings::BuildPassCameraConstants(resources, config, context);

            ReSTIRGIExecutionInputs inputs;
            inputs.FrameState.Enabled = true;
            inputs.FrameState.UseSoftShadowVariant =
                resources.Pipelines.GetShadowMode() == PathTracingShadowMode::SoftShadows;
            inputs.FrameState.EnvironmentProjectionVariant =
                static_cast<uint32_t>(resources.Pipelines.GetLayout().EnvironmentProjection);
            inputs.FrameState.Constants = resources.IndirectLightingReSTIRGI.GetFrameConstants(
                config.FrameState->Width,
                config.FrameState->Height,
                config.FrameState->FrameIndex,
                config.FrameState->ReSTIRGIHistoryValid,
                static_cast<uint32_t>(config.FrameState->MaxBounces));
            inputs.IndirectLighting = context.GetTexture(DemoResourceIds::IndirectLighting);
            inputs.MotionVector = gbuffer.MotionVector;
            inputs.BindSceneInputs = [resources, gbuffer, camera](CommandContext& commandContext, ComputeShader& shader)
            {
                RaytracingDemoPassBindings::BindInlinePathTracingInputs(
                    resources,
                    commandContext,
                    shader,
                    gbuffer,
                    camera);
                resources.Scene.TransitionRayTracingShaderResources(
                    commandContext.GetCommandList(),
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                commandContext.BindBindlessDescriptorHeap(resources.Scene.GetBindlessDescriptorHeap());
            };
            resources.IndirectLightingReSTIRGIPass.Execute(commandList, inputs);
        });
    return pass;
}
//Modify End
