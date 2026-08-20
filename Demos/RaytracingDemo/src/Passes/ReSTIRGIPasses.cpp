#include <Passes/RaytracingDemoPasses.h>

#include <PathTracing/PathTracingSceneBindings.h>
#include <RenderGraph/RaytracingDemoGraphResources.h>

#include <DX12Library/CommandList.h>
//Modify Begin:2026-08-19 by Hui
#include <DX12Library/ByteAddressBuffer.h>
#include <DX12Library/GpuTimestampProfiler.h>
#include <DX12Library/StructuredBuffer.h>
#include <Framework/Rendering/Lighting/ActivePixelListController.h>
//Modify End
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <RenderGraph/RenderContext.h>
#include <RenderGraph/RenderGraphBuilder.h>

//Modify Begin:2026-08-20 by Hui
namespace
{
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;

    struct ReSTIRGIPassData
    {
        RaytracingDemoPassResourcesSnapshot Resources;
        RaytracingDemoPassConfig Config = {};
        bool UseCompactedDispatch = false;
    };
}

void RaytracingDemoPasses::Builder::AddReSTIRGIPass(
    RenderGraph::RenderGraphBuilder& renderGraphBuilder,
    const RaytracingDemoPassResources& resources,
    const RaytracingDemoPassConfig& config)
{
    using namespace RenderGraph;
    const bool useCompactedDispatch = config.FrameState->UsesCompactedRayTracedPixelDispatch();
    renderGraphBuilder.AddPass<ReSTIRGIPassData>(
        L"ReSTIR GI",
        [&resources, config, useCompactedDispatch](RenderGraphPassBuilder& passDataBuilder, ReSTIRGIPassData& passData)
        {
            passData.Resources.emplace(resources);
            passData.Config = config;
            passData.UseCompactedDispatch = useCompactedDispatch;
            passDataBuilder.ReadToken(DemoResourceIds::BaseResourcesFinishedToken);
            passDataBuilder.ReadBuffer(DemoResourceIds::GBufferAlbedoOcclusion);
            passDataBuilder.ReadBuffer(DemoResourceIds::GBufferSpecularSmoothness);
            passDataBuilder.ReadBuffer(DemoResourceIds::GBufferNormal);
            passDataBuilder.ReadBuffer(DemoResourceIds::GBufferEmissionMetallic);
            passDataBuilder.ReadBuffer(DemoResourceIds::GBufferPosition);
            passDataBuilder.ReadBuffer(DemoResourceIds::MotionVector);
            passDataBuilder.ReadBuffer(DemoResourceIds::DepthBuffer);
            if (useCompactedDispatch)
            {
                passDataBuilder.ReadToken(DemoResourceIds::ActivePixelComputeDispatchReadyToken);
                passDataBuilder.ReadIndirectArgument(DemoResourceIds::ActivePixelDispatchData);
                passDataBuilder.ReadBuffer(DemoResourceIds::ActiveRayPixelIndices);
                passDataBuilder.ReadBuffer(DemoResourceIds::ActiveRayPixelCount);
            }
            passDataBuilder.WriteUav(DemoResourceIds::IndirectLighting);
            passDataBuilder.WriteToken(DemoResourceIds::IndirectLightingFinishedToken);
            RaytracingDemoPassBindings::DeclareRayTracingExternalResourceAccesses(
                passDataBuilder,
                resources,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        },
        [](const ReSTIRGIPassData& passData, const RenderContext& context, CommandList& commandList)
        {
            const RaytracingDemoPassResources& resources = passData.Resources.value();
            const RaytracingDemoPassConfig& config = passData.Config;
            const bool useCompactedDispatch = passData.UseCompactedDispatch;
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
            if (useCompactedDispatch)
            {
                const std::shared_ptr<StructuredBuffer> activePixelIndices =
                    std::dynamic_pointer_cast<StructuredBuffer>(context.GetBuffer(DemoResourceIds::ActiveRayPixelIndices));
                const std::shared_ptr<ByteAddressBuffer> activePixelCount =
                    std::dynamic_pointer_cast<ByteAddressBuffer>(context.GetBuffer(DemoResourceIds::ActiveRayPixelCount));
                const std::shared_ptr<ByteAddressBuffer> activePixelDispatchData =
                    std::dynamic_pointer_cast<ByteAddressBuffer>(context.GetBuffer(DemoResourceIds::ActivePixelDispatchData));
                Assert(activePixelIndices != nullptr && activePixelCount != nullptr && activePixelDispatchData != nullptr,
                    "Compacted ReSTIR GI requires active-pixel index, count, and dispatch-data buffers.");
                inputs.CompactedDispatch = resources.ActivePixels.GetComputeDispatch(
                    *activePixelIndices,
                    *activePixelCount,
                    *activePixelDispatchData);
            }
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
