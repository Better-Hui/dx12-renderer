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
            { DemoResourceIds::MotionVector, InputType::NonPixelShaderResource },
            { DemoResourceIds::GBufferAlbedoOcclusion, InputType::NonPixelShaderResource },
            { DemoResourceIds::GBufferSpecularSmoothness, InputType::NonPixelShaderResource },
            { DemoResourceIds::GBufferNormal, InputType::NonPixelShaderResource },
            { DemoResourceIds::GBufferEmissionMetallic, InputType::NonPixelShaderResource },
            { DemoResourceIds::GBufferPosition, InputType::NonPixelShaderResource },
            { DemoResourceIds::DepthBuffer, InputType::NonPixelShaderResource },
            { DemoResourceIds::ReSTIRDIReservoirA, InputType::UnorderedAccess },
            { DemoResourceIds::ReSTIRDIReservoirB, InputType::UnorderedAccess },
            { DemoResourceIds::ReSTIRDIHistoryPositionA, InputType::UnorderedAccess },
            { DemoResourceIds::ReSTIRDIHistoryPositionB, InputType::UnorderedAccess },
            { DemoResourceIds::ReSTIRDIHistoryNormalRoughnessA, InputType::UnorderedAccess },
            { DemoResourceIds::ReSTIRDIHistoryNormalRoughnessB, InputType::UnorderedAccess },
            { DemoResourceIds::ReSTIRDIHistoryDiffuseMetallicA, InputType::UnorderedAccess },
            { DemoResourceIds::ReSTIRDIHistoryDiffuseMetallicB, InputType::UnorderedAccess },
            { DemoResourceIds::ReSTIRDIHistorySpecularOcclusionA, InputType::UnorderedAccess },
            { DemoResourceIds::ReSTIRDIHistorySpecularOcclusionB, InputType::UnorderedAccess },
            { DemoResourceIds::ReSTIRDIRISFinishedToken, InputType::Token },
        },
        {
            { DemoResourceIds::ReSTIRDIReservoirA, OutputType::UnorderedAccess },
            { DemoResourceIds::ReSTIRDIReservoirB, OutputType::UnorderedAccess },
            { DemoResourceIds::ReSTIRDIHistoryPositionA, OutputType::UnorderedAccess },
            { DemoResourceIds::ReSTIRDIHistoryPositionB, OutputType::UnorderedAccess },
            { DemoResourceIds::ReSTIRDIHistoryNormalRoughnessA, OutputType::UnorderedAccess },
            { DemoResourceIds::ReSTIRDIHistoryNormalRoughnessB, OutputType::UnorderedAccess },
            { DemoResourceIds::ReSTIRDIHistoryDiffuseMetallicA, OutputType::UnorderedAccess },
            { DemoResourceIds::ReSTIRDIHistoryDiffuseMetallicB, OutputType::UnorderedAccess },
            { DemoResourceIds::ReSTIRDIHistorySpecularOcclusionA, OutputType::UnorderedAccess },
            { DemoResourceIds::ReSTIRDIHistorySpecularOcclusionB, OutputType::UnorderedAccess },
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
            commandContext.SetUnorderedAccessView(shader, "ReSTIRDIHistoryReservoir", UnorderedAccessView(writeReservoirA ? reservoirResources.ReservoirB : reservoirResources.ReservoirA));
            commandContext.SetUnorderedAccessView(shader, "ReSTIRDICurrentReservoir", UnorderedAccessView(writeReservoirA ? reservoirResources.ReservoirA : reservoirResources.ReservoirB));
            commandContext.SetUnorderedAccessView(shader, "ReSTIRDIHistoryPosition", UnorderedAccessView(writeReservoirA ? reservoirResources.HistoryPositionB : reservoirResources.HistoryPositionA));
            commandContext.SetUnorderedAccessView(shader, "ReSTIRDICurrentPosition", UnorderedAccessView(writeReservoirA ? reservoirResources.HistoryPositionA : reservoirResources.HistoryPositionB));
            commandContext.SetUnorderedAccessView(shader, "ReSTIRDIHistoryNormalRoughness", UnorderedAccessView(writeReservoirA ? reservoirResources.HistoryNormalRoughnessB : reservoirResources.HistoryNormalRoughnessA));
            commandContext.SetUnorderedAccessView(shader, "ReSTIRDICurrentNormalRoughness", UnorderedAccessView(writeReservoirA ? reservoirResources.HistoryNormalRoughnessA : reservoirResources.HistoryNormalRoughnessB));
            commandContext.SetUnorderedAccessView(shader, "ReSTIRDIHistoryDiffuseMetallic", UnorderedAccessView(writeReservoirA ? reservoirResources.HistoryDiffuseMetallicB : reservoirResources.HistoryDiffuseMetallicA));
            commandContext.SetUnorderedAccessView(shader, "ReSTIRDICurrentDiffuseMetallic", UnorderedAccessView(writeReservoirA ? reservoirResources.HistoryDiffuseMetallicA : reservoirResources.HistoryDiffuseMetallicB));
            commandContext.SetUnorderedAccessView(shader, "ReSTIRDIHistorySpecularOcclusion", UnorderedAccessView(writeReservoirA ? reservoirResources.HistorySpecularOcclusionB : reservoirResources.HistorySpecularOcclusionA));
            commandContext.SetUnorderedAccessView(shader, "ReSTIRDICurrentSpecularOcclusion", UnorderedAccessView(writeReservoirA ? reservoirResources.HistorySpecularOcclusionA : reservoirResources.HistorySpecularOcclusionB));
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
            { DemoResourceIds::ReSTIRDIBoilingReservoir, InputType::NonPixelShaderResource },
            { DemoResourceIds::GBufferAlbedoOcclusion, InputType::NonPixelShaderResource },
            { DemoResourceIds::GBufferSpecularSmoothness, InputType::NonPixelShaderResource },
            { DemoResourceIds::GBufferNormal, InputType::NonPixelShaderResource },
            { DemoResourceIds::GBufferEmissionMetallic, InputType::NonPixelShaderResource },
            { DemoResourceIds::GBufferPosition, InputType::NonPixelShaderResource },
            { DemoResourceIds::DepthBuffer, InputType::NonPixelShaderResource },
            { DemoResourceIds::ReSTIRDIBoilingFinishedToken, InputType::Token },
        },
        {
            { DemoResourceIds::ReSTIRDISpatialReservoir, OutputType::UnorderedAccess },
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
                ShaderResourceView(reservoirResources.BoilingReservoir));
            commandContext.SetUnorderedAccessView(shader, "ReSTIRDISpatialReservoir", UnorderedAccessView(reservoirResources.SpatialReservoir));
            DispatchReSTIRDI(commandContext, shader, camera);
        });
}

