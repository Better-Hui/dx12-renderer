#include <Passes/RaytracingDemoPasses.h>

#include <PathTracing/PathTracingSceneBindings.h>
#include <RenderGraph/RaytracingDemoGraphResources.h>

#include <DX12Library/CommandList.h>
//Modify Begin:2026-08-11 by Hui
#include <DX12Library/GpuTimestampProfiler.h>
//Modify End
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <RenderGraph/RenderContext.h>
#include <RenderGraph/RenderGraphBuilder.h>

//Modify Begin:2026-08-18 by Hui
namespace
{
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;

    struct ReSTIRGIPassData
    {
        RaytracingDemoPassResourcesSnapshot Resources;
        RaytracingDemoPassConfig Config = {};
    };
}

void RaytracingDemoPasses::Builder::AddReSTIRGIPass(
    RenderGraph::RenderGraphBuilder& renderGraphBuilder,
    const RaytracingDemoPassResources& resources,
    const RaytracingDemoPassConfig& config)
{
    using namespace RenderGraph;
    renderGraphBuilder.AddPass<ReSTIRGIPassData>(
        L"ReSTIR GI",
        [&resources, config](RenderGraphPassBuilder& passBuilder, ReSTIRGIPassData& passData)
        {
            passData.Resources.emplace(resources);
            passData.Config = config;
            passBuilder.ReadToken(DemoResourceIds::BaseResourcesFinishedToken);
            passBuilder.ReadBuffer(DemoResourceIds::GBufferAlbedoOcclusion);
            passBuilder.ReadBuffer(DemoResourceIds::GBufferSpecularSmoothness);
            passBuilder.ReadBuffer(DemoResourceIds::GBufferNormal);
            passBuilder.ReadBuffer(DemoResourceIds::GBufferEmissionMetallic);
            passBuilder.ReadBuffer(DemoResourceIds::GBufferPosition);
            passBuilder.ReadBuffer(DemoResourceIds::MotionVector);
            passBuilder.ReadBuffer(DemoResourceIds::DepthBuffer);
            passBuilder.WriteUav(DemoResourceIds::IndirectLighting);
            passBuilder.WriteToken(DemoResourceIds::IndirectLightingFinishedToken);
            RaytracingDemoPassBindings::DeclareRayTracingExternalResourceAccesses(
                passBuilder,
                resources,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        },
        [](const ReSTIRGIPassData& passData, const RenderContext& context, CommandList& commandList)
        {
            const RaytracingDemoPassResources& resources = passData.Resources.value();
            const RaytracingDemoPassConfig& config = passData.Config;
            const RaytracingDemoRenderGraph::FrameGBufferResources gbuffer =
                RaytracingDemoRenderGraph::GetFrameGBufferResources(context);
            const RaytracingDemoCameraConstants camera =
                RaytracingDemoPassBindings::BuildPassCameraConstants(resources, config, context);

            ReSTIRGIExecutionInputs inputs;
            inputs.FrameState.Enabled = true;
            inputs.FrameState.UseSoftShadowVariant =
                resources.Pipelines.GetShadowMode() == PathTracingShadowMode::SoftShadows;
            inputs.FrameState.ShadingModel = config.FrameState->ShadingModel;
            inputs.FrameState.EnvironmentProjectionVariant =
                static_cast<uint32_t>(resources.Pipelines.GetLayout().EnvironmentProjection);
            inputs.FrameState.VariantConfig = resources.IndirectLightingReSTIRGI.GetVariantConfig(
                static_cast<uint32_t>(config.FrameState->MaxBounces));
            inputs.FrameState.Constants = resources.IndirectLightingReSTIRGI.GetFrameConstants(
                config.FrameState->Width,
                config.FrameState->Height,
                config.FrameState->FrameIndex,
                config.FrameState->ReSTIRGIHistoryValid);
            inputs.IndirectLighting = context.GetTexture(DemoResourceIds::IndirectLighting);
            inputs.MotionVector = gbuffer.MotionVector;
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
}
//Modify End
