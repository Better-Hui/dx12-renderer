#include <Passes/RaytracingDemoPasses.h>

#include <PathTracing/PathTracingSceneBindings.h>
#include <RenderGraph/RaytracingDemoGraphResources.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/Texture/UnorderedAccessView.h>
#include <RenderGraph/RenderPass.h>
//Modify Begin:2026-07-30 by BestHui
#include <Scene/SceneLightManager.h>
//Modify End

#include <string_view>

namespace
{
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;

//Modify Begin:2026-07-27 by BestHui
    void DispatchDxrLightingPass(
        CommandList& cmd,
//Modify Begin:2026-07-30 by BestHui
        BindlessDescriptorHeap& bindlessDescriptorHeap,
//Modify End
        RayTracingShader& shader,
        RayTracingBindingSet& bindingSet,
        const std::string_view passName,
        const uint32_t width,
        const uint32_t height)
    {
//Modify Begin:2026-07-29 by BestHui
        CommandContext commandContext(cmd);
//Modify Begin:2026-07-30 by BestHui
        commandContext.BindBindlessDescriptorHeap(bindlessDescriptorHeap);
//Modify End
        commandContext.BindPipeline(shader);
        commandContext.BindDescriptorSet(bindingSet);
        commandContext.DispatchRays(RayTracingDispatchDesc{ passName, width, height, 1u });
        commandContext.InsertDescriptorSetOutputBarriers(bindingSet);
//Modify End
    }
//Modify End
}

