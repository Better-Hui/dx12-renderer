#include <Passes/RaytracingDemoPasses.h>

//Modify Begin:2026-08-18 by Hui
#include <PathTracing/PathTracingSceneBindings.h>
#include <RenderGraph/RaytracingDemoGraphResources.h>

#include <DX12Library/CommandList.h>
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
    };
}

void RaytracingDemoPasses::Builder::AddReSTIRDIPass(
    RenderGraph::RenderGraphBuilder& renderGraphBuilder,
    const RaytracingDemoPassResources& resources,
    const RaytracingDemoPassConfig& config)
{
    using namespace RenderGraph;
    renderGraphBuilder.AddPass<ReSTIRDIPassData>(
        L"ReSTIR DI",
        [&resources, config](RenderGraphPassBuilder& passBuilder, ReSTIRDIPassData& passData)
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
