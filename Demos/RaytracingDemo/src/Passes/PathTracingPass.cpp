#include <Passes/RaytracingDemoPasses.h>

#include <PathTracing/PathTracingSceneBindings.h>
#include <RenderGraph/RaytracingDemoGraphResources.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/Texture/UnorderedAccessView.h>
#include <RenderGraph/RenderGraphBuilder.h>
//Modify Begin:2026-07-30 by Hui
#include <Scene/SceneLightManager.h>
//Modify End

#include <string_view>
#include <vector>

namespace
{
    using DemoResourceIds = RaytracingDemoRenderGraph::ResourceIds;

//Modify Begin:2026-08-18 by Hui
    struct PathTracingLightingPassData
    {
        RaytracingDemoPassResourcesSnapshot Resources;
        RaytracingDemoPassConfig Config = {};
        PathTracingBackend Backend = PathTracingBackend::InlineRayQuery;
    };

    struct LightingCompositePassData
    {
        RaytracingDemoPassResourcesSnapshot Resources;
        RaytracingDemoPassConfig Config = {};
        PathTracingCompositeFeatures Features = {};
    };
//Modify End

//Modify Begin:2026-07-27 by Hui
    void DispatchDxrLightingPass(
        CommandList& cmd,
//Modify Begin:2026-07-30 by Hui
        BindlessDescriptorHeap& bindlessDescriptorHeap,
//Modify End
        RayTracingShader& shader,
        RayTracingBindingSet& bindingSet,
        const std::string_view passName,
        const uint32_t width,
        const uint32_t height)
    {
//Modify Begin:2026-07-29 by Hui
        CommandContext commandContext(cmd);
//Modify Begin:2026-07-30 by Hui
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

//Modify Begin:2026-07-30 by Hui
void RaytracingDemoPasses::Builder::AddDirectLightingPass(
    RenderGraph::RenderGraphBuilder& renderGraphBuilder,
    const RaytracingDemoPassResources& resources,
    const RaytracingDemoPassConfig& config)
{
    using namespace RenderGraph;
    const PathTracingBackend backend = config.FrameState->Backend;
    renderGraphBuilder.AddPass<PathTracingLightingPassData>(
        L"Direct Lighting",
        [&resources, config, backend](RenderGraphPassBuilder& passBuilder, PathTracingLightingPassData& passData)
        {
            passData.Resources.emplace(resources);
            passData.Config = config;
            passData.Backend = backend;
            passBuilder.ReadToken(DemoResourceIds::BaseResourcesFinishedToken);
            const auto readGBuffer = [&passBuilder, backend](const ResourceId resourceId)
            {
                if (backend == PathTracingBackend::InlineRayQuery)
                {
                    passBuilder.ReadBuffer(resourceId);
                }
                else
                {
                    passBuilder.ReadTexture(resourceId);
                }
            };
            readGBuffer(DemoResourceIds::GBufferAlbedoOcclusion);
            readGBuffer(DemoResourceIds::GBufferSpecularSmoothness);
            readGBuffer(DemoResourceIds::GBufferNormal);
            readGBuffer(DemoResourceIds::GBufferEmissionMetallic);
            readGBuffer(DemoResourceIds::GBufferPosition);
            readGBuffer(DemoResourceIds::DepthBuffer);
            passBuilder.WriteUav(DemoResourceIds::DirectLighting);
            passBuilder.WriteToken(DemoResourceIds::DirectLightingFinishedToken);
            RaytracingDemoPassBindings::DeclareRayTracingExternalResourceAccesses(
                passBuilder,
                resources,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            passBuilder.SetParallelRecordingEligible(backend == PathTracingBackend::InlineRayQuery);
        },
        [](const PathTracingLightingPassData& passData, const RenderContext& context, CommandList& cmd)
        {
            const RaytracingDemoPassResources& resources = passData.Resources.value();
            const RaytracingDemoPassConfig& config = passData.Config;
            const PathTracingBackend backend = passData.Backend;
            const RaytracingDemoRenderGraph::FrameGBufferResources gbuffer = RaytracingDemoRenderGraph::GetFrameGBufferResources(context);
            const RaytracingDemoCameraConstants camera = RaytracingDemoPassBindings::BuildPassCameraConstants(resources, config, context);

            if (backend == PathTracingBackend::InlineRayQuery)
            {
                ComputeShader& directLightingShader = resources.Pipelines.GetInlineDirectLightingShader();
                CommandContext commandContext(cmd);
                RaytracingDemoPassBindings::BindInlinePathTracingInputs(resources, commandContext, directLightingShader, gbuffer, camera);
                commandContext.SetUnorderedAccessView(directLightingShader, "DirectLighting", UnorderedAccessView(context.GetTexture(DemoResourceIds::DirectLighting)));
//Modify Begin:2026-07-30 by Hui
                commandContext.BindBindlessDescriptorHeap(resources.Scene.GetBindlessDescriptorHeap());
//Modify End
                commandContext.BindPipeline(directLightingShader);
                commandContext.BindDescriptorSet(directLightingShader.GetDescriptorSet());
                commandContext.Dispatch(Math::DivideByMultiple(camera.Width, 8u), Math::DivideByMultiple(camera.Height, 8u), 1u);
//Modify End
            }
            else
            {
                RayTracingBindingSet& directBindingSet = resources.Pipelines.GetDirectRayTracingBindingSet();
                directBindingSet.SetUnorderedAccessView("DirectLighting", UnorderedAccessView(context.GetTexture(DemoResourceIds::DirectLighting)));
                RaytracingDemoPassBindings::BindDxrPathTracingInputs(resources, directBindingSet, gbuffer, camera);
//Modify Begin:2026-07-27 by Hui
                DispatchDxrLightingPass(
                    cmd,
//Modify Begin:2026-07-30 by Hui
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

void RaytracingDemoPasses::Builder::AddIndirectLightingPass(
    RenderGraph::RenderGraphBuilder& renderGraphBuilder,
    const RaytracingDemoPassResources& resources,
    const RaytracingDemoPassConfig& config)
{
    using namespace RenderGraph;
//Modify Begin:2026-08-03 by Hui
    const PathTracingBackend backend = config.FrameState->Backend;
    const bool useAsyncCompute =
        config.FrameState->AsyncComputeEnabled && backend == PathTracingBackend::InlineRayQuery;
//Modify End

    renderGraphBuilder.AddPass<PathTracingLightingPassData>(
        L"Indirect Lighting",
        [&resources, config, backend, useAsyncCompute](RenderGraphPassBuilder& passBuilder, PathTracingLightingPassData& passData)
        {
            passData.Resources.emplace(resources);
            passData.Config = config;
            passData.Backend = backend;
            passBuilder.ReadToken(DemoResourceIds::BaseResourcesFinishedToken);
            const auto readGBuffer = [&passBuilder, backend](const ResourceId resourceId)
            {
                if (backend == PathTracingBackend::InlineRayQuery)
                {
                    passBuilder.ReadBuffer(resourceId);
                }
                else
                {
                    passBuilder.ReadTexture(resourceId);
                }
            };
            readGBuffer(DemoResourceIds::GBufferAlbedoOcclusion);
            readGBuffer(DemoResourceIds::GBufferSpecularSmoothness);
            readGBuffer(DemoResourceIds::GBufferNormal);
            readGBuffer(DemoResourceIds::GBufferEmissionMetallic);
            readGBuffer(DemoResourceIds::GBufferPosition);
            readGBuffer(DemoResourceIds::DepthBuffer);
            passBuilder.WriteUav(DemoResourceIds::IndirectLighting);
            passBuilder.WriteToken(DemoResourceIds::IndirectLightingFinishedToken);
            if (useAsyncCompute)
            {
                passBuilder.UseAsyncComputeWhenSupported();
            }
            passBuilder.SetParallelRecordingEligible(
                !useAsyncCompute && backend == PathTracingBackend::InlineRayQuery);
            RaytracingDemoPassBindings::DeclareRayTracingExternalResourceAccesses(
                passBuilder,
                resources,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        },
        [](const PathTracingLightingPassData& passData, const RenderContext& context, CommandList& cmd)
        {
            const RaytracingDemoPassResources& resources = passData.Resources.value();
            const RaytracingDemoPassConfig& config = passData.Config;
            const PathTracingBackend backend = passData.Backend;
            const RaytracingDemoRenderGraph::FrameGBufferResources gbuffer = RaytracingDemoRenderGraph::GetFrameGBufferResources(context);
            const RaytracingDemoCameraConstants camera = RaytracingDemoPassBindings::BuildPassCameraConstants(resources, config, context);

            if (backend == PathTracingBackend::InlineRayQuery)
            {
                ComputeShader& indirectLightingShader = resources.Pipelines.GetInlineIndirectLightingShader();
                CommandContext commandContext(cmd);
                RaytracingDemoPassBindings::BindInlinePathTracingInputs(resources, commandContext, indirectLightingShader, gbuffer, camera);
                commandContext.SetUnorderedAccessView(indirectLightingShader, "IndirectLighting", UnorderedAccessView(context.GetTexture(DemoResourceIds::IndirectLighting)));
//Modify Begin:2026-07-30 by Hui
                commandContext.BindBindlessDescriptorHeap(resources.Scene.GetBindlessDescriptorHeap());
//Modify End
                commandContext.BindPipeline(indirectLightingShader);
                commandContext.BindDescriptorSet(indirectLightingShader.GetDescriptorSet());
                commandContext.Dispatch(Math::DivideByMultiple(camera.Width, 8u), Math::DivideByMultiple(camera.Height, 8u), 1u);
//Modify End
            }
            else
            {
                RayTracingBindingSet& indirectBindingSet = resources.Pipelines.GetIndirectRayTracingBindingSet();
                indirectBindingSet.SetUnorderedAccessView("IndirectLighting", UnorderedAccessView(context.GetTexture(DemoResourceIds::IndirectLighting)));
                RaytracingDemoPassBindings::BindDxrPathTracingInputs(resources, indirectBindingSet, gbuffer, camera);
//Modify Begin:2026-07-27 by Hui
                DispatchDxrLightingPass(
                    cmd,
//Modify Begin:2026-07-30 by Hui
                    resources.Scene.GetBindlessDescriptorHeap(),
//Modify End
                    resources.Pipelines.GetRayTracingShader(),
                    indirectBindingSet,
                    "IndirectLightingRayGen",
                    camera.Width,
                    camera.Height);
//Modify End
            }
        });
}

void RaytracingDemoPasses::Builder::AddLightingCompositePass(
    RenderGraph::RenderGraphBuilder& renderGraphBuilder,
    const RaytracingDemoPassResources& resources,
    const RaytracingDemoPassConfig& config)
{
    using namespace RenderGraph;
    const PathTracingCompositeFeatures features = config.FrameState->GetCompositeFeatures();
    renderGraphBuilder.AddPass<LightingCompositePassData>(
        L"Lighting Composite",
        [&resources, config, features](RenderGraphPassBuilder& passBuilder, LightingCompositePassData& passData)
        {
            passData.Resources.emplace(resources);
            passData.Config = config;
            passData.Features = features;
            passBuilder.ReadTexture(DemoResourceIds::GBufferAlbedoOcclusion);
            passBuilder.ReadTexture(DemoResourceIds::GBufferSpecularSmoothness);
            passBuilder.ReadTexture(DemoResourceIds::GBufferNormal);
            passBuilder.ReadTexture(DemoResourceIds::GBufferEmissionMetallic);
            passBuilder.ReadTexture(DemoResourceIds::GBufferPosition);
            passBuilder.ReadTexture(DemoResourceIds::DepthBuffer);
            if (features.DirectLightingEnabled)
            {
                passBuilder.ReadToken(DemoResourceIds::DirectLightingFinishedToken);
                passBuilder.ReadTexture(DemoResourceIds::DirectLighting);
            }
            if (features.IndirectLightingEnabled)
            {
                passBuilder.ReadToken(DemoResourceIds::IndirectLightingFinishedToken);
                passBuilder.ReadTexture(DemoResourceIds::IndirectLighting);
            }
            passBuilder.WriteUav(DemoResourceIds::SceneColor);
            passBuilder.WriteToken(DemoResourceIds::RayTracingFinishedToken);
            if (features.AccumulationEnabled)
            {
                passBuilder.WriteUav(DemoResourceIds::HistoryColor);
            }
            if (features.DenoiserMode == static_cast<uint32_t>(DenoiserController::Algorithm::SVGF))
            {
                passBuilder.WriteUav(DemoResourceIds::NoisyRadiance);
            }
            if (features.DenoiserMode == static_cast<uint32_t>(DenoiserController::Algorithm::NRD))
            {
                passBuilder.WriteUav(DemoResourceIds::NRDNoisyRadiance);
            }
            passBuilder.SetParallelRecordingEligible(
                config.FrameState->Backend == PathTracingBackend::InlineRayQuery);
        },
        [](const LightingCompositePassData& passData, const RenderContext& context, CommandList& cmd)
        {
            const RaytracingDemoPassResources& resources = passData.Resources.value();
            const RaytracingDemoPassConfig& config = passData.Config;
            const PathTracingCompositeFeatures& features = passData.Features;
            const RaytracingDemoRenderGraph::FrameGBufferResources gbuffer = RaytracingDemoRenderGraph::GetFrameGBufferResources(context);
            const RaytracingDemoCameraConstants camera = RaytracingDemoPassBindings::BuildPassCameraConstants(resources, config, context);
            ComputeShader& compositeShader = RaytracingDemoPassBindings::BindCompositeInputs(
                resources,
                cmd,
                context,
                gbuffer,
                camera,
                features);
//Modify Begin:2026-07-28 by Hui
            CommandContext commandContext(cmd);
            commandContext.BindPipeline(compositeShader);
            commandContext.BindDescriptorSet(compositeShader.GetDescriptorSet());
            commandContext.Dispatch(Math::DivideByMultiple(camera.Width, 8u), Math::DivideByMultiple(camera.Height, 8u), 1u);
//Modify End
        });
}
//Modify End
