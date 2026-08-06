//Modify Begin:2026-08-05 by BestHui
#include <Passes/RaytracingDemoPasses.h>

#include <PathTracing/PathTracingSceneBindings.h>
#include <RenderGraph/RaytracingDemoGraphResources.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>
#include <Framework/Rendering/Texture/UnorderedAccessView.h>
#include <RenderGraph/RenderContext.h>
#include <RenderGraph/RenderPass.h>

namespace
{
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;

    bool UsesReSTIRDI(const RaytracingDemoPassConfig& config)
    {
        return config.Backend != nullptr &&
            *config.Backend == PathTracingBackend::InlineRayQuery &&
            config.DirectLightingEnabled != nullptr &&
            *config.DirectLightingEnabled &&
            config.DirectLightingTechnique != nullptr &&
            *config.DirectLightingTechnique == RaytracingDemoLightingTechnique::ReSTIRDI;
    }

    void BindReSTIRDIConstants(
        CommandContext& commandContext,
        ComputeShader& shader,
        const RaytracingDemoPassResources& resources,
        const RaytracingDemoCameraConstants& camera)
    {
        const ReSTIRDIFrameConstants constants = resources.DirectLightingReSTIRDI.GetFrameConstants(camera.ReSTIRDIHistoryValid != 0u);
        commandContext.SetConstantBuffer(shader, "ReSTIRDIConstants", sizeof(constants), &constants);
    }

