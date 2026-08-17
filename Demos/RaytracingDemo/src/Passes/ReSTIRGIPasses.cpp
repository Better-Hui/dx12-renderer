#include <Passes/RaytracingDemoPasses.h>

#include <PathTracing/PathTracingSceneBindings.h>
#include <RenderGraph/RaytracingDemoGraphResources.h>

#include <DX12Library/CommandList.h>
//Modify Begin:2026-08-11 by Hui
#include <DX12Library/GpuTimestampProfiler.h>
//Modify End
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <RenderGraph/RenderContext.h>
#include <RenderGraph/RenderPass.h>

//Modify Begin:2026-08-10 by Hui
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
//Modify Begin:2026-07-30 by Hui
            inputs.FrameState.ShadingModel = config.FrameState->ShadingModel;
//Modify End
            inputs.FrameState.EnvironmentProjectionVariant =
                static_cast<uint32_t>(resources.Pipelines.GetLayout().EnvironmentProjection);
//Modify Begin:2026-08-11 by Hui
            inputs.FrameState.VariantConfig = resources.IndirectLightingReSTIRGI.GetVariantConfig(
                static_cast<uint32_t>(config.FrameState->MaxBounces));
//Modify End
            inputs.FrameState.Constants = resources.IndirectLightingReSTIRGI.GetFrameConstants(
                config.FrameState->Width,
                config.FrameState->Height,
                config.FrameState->FrameIndex,
                config.FrameState->ReSTIRGIHistoryValid);
            inputs.IndirectLighting = context.GetTexture(DemoResourceIds::IndirectLighting);
            inputs.MotionVector = gbuffer.MotionVector;
//Modify Begin:2026-08-11 by Hui
            inputs.EnableStageTiming = config.FrameState->ReSTIRGIStageTimingEnabled;
            inputs.WriteTimestamp = [profiler = resources.DirectGpuTimestampProfiler](
                CommandList& timestampCommandList,
                const char* markerName)
            {
                if (profiler != nullptr)
                {
                    profiler->WriteTimestamp(timestampCommandList, markerName);
                }
            };
            inputs.PrepareCommandContext = [&resources](CommandContext& commandContext)
            {
                commandContext.BindBindlessDescriptorHeap(resources.Scene.GetBindlessDescriptorHeap());
            };
//Modify End
            inputs.BindSceneInputs = [resources, gbuffer, camera](CommandContext& commandContext, ComputeShader& shader)
            {
                RaytracingDemoPassBindings::BindInlinePathTracingInputs(
                    resources,
                    commandContext,
                    shader,
                    gbuffer,
                    camera);
            };
            resources.IndirectLightingReSTIRGIPass.Execute(commandList, inputs);
        });
//Modify Begin:2026-08-13 by Hui
    RaytracingDemoPassBindings::DeclareRayTracingExternalResourceAccesses(
        *pass,
        resources,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
//Modify End
    return pass;
}
//Modify End
