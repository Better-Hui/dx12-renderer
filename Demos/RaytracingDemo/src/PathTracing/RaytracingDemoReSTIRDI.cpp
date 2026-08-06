#include <PathTracing/RaytracingDemoReSTIRDI.h>

//Modify Begin:2026-07-30 by BestHui
#include <PathTracing/PathTracingSceneBindings.h>
#include <RenderGraph/RaytracingDemoGraphResources.h>

#include <DX12Library/CommandList.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <RenderGraph/RenderContext.h>

namespace
{
    bool UsesReSTIRDI(const RaytracingDemoPassConfig& config)
    {
        return config.FrameState->Backend == PathTracingBackend::InlineRayQuery &&
            config.FrameState->DirectLightingTechnique == RaytracingDemoLightingTechnique::ReSTIRDI;
    }
}

FrameworkRenderFeatures::ReSTIRDIPassInputs RaytracingDemoReSTIRDI::CreatePassInputs(
    const RaytracingDemoPassResources& resources,
    const RaytracingDemoPassConfig& config)
{
    using namespace FrameworkRenderFeatures;
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;

    auto sceneAdapter = std::make_shared<ReSTIRDISceneAdapter>();
    sceneAdapter->GetFrameState = [resources, config](const RenderGraph::RenderContext& context)
    {
        ReSTIRDIFrameState frameState;
        frameState.Enabled = UsesReSTIRDI(config);
        frameState.UseSoftShadowVariant = resources.Pipelines.GetShadowMode() == PathTracingShadowMode::SoftShadows;
        frameState.Width = config.FrameState->Width;
        frameState.Height = config.FrameState->Height;
        frameState.FrameIndex = config.FrameState->FrameIndex;
        frameState.Constants = resources.DirectLightingReSTIRDI.GetFrameConstants(
            config.FrameState->ReSTIRDIHistoryValid);
        return frameState;
    };
    sceneAdapter->BindInputs = [resources, config](
        const RenderGraph::RenderContext& context,
        CommandList& commandList,
        ComputeShader& shader)
    {
        const RaytracingDemoRenderGraph::FrameGBufferResources gbuffer =
            RaytracingDemoRenderGraph::GetFrameGBufferResources(context);
        const RaytracingDemoCameraConstants camera =
            RaytracingDemoPassBindings::BuildPassCameraConstants(resources, config, context);
        RaytracingDemoPassBindings::BindInlinePathTracingInputs(resources, commandList, shader, gbuffer, camera);
        resources.Scene.TransitionRayTracingShaderResources(commandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        CommandContext(commandList).BindBindlessDescriptorHeap(resources.Scene.GetBindlessDescriptorHeap());
    };

    return {
        {
            DemoResourceIds::BaseResourcesFinishedToken,
            {
                DemoResourceIds::GBufferAlbedoOcclusion,
                DemoResourceIds::GBufferSpecularSmoothness,
                DemoResourceIds::GBufferNormal,
                DemoResourceIds::GBufferEmissionMetallic,
                DemoResourceIds::GBufferPosition,
                DemoResourceIds::MotionVector,
                DemoResourceIds::DepthBuffer,
            },
            {
                DemoResourceIds::ReSTIRDIReservoirA,
                DemoResourceIds::ReSTIRDIReservoirB,
                DemoResourceIds::ReSTIRDIReservoirAState,
                DemoResourceIds::ReSTIRDIReservoirBState,
                DemoResourceIds::ReSTIRDIHistoryPositionA,
                DemoResourceIds::ReSTIRDIHistoryPositionB,
                DemoResourceIds::ReSTIRDIHistoryNormalRoughnessA,
                DemoResourceIds::ReSTIRDIHistoryNormalRoughnessB,
                DemoResourceIds::ReSTIRDIHistoryDiffuseMetallicA,
                DemoResourceIds::ReSTIRDIHistoryDiffuseMetallicB,
                DemoResourceIds::ReSTIRDIHistorySpecularOcclusionA,
                DemoResourceIds::ReSTIRDIHistorySpecularOcclusionB,
            },
            {
                DemoResourceIds::ReSTIRDIRISReservoir,
                DemoResourceIds::ReSTIRDIRISReservoirState,
                DemoResourceIds::ReSTIRDITemporalReservoir,
                DemoResourceIds::ReSTIRDITemporalReservoirState,
                DemoResourceIds::ReSTIRDISpatialReservoir,
                DemoResourceIds::ReSTIRDISpatialReservoirState,
                DemoResourceIds::ReSTIRDIRISFinishedToken,
                DemoResourceIds::ReSTIRDITemporalFinishedToken,
                DemoResourceIds::ReSTIRDISpatialFinishedToken,
                DemoResourceIds::ReSTIRDIShadeFinishedToken,
            },
            DemoResourceIds::DirectLighting,
        },
        std::move(sceneAdapter),
    };
}
//Modify End