//Modify Begin:2026-07-30 by BestHui
std::unique_ptr<RenderGraph::RenderPass> RaytracingDemoPasses::Builder::CreateDirectLightingPass(
    const RaytracingDemoPassResources& resources,
    const RaytracingDemoPassConfig& config)
{
    using namespace RenderGraph;
    const PathTracingBackend backend = config.Backend != nullptr ? *config.Backend : resources.Pipelines.GetBackend();
    const InputType gbufferInputType = backend == PathTracingBackend::InlineRayQuery
        ? InputType::NonPixelShaderResource
        : InputType::ShaderResource;

    return RenderPass::Create(
        L"Direct Lighting",
        {
            { DemoResourceIds::BaseResourcesFinishedToken, InputType::Token },
            { DemoResourceIds::GBufferAlbedoOcclusion, gbufferInputType },
            { DemoResourceIds::GBufferSpecularSmoothness, gbufferInputType },
            { DemoResourceIds::GBufferNormal, gbufferInputType },
            { DemoResourceIds::GBufferEmissionMetallic, gbufferInputType },
            { DemoResourceIds::GBufferPosition, gbufferInputType },
            { DemoResourceIds::DepthBuffer, gbufferInputType },
        },
        {
            { DemoResourceIds::DirectLighting, OutputType::UnorderedAccess },
            { DemoResourceIds::DirectLightingFinishedToken, OutputType::Token },
        },
        [resources, config, backend](const RenderContext& context, CommandList& cmd)
        {
            if (config.DirectLightingEnabled == nullptr || !*config.DirectLightingEnabled)
            {
                return;
            }

            const RaytracingDemoRenderGraph::FrameGBufferResources gbuffer = RaytracingDemoRenderGraph::GetFrameGBufferResources(context);
            const RaytracingDemoCameraConstants camera = RaytracingDemoPassBindings::BuildPassCameraConstants(resources, config, context);

            if (backend == PathTracingBackend::InlineRayQuery)
            {
                ComputeShader& directLightingShader = resources.Pipelines.GetInlineDirectLightingShader();
                RaytracingDemoPassBindings::BindInlinePathTracingInputs(resources, cmd, directLightingShader, gbuffer, camera);
//Modify Begin:2026-07-28 by BestHui
                resources.Scene.TransitionRayTracingShaderResources(
                    cmd,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                CommandContext commandContext(cmd);
                commandContext.SetUnorderedAccessView(directLightingShader, "DirectLighting", UnorderedAccessView(context.m_ResourcePool->GetTexture(DemoResourceIds::DirectLighting)));
//Modify Begin:2026-07-30 by BestHui
                commandContext.BindBindlessDescriptorHeap(resources.Scene.GetBindlessDescriptorHeap());
//Modify End
                commandContext.BindPipeline(directLightingShader);
                commandContext.BindDescriptorSet(directLightingShader.GetDescriptorSet());
                commandContext.Dispatch(Math::DivideByMultiple(camera.Width, 8u), Math::DivideByMultiple(camera.Height, 8u), 1u);
//Modify End
            }
            else
            {
                resources.Scene.TransitionRayTracingShaderResources(
                    cmd,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                RayTracingBindingSet& directBindingSet = resources.Pipelines.GetDirectRayTracingBindingSet();
                directBindingSet.SetUnorderedAccessView("DirectLighting", UnorderedAccessView(context.m_ResourcePool->GetTexture(DemoResourceIds::DirectLighting)));
                RaytracingDemoPassBindings::BindDxrPathTracingInputs(resources, directBindingSet, gbuffer, camera);
//Modify Begin:2026-07-27 by BestHui
                DispatchDxrLightingPass(
                    cmd,
//Modify Begin:2026-07-30 by BestHui
                    resources.Scene.GetBindlessDescriptorHeap(),
//Modify End
                    resources.Pipelines.GetRayTracingShader(),
                    directBindingSet,
                    "DirectLightingRayGen",
                    camera.Width,
                    camera.Height);
//Modify End
            }
        });
}

std::unique_ptr<RenderGraph::RenderPass> RaytracingDemoPasses::Builder::CreateIndirectLightingPass(
    const RaytracingDemoPassResources& resources,
    const RaytracingDemoPassConfig& config)
{
    using namespace RenderGraph;
//Modify Begin:2026-08-03 by BestHui
    const PathTracingBackend backend = config.Backend != nullptr ? *config.Backend : resources.Pipelines.GetBackend();
    const RenderPassQueue queue =
        config.AsyncComputeEnabled != nullptr && *config.AsyncComputeEnabled && backend == PathTracingBackend::InlineRayQuery
            ? RenderPassQueue::AsyncCompute
            : RenderPassQueue::Direct;
    const InputType gbufferInputType = backend == PathTracingBackend::InlineRayQuery
        ? InputType::NonPixelShaderResource
        : InputType::ShaderResource;
//Modify End

    auto pass = RenderPass::Create(
        L"Indirect Lighting",
        {
//Modify Begin:2026-08-03 by BestHui
            { DemoResourceIds::BaseResourcesFinishedToken, InputType::Token },
//Modify End
            { DemoResourceIds::GBufferAlbedoOcclusion, gbufferInputType },
            { DemoResourceIds::GBufferSpecularSmoothness, gbufferInputType },
            { DemoResourceIds::GBufferNormal, gbufferInputType },
            { DemoResourceIds::GBufferEmissionMetallic, gbufferInputType },
            { DemoResourceIds::GBufferPosition, gbufferInputType },
            { DemoResourceIds::DepthBuffer, gbufferInputType },
        },
        {
            { DemoResourceIds::IndirectLighting, OutputType::UnorderedAccess },
            { DemoResourceIds::IndirectLightingFinishedToken, OutputType::Token },
        },
        [resources, config, backend, queue](const RenderContext& context, CommandList& cmd)
        {
            if (config.IndirectLightingEnabled == nullptr || !*config.IndirectLightingEnabled)
            {
                return;
            }

            const RaytracingDemoRenderGraph::FrameGBufferResources gbuffer = RaytracingDemoRenderGraph::GetFrameGBufferResources(context);
            const RaytracingDemoCameraConstants camera = RaytracingDemoPassBindings::BuildPassCameraConstants(resources, config, context);

            if (backend == PathTracingBackend::InlineRayQuery)
            {
                ComputeShader& indirectLightingShader = resources.Pipelines.GetInlineIndirectLightingShader();
                RaytracingDemoPassBindings::BindInlinePathTracingInputs(resources, cmd, indirectLightingShader, gbuffer, camera);
//Modify Begin:2026-07-28 by BestHui
                if (queue != RenderPassQueue::AsyncCompute)
                {
                    resources.Scene.TransitionRayTracingShaderResources(
                        cmd,
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                }
                CommandContext commandContext(cmd);
                commandContext.SetUnorderedAccessView(indirectLightingShader, "IndirectLighting", UnorderedAccessView(context.m_ResourcePool->GetTexture(DemoResourceIds::IndirectLighting)));
//Modify Begin:2026-07-30 by BestHui
                commandContext.BindBindlessDescriptorHeap(resources.Scene.GetBindlessDescriptorHeap());
//Modify End
                commandContext.BindPipeline(indirectLightingShader);
                commandContext.BindDescriptorSet(indirectLightingShader.GetDescriptorSet());
                commandContext.Dispatch(Math::DivideByMultiple(camera.Width, 8u), Math::DivideByMultiple(camera.Height, 8u), 1u);
//Modify End
            }
            else
            {
                resources.Scene.TransitionRayTracingShaderResources(
                    cmd,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                RayTracingBindingSet& indirectBindingSet = resources.Pipelines.GetIndirectRayTracingBindingSet();
                indirectBindingSet.SetUnorderedAccessView("IndirectLighting", UnorderedAccessView(context.m_ResourcePool->GetTexture(DemoResourceIds::IndirectLighting)));
                RaytracingDemoPassBindings::BindDxrPathTracingInputs(resources, indirectBindingSet, gbuffer, camera);
//Modify Begin:2026-07-27 by BestHui
                DispatchDxrLightingPass(
                    cmd,
//Modify Begin:2026-07-30 by BestHui
                    resources.Scene.GetBindlessDescriptorHeap(),
//Modify End
                    resources.Pipelines.GetRayTracingShader(),
                    indirectBindingSet,
                    "IndirectLightingRayGen",
                    camera.Width,
                    camera.Height);
//Modify End
            }
//Modify Begin:2026-08-03 by BestHui
        },
        queue);
    if (queue == RenderPassQueue::AsyncCompute)
    {
        pass->SetAsyncComputePrepare([resources](CommandList& commandList)
        {
            resources.Scene.TransitionRayTracingShaderResources(
                commandList,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            commandList.TransitionBarrier(
                resources.Scene.GetMaterialBuffer(),
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            commandList.TransitionBarrier(
                resources.Scene.GetGeometryBuffer(),
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            if (resources.SkyboxTexture != nullptr)
            {
                commandList.TransitionBarrier(
                    *resources.SkyboxTexture,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            }
            resources.Lights.PrepareAsyncComputeResources(commandList);
        });
    }
    return pass;
//Modify End
}

std::unique_ptr<RenderGraph::RenderPass> RaytracingDemoPasses::Builder::CreateLightingCompositePass(
    const RaytracingDemoPassResources& resources,
    const RaytracingDemoPassConfig& config)
{
    using namespace RenderGraph;

    return RenderPass::Create(
        L"Lighting Composite",
        {
            { DemoResourceIds::DirectLightingFinishedToken, InputType::Token },
            { DemoResourceIds::IndirectLightingFinishedToken, InputType::Token },
            { DemoResourceIds::DirectLighting, InputType::ShaderResource },
            { DemoResourceIds::IndirectLighting, InputType::ShaderResource },
            { DemoResourceIds::GBufferAlbedoOcclusion, InputType::ShaderResource },
            { DemoResourceIds::GBufferSpecularSmoothness, InputType::ShaderResource },
            { DemoResourceIds::GBufferNormal, InputType::ShaderResource },
            { DemoResourceIds::GBufferEmissionMetallic, InputType::ShaderResource },
            { DemoResourceIds::GBufferPosition, InputType::ShaderResource },
            { DemoResourceIds::DepthBuffer, InputType::ShaderResource },
        },
        {
            { DemoResourceIds::SceneColor, OutputType::UnorderedAccess },
            { DemoResourceIds::HistoryColor, OutputType::UnorderedAccess },
            { DemoResourceIds::NoisyRadiance, OutputType::UnorderedAccess },
            { DemoResourceIds::NRDNoisyRadiance, OutputType::UnorderedAccess },
            { DemoResourceIds::RayTracingFinishedToken, OutputType::Token },
        },
        [resources, config](const RenderContext& context, CommandList& cmd)
        {
            const RaytracingDemoRenderGraph::FrameGBufferResources gbuffer = RaytracingDemoRenderGraph::GetFrameGBufferResources(context);
            const RaytracingDemoCameraConstants camera = RaytracingDemoPassBindings::BuildPassCameraConstants(resources, config, context);
            RaytracingDemoPassBindings::BindCompositeInputs(resources, cmd, context, gbuffer, camera);
//Modify Begin:2026-07-28 by BestHui
            ComputeShader& compositeShader = resources.Pipelines.GetLightingCompositeShader();
            CommandContext commandContext(cmd);
            commandContext.BindPipeline(compositeShader);
            commandContext.BindDescriptorSet(compositeShader.GetDescriptorSet());
            commandContext.Dispatch(Math::DivideByMultiple(camera.Width, 8u), Math::DivideByMultiple(camera.Height, 8u), 1u);
//Modify End
        });
}
//Modify End