    void BindInlineReSTIRDIInputs(
        const RaytracingDemoPassResources& resources,
        const RaytracingDemoPassConfig& config,
        const RenderGraph::RenderContext& context,
        CommandList& commandList,
        ComputeShader& shader)
    {
        const RaytracingDemoRenderGraph::FrameGBufferResources gbuffer = RaytracingDemoRenderGraph::GetFrameGBufferResources(context);
        const RaytracingDemoCameraConstants camera = RaytracingDemoPassBindings::BuildPassCameraConstants(resources, config, context);
        RaytracingDemoPassBindings::BindInlinePathTracingInputs(resources, commandList, shader, gbuffer, camera);
        resources.Scene.TransitionRayTracingShaderResources(commandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        CommandContext commandContext(commandList);
        commandContext.BindBindlessDescriptorHeap(resources.Scene.GetBindlessDescriptorHeap());
        BindReSTIRDIConstants(commandContext, shader, resources, camera);
    }

    void DispatchReSTIRDI(CommandContext& commandContext, ComputeShader& shader, const RaytracingDemoCameraConstants& camera)
    {
        commandContext.BindPipeline(shader);
        commandContext.BindDescriptorSet(shader.GetDescriptorSet());
        commandContext.Dispatch(Math::DivideByMultiple(camera.Width, 8u), Math::DivideByMultiple(camera.Height, 8u), 1u);
    }

    RaytracingDemoCameraConstants GetCameraConstants(
        const RaytracingDemoPassResources& resources,
        const RaytracingDemoPassConfig& config,
        const RenderGraph::RenderContext& context)
    {
        return RaytracingDemoPassBindings::BuildPassCameraConstants(resources, config, context);
    }
}

std::unique_ptr<RenderGraph::RenderPass> RaytracingDemoPasses::Builder::CreateReSTIRDIRISPass(
    const RaytracingDemoPassResources& resources,
    const RaytracingDemoPassConfig& config)
{
    using namespace RenderGraph;
    return RenderPass::Create(
        L"ReSTIR DI RIS",
        {
            { DemoResourceIds::BaseResourcesFinishedToken, InputType::Token },
            { DemoResourceIds::GBufferAlbedoOcclusion, InputType::NonPixelShaderResource },
            { DemoResourceIds::GBufferSpecularSmoothness, InputType::NonPixelShaderResource },
            { DemoResourceIds::GBufferNormal, InputType::NonPixelShaderResource },
            { DemoResourceIds::GBufferEmissionMetallic, InputType::NonPixelShaderResource },
            { DemoResourceIds::GBufferPosition, InputType::NonPixelShaderResource },
            { DemoResourceIds::DepthBuffer, InputType::NonPixelShaderResource },
        },
        {
            { DemoResourceIds::ReSTIRDIRISReservoir, OutputType::UnorderedAccess },
            { DemoResourceIds::ReSTIRDIRISReservoirState, OutputType::UnorderedAccess },
            { DemoResourceIds::ReSTIRDIRISFinishedToken, OutputType::Token },
        },
        [resources, config](const RenderContext& context, CommandList& commandList)
        {
            if (!UsesReSTIRDI(config))
            {
                return;
            }

            ComputeShader& shader = resources.Pipelines.GetInlineReSTIRDIRISShader();
            BindInlineReSTIRDIInputs(resources, config, context, commandList, shader);
            const RaytracingDemoCameraConstants camera = GetCameraConstants(resources, config, context);
            CommandContext commandContext(commandList);
            commandContext.SetUnorderedAccessView(shader, "ReSTIRDIRISReservoir", UnorderedAccessView(context.m_ResourcePool->GetTexture(DemoResourceIds::ReSTIRDIRISReservoir)));
            commandContext.SetUnorderedAccessView(shader, "ReSTIRDIRISReservoirState", UnorderedAccessView(context.m_ResourcePool->GetTexture(DemoResourceIds::ReSTIRDIRISReservoirState)));
            DispatchReSTIRDI(commandContext, shader, camera);
        });
}

std::unique_ptr<RenderGraph::RenderPass> RaytracingDemoPasses::Builder::CreateReSTIRDITemporalPass(
    const RaytracingDemoPassResources& resources,
    const RaytracingDemoPassConfig& config)
{
    using namespace RenderGraph;
    return RenderPass::Create(
        L"ReSTIR DI Temporal",
        {
            { DemoResourceIds::ReSTIRDIRISReservoir, InputType::NonPixelShaderResource },
            { DemoResourceIds::ReSTIRDIRISReservoirState, InputType::NonPixelShaderResource },
            { DemoResourceIds::MotionVector, InputType::NonPixelShaderResource },
            { DemoResourceIds::GBufferAlbedoOcclusion, InputType::NonPixelShaderResource },
            { DemoResourceIds::GBufferSpecularSmoothness, InputType::NonPixelShaderResource },
            { DemoResourceIds::GBufferNormal, InputType::NonPixelShaderResource },
            { DemoResourceIds::GBufferEmissionMetallic, InputType::NonPixelShaderResource },
            { DemoResourceIds::GBufferPosition, InputType::NonPixelShaderResource },
            { DemoResourceIds::DepthBuffer, InputType::NonPixelShaderResource },
            { DemoResourceIds::ReSTIRDIRISFinishedToken, InputType::Token },
        },
        {
            { DemoResourceIds::ReSTIRDITemporalReservoir, OutputType::UnorderedAccess },
            { DemoResourceIds::ReSTIRDITemporalReservoirState, OutputType::UnorderedAccess },
            { DemoResourceIds::ReSTIRDITemporalFinishedToken, OutputType::Token },
        },
        [resources, config](const RenderContext& context, CommandList& commandList)
        {
            if (!UsesReSTIRDI(config))
            {
                return;
            }

            ComputeShader& shader = resources.Pipelines.GetInlineReSTIRDITemporalShader();
            BindInlineReSTIRDIInputs(resources, config, context, commandList, shader);
            const RaytracingDemoCameraConstants camera = GetCameraConstants(resources, config, context);
            const RaytracingDemoRenderGraph::ReSTIRDIResources reservoirResources = RaytracingDemoRenderGraph::GetReSTIRDIResources(context);
            const bool writeReservoirA = (camera.FrameIndex & 1u) == 0u;
            CommandContext commandContext(commandList);
            commandContext.SetShaderResourceView(shader, "ReSTIRDIRISReservoir", ShaderResourceView(reservoirResources.RISReservoir));
            commandContext.SetShaderResourceView(shader, "ReSTIRDIRISReservoirState", ShaderResourceView(reservoirResources.RISReservoirState));
            commandContext.SetShaderResourceView(shader, "MotionVectorTexture", ShaderResourceView(RaytracingDemoRenderGraph::GetFrameGBufferResources(context).MotionVector));
            commandContext.SetShaderResourceView(shader, "ReSTIRDIHistoryReservoir", ShaderResourceView(writeReservoirA ? reservoirResources.ReservoirB : reservoirResources.ReservoirA));
            commandContext.SetShaderResourceView(shader, "ReSTIRDIHistoryReservoirState", ShaderResourceView(writeReservoirA ? reservoirResources.ReservoirBState : reservoirResources.ReservoirAState));
            commandContext.SetShaderResourceView(shader, "ReSTIRDIHistoryPosition", ShaderResourceView(writeReservoirA ? reservoirResources.HistoryPositionB : reservoirResources.HistoryPositionA));
            commandContext.SetShaderResourceView(shader, "ReSTIRDIHistoryNormalRoughness", ShaderResourceView(writeReservoirA ? reservoirResources.HistoryNormalRoughnessB : reservoirResources.HistoryNormalRoughnessA));
            commandContext.SetShaderResourceView(shader, "ReSTIRDIHistoryDiffuseMetallic", ShaderResourceView(writeReservoirA ? reservoirResources.HistoryDiffuseMetallicB : reservoirResources.HistoryDiffuseMetallicA));
            commandContext.SetShaderResourceView(shader, "ReSTIRDIHistorySpecularOcclusion", ShaderResourceView(writeReservoirA ? reservoirResources.HistorySpecularOcclusionB : reservoirResources.HistorySpecularOcclusionA));
            commandContext.SetUnorderedAccessView(shader, "ReSTIRDITemporalReservoir", UnorderedAccessView(reservoirResources.TemporalReservoir));
            commandContext.SetUnorderedAccessView(shader, "ReSTIRDITemporalReservoirState", UnorderedAccessView(reservoirResources.TemporalReservoirState));
            DispatchReSTIRDI(commandContext, shader, camera);
        });
}

std::unique_ptr<RenderGraph::RenderPass> RaytracingDemoPasses::Builder::CreateReSTIRDISpatialPass(
    const RaytracingDemoPassResources& resources,
    const RaytracingDemoPassConfig& config)
{
    using namespace RenderGraph;
    return RenderPass::Create(
        L"ReSTIR DI Spatial",
        {
            { DemoResourceIds::ReSTIRDITemporalReservoir, InputType::NonPixelShaderResource },
            { DemoResourceIds::ReSTIRDITemporalReservoirState, InputType::NonPixelShaderResource },
            { DemoResourceIds::GBufferAlbedoOcclusion, InputType::NonPixelShaderResource },
            { DemoResourceIds::GBufferSpecularSmoothness, InputType::NonPixelShaderResource },
            { DemoResourceIds::GBufferNormal, InputType::NonPixelShaderResource },
            { DemoResourceIds::GBufferEmissionMetallic, InputType::NonPixelShaderResource },
            { DemoResourceIds::GBufferPosition, InputType::NonPixelShaderResource },
            { DemoResourceIds::DepthBuffer, InputType::NonPixelShaderResource },
            { DemoResourceIds::ReSTIRDITemporalFinishedToken, InputType::Token },
        },
        {
            { DemoResourceIds::ReSTIRDISpatialReservoir, OutputType::UnorderedAccess },
            { DemoResourceIds::ReSTIRDISpatialReservoirState, OutputType::UnorderedAccess },
            { DemoResourceIds::ReSTIRDISpatialFinishedToken, OutputType::Token },
        },
        [resources, config](const RenderContext& context, CommandList& commandList)
        {
            if (!UsesReSTIRDI(config))
            {
                return;
            }

            ComputeShader& shader = resources.Pipelines.GetInlineReSTIRDISpatialShader();
            BindInlineReSTIRDIInputs(resources, config, context, commandList, shader);
            const RaytracingDemoCameraConstants camera = GetCameraConstants(resources, config, context);
            const RaytracingDemoRenderGraph::ReSTIRDIResources reservoirResources = RaytracingDemoRenderGraph::GetReSTIRDIResources(context);
            CommandContext commandContext(commandList);
            commandContext.SetShaderResourceView(
                shader,
                "ReSTIRDITemporalReservoir",
                ShaderResourceView(reservoirResources.TemporalReservoir));
            commandContext.SetShaderResourceView(shader, "ReSTIRDITemporalReservoirState", ShaderResourceView(reservoirResources.TemporalReservoirState));
            commandContext.SetUnorderedAccessView(shader, "ReSTIRDISpatialReservoir", UnorderedAccessView(reservoirResources.SpatialReservoir));
            commandContext.SetUnorderedAccessView(shader, "ReSTIRDISpatialReservoirState", UnorderedAccessView(reservoirResources.SpatialReservoirState));
            DispatchReSTIRDI(commandContext, shader, camera);
        });
}

std::unique_ptr<RenderGraph::RenderPass> RaytracingDemoPasses::Builder::CreateReSTIRDIShadePass(
    const RaytracingDemoPassResources& resources,
    const RaytracingDemoPassConfig& config)
{
    using namespace RenderGraph;
    return RenderPass::Create(
        L"ReSTIR DI Shade",
        {
            { DemoResourceIds::ReSTIRDISpatialReservoir, InputType::NonPixelShaderResource },
            { DemoResourceIds::ReSTIRDISpatialReservoirState, InputType::NonPixelShaderResource },
            { DemoResourceIds::GBufferAlbedoOcclusion, InputType::NonPixelShaderResource },
            { DemoResourceIds::GBufferSpecularSmoothness, InputType::NonPixelShaderResource },
            { DemoResourceIds::GBufferNormal, InputType::NonPixelShaderResource },
            { DemoResourceIds::GBufferEmissionMetallic, InputType::NonPixelShaderResource },
            { DemoResourceIds::GBufferPosition, InputType::NonPixelShaderResource },
            { DemoResourceIds::DepthBuffer, InputType::NonPixelShaderResource },
            { DemoResourceIds::ReSTIRDISpatialFinishedToken, InputType::Token },
        },
        {
            { DemoResourceIds::DirectLighting, OutputType::UnorderedAccess },
            { DemoResourceIds::ReSTIRDIReservoirA, OutputType::UnorderedAccess },
            { DemoResourceIds::ReSTIRDIReservoirB, OutputType::UnorderedAccess },
            { DemoResourceIds::ReSTIRDIReservoirAState, OutputType::UnorderedAccess },
            { DemoResourceIds::ReSTIRDIReservoirBState, OutputType::UnorderedAccess },
            { DemoResourceIds::ReSTIRDIHistoryPositionA, OutputType::UnorderedAccess },
            { DemoResourceIds::ReSTIRDIHistoryPositionB, OutputType::UnorderedAccess },
            { DemoResourceIds::ReSTIRDIHistoryNormalRoughnessA, OutputType::UnorderedAccess },
            { DemoResourceIds::ReSTIRDIHistoryNormalRoughnessB, OutputType::UnorderedAccess },
            { DemoResourceIds::ReSTIRDIHistoryDiffuseMetallicA, OutputType::UnorderedAccess },
            { DemoResourceIds::ReSTIRDIHistoryDiffuseMetallicB, OutputType::UnorderedAccess },
            { DemoResourceIds::ReSTIRDIHistorySpecularOcclusionA, OutputType::UnorderedAccess },
            { DemoResourceIds::ReSTIRDIHistorySpecularOcclusionB, OutputType::UnorderedAccess },
            { DemoResourceIds::ReSTIRDIShadeFinishedToken, OutputType::Token },
        },
        [resources, config](const RenderContext& context, CommandList& commandList)
        {
            if (!UsesReSTIRDI(config))
            {
                return;
            }

            ComputeShader& shader = resources.Pipelines.GetInlineReSTIRDIShadeShader();
            const RaytracingDemoRenderGraph::FrameGBufferResources gbuffer = RaytracingDemoRenderGraph::GetFrameGBufferResources(context);
            const RaytracingDemoCameraConstants camera = GetCameraConstants(resources, config, context);
            RaytracingDemoPassBindings::BindInlinePathTracingInputs(resources, commandList, shader, gbuffer, camera);
            resources.Scene.TransitionRayTracingShaderResources(commandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            const RaytracingDemoRenderGraph::ReSTIRDIResources reservoirResources = RaytracingDemoRenderGraph::GetReSTIRDIResources(context);
            CommandContext commandContext(commandList);
            commandContext.BindBindlessDescriptorHeap(resources.Scene.GetBindlessDescriptorHeap());
            BindReSTIRDIConstants(commandContext, shader, resources, camera);
            commandContext.SetShaderResourceView(shader, "ReSTIRDIFinalReservoir", ShaderResourceView(reservoirResources.SpatialReservoir));
            commandContext.SetShaderResourceView(shader, "ReSTIRDIFinalReservoirState", ShaderResourceView(reservoirResources.SpatialReservoirState));
            commandContext.SetUnorderedAccessView(shader, "DirectLighting", UnorderedAccessView(context.m_ResourcePool->GetTexture(DemoResourceIds::DirectLighting)));
            const bool writeReservoirA = (camera.FrameIndex & 1u) == 0u;
            commandContext.SetUnorderedAccessView(shader, "ReSTIRDICurrentReservoir", UnorderedAccessView(writeReservoirA ? reservoirResources.ReservoirA : reservoirResources.ReservoirB));
            commandContext.SetUnorderedAccessView(shader, "ReSTIRDICurrentReservoirState", UnorderedAccessView(writeReservoirA ? reservoirResources.ReservoirAState : reservoirResources.ReservoirBState));
            commandContext.SetUnorderedAccessView(shader, "ReSTIRDICurrentPosition", UnorderedAccessView(writeReservoirA ? reservoirResources.HistoryPositionA : reservoirResources.HistoryPositionB));
            commandContext.SetUnorderedAccessView(shader, "ReSTIRDICurrentNormalRoughness", UnorderedAccessView(writeReservoirA ? reservoirResources.HistoryNormalRoughnessA : reservoirResources.HistoryNormalRoughnessB));
            commandContext.SetUnorderedAccessView(shader, "ReSTIRDICurrentDiffuseMetallic", UnorderedAccessView(writeReservoirA ? reservoirResources.HistoryDiffuseMetallicA : reservoirResources.HistoryDiffuseMetallicB));
            commandContext.SetUnorderedAccessView(shader, "ReSTIRDICurrentSpecularOcclusion", UnorderedAccessView(writeReservoirA ? reservoirResources.HistorySpecularOcclusionA : reservoirResources.HistorySpecularOcclusionB));
            DispatchReSTIRDI(commandContext, shader, camera);
        });
}
//Modify End
