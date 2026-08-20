#include <Passes/RaytracingDemoPasses.h>

//Modify Begin:2026-08-20 by Hui
#include <PathTracing/PathTracingSceneBindings.h>
#include <RenderGraph/RaytracingDemoGraphResources.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/ByteAddressBuffer.h>
#include <DX12Library/StructuredBuffer.h>
#include <Framework/Rendering/Lighting/ActivePixelListController.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <RenderGraph/RenderContext.h>
#include <RenderGraph/RenderGraphBuilder.h>

namespace
{
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;

    struct ReSTIRDIPassData
    {
        RaytracingDemoPassResourcesSnapshot Resources;
        RaytracingDemoPassConfig Config = {};
        bool UseCompactedDispatch = false;
    };
}

void RaytracingDemoPasses::Builder::AddReSTIRDIPass(
    RenderGraph::RenderGraphBuilder& renderGraphBuilder,
    const RaytracingDemoPassResources& resources,
    const RaytracingDemoPassConfig& config)
{
    using namespace RenderGraph;
    const ReSTIRDIFrameConstants restirConstants = resources.DirectLightingReSTIRDI.GetFrameConstants(
        config.FrameState->ReSTIRDIHistoryValid);
    const bool useCompactedDispatch = config.FrameState->UsesCompactedRayTracedPixelDispatch();
    renderGraphBuilder.AddPass<ReSTIRDIPassData>(
        L"ReSTIR DI",
        [&resources, config, useCompactedDispatch](RenderGraphPassBuilder& passBuilder, ReSTIRDIPassData& passData)
        {
            passData.Resources.emplace(resources);
            passData.Config = config;
            passData.UseCompactedDispatch = useCompactedDispatch;
            passBuilder.ReadToken(DemoResourceIds::BaseResourcesFinishedToken);
            passBuilder.ReadBuffer(DemoResourceIds::GBufferAlbedoOcclusion);
            passBuilder.ReadBuffer(DemoResourceIds::GBufferSpecularSmoothness);
            passBuilder.ReadBuffer(DemoResourceIds::GBufferNormal);
            passBuilder.ReadBuffer(DemoResourceIds::GBufferEmissionMetallic);
            passBuilder.ReadBuffer(DemoResourceIds::GBufferPosition);
            passBuilder.ReadBuffer(DemoResourceIds::MotionVector);
            passBuilder.ReadBuffer(DemoResourceIds::DepthBuffer);
            if (useCompactedDispatch)
            {
                passBuilder.ReadToken(DemoResourceIds::ActivePixelComputeDispatchReadyToken);
                passBuilder.ReadIndirectArgument(DemoResourceIds::ActivePixelDispatchData);
                passBuilder.ReadBuffer(DemoResourceIds::ActiveRayPixelIndices);
                passBuilder.ReadBuffer(DemoResourceIds::ActiveRayPixelCount);
            }
            passBuilder.WriteUav(DemoResourceIds::DirectLighting);
            passBuilder.WriteToken(DemoResourceIds::DirectLightingFinishedToken);
            RaytracingDemoPassBindings::DeclareRayTracingExternalResourceAccesses(
                passBuilder,
                resources,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        },
        [](const ReSTIRDIPassData& passData, const RenderContext& context, CommandList& commandList)
        {
            const RaytracingDemoPassResources& resources = passData.Resources.value();
            const RaytracingDemoPassConfig& config = passData.Config;
            const bool useCompactedDispatch = passData.UseCompactedDispatch;
            const RaytracingDemoRenderGraph::FrameGBufferResources gbuffer =
                RaytracingDemoRenderGraph::GetFrameGBufferResources(context);
            const RaytracingDemoCameraConstants camera =
                RaytracingDemoPassBindings::BuildPassCameraConstants(resources, config, context);

            ReSTIRDIExecutionInputs inputs;
            inputs.FrameState.Enabled = true;
            inputs.FrameState.UseSoftShadowVariant =
                resources.Pipelines.GetShadowMode() == PathTracingShadowMode::SoftShadows;
            inputs.FrameState.ShadingModel = config.FrameState->ShadingModel;
            inputs.FrameState.EnvironmentProjectionVariant =
                static_cast<uint32_t>(resources.Pipelines.GetLayout().EnvironmentProjection);
            inputs.FrameState.Width = config.FrameState->Width;
            inputs.FrameState.Height = config.FrameState->Height;
            inputs.FrameState.FrameIndex = config.FrameState->FrameIndex;
            inputs.FrameState.Constants = resources.DirectLightingReSTIRDI.GetFrameConstants(
                config.FrameState->ReSTIRDIHistoryValid);
            inputs.DirectLighting = context.GetTexture(DemoResourceIds::DirectLighting);
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
                    "Compacted ReSTIR DI requires active-pixel index, count, and dispatch-data buffers.");
                inputs.CompactedDispatch = resources.ActivePixels.GetComputeDispatch(
                    *activePixelIndices,
                    *activePixelCount,
                    *activePixelDispatchData);
            }
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
            resources.DirectLightingReSTIRDIPass.Execute(commandList, inputs);
        });
}
//Modify End