std::unique_ptr<RenderGraph::RenderPass> RaytracingDemoPasses::Builder::CreateReSTIRDIBoilingPass(
    const RaytracingDemoPassResources& resources,
    const RaytracingDemoPassConfig& config)
{
    using namespace RenderGraph;
    return RenderPass::Create(
        L"ReSTIR DI Boiling Filter",
        {
            { DemoResourceIds::ReSTIRDIReservoirA, InputType::NonPixelShaderResource },
            { DemoResourceIds::ReSTIRDIReservoirB, InputType::NonPixelShaderResource },
            { DemoResourceIds::ReSTIRDITemporalFinishedToken, InputType::Token },
        },
        {
            { DemoResourceIds::ReSTIRDIBoilingReservoir, OutputType::UnorderedAccess },
            { DemoResourceIds::ReSTIRDIBoilingFinishedToken, OutputType::Token },
        },
        [resources, config](const RenderContext& context, CommandList& commandList)
        {
            if (!UsesReSTIRDI(config))
            {
                return;
            }

            ComputeShader& shader = resources.Pipelines.GetInlineReSTIRDIBoilingShader();
            const RaytracingDemoCameraConstants camera = GetCameraConstants(resources, config, context);
            const RaytracingDemoRenderGraph::ReSTIRDIResources reservoirResources = RaytracingDemoRenderGraph::GetReSTIRDIResources(context);
            CommandContext commandContext(commandList);
            if (shader.HasConstantBuffer("CameraConstants"))
            {
                commandContext.SetConstantBuffer(shader, "CameraConstants", sizeof(camera), &camera);
            }
            if (shader.HasConstantBuffer("ReSTIRDIConstants"))
            {
                BindReSTIRDIConstants(commandContext, shader, resources, camera);
            }
            if (shader.HasShaderResourceView("ReSTIRDIInputReservoir"))
            {
                commandContext.SetShaderResourceView(
                    shader,
                    "ReSTIRDIInputReservoir",
                    ShaderResourceView((camera.FrameIndex & 1u) == 0u ? reservoirResources.ReservoirA : reservoirResources.ReservoirB));
            }
            if (shader.HasUnorderedAccessView("ReSTIRDIBoilingReservoir"))
            {
                commandContext.SetUnorderedAccessView(shader, "ReSTIRDIBoilingReservoir", UnorderedAccessView(reservoirResources.BoilingReservoir));
            }
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
            commandContext.SetUnorderedAccessView(shader, "DirectLighting", UnorderedAccessView(context.m_ResourcePool->GetTexture(DemoResourceIds::DirectLighting)));
            DispatchReSTIRDI(commandContext, shader, camera);
        });
}
//Modify End
