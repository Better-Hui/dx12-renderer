#include <Passes/RaytracingDemoPasses.h>

#include <PathTracing/PathTracingSceneBindings.h>
#include <RenderGraph/RaytracingDemoGraphResources.h>

#include <DX12Library/CommandList.h>
//Modify Begin:2026-08-20 by Hui
#include <DX12Library/ByteAddressBuffer.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/StructuredBuffer.h>
#include <Framework/Rendering/Lighting/ActivePixelListController.h>
//Modify End
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>
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

//Modify Begin:2026-08-19 by Hui
    struct PathTracingLightingPassData
    {
        RaytracingDemoPassResourcesSnapshot Resources;
        RaytracingDemoPassConfig Config = {};
        PathTracingBackend Backend = PathTracingBackend::InlineRayQuery;
        PathTracingDispatchMode DispatchMode = PathTracingDispatchMode::FullResolution;
    };

    struct PathTracingActivePixelCompactionPassData
    {
        RaytracingDemoPassResourcesSnapshot Resources;
        RaytracingDemoPassConfig Config = {};
    };

    struct PathTracingActiveRayCountReadbackPassData
    {
        RaytracingDemoPassResourcesSnapshot Resources;
    };

    struct PathTracingDxrCompactedDispatchTemplatePassData
    {
        RaytracingDemoPassResourcesSnapshot Resources;
        bool PrepareDirectLighting = false;
        bool PrepareIndirectLighting = false;
    };

    struct PathTracingCompactDispatchFinalizePassData
    {
        RaytracingDemoPassResourcesSnapshot Resources;
        PathTracingBackend Backend = PathTracingBackend::InlineRayQuery;
        bool PrepareDirectLighting = false;
        bool PrepareIndirectLighting = false;
        bool PrepareComputeDispatch = false;
    };

    struct LightingCompositePassData
    {
        RaytracingDemoPassResourcesSnapshot Resources;
        RaytracingDemoPassConfig Config = {};
        PathTracingCompositeFeatures Features = {};
    };
//Modify End

//Modify Begin:2026-07-30 by Hui
    void DispatchDxrLightingPass(
        CommandList& cmd,
        BindlessDescriptorHeap& bindlessDescriptorHeap,
        RayTracingShader& shader,
        RayTracingBindingSet& bindingSet,
        const PathTracingIndirectDispatch& indirectDispatch)
    {
        CommandContext commandContext(cmd);
        commandContext.BindBindlessDescriptorHeap(bindlessDescriptorHeap);
        commandContext.BindPipeline(shader);
        commandContext.BindDescriptorSet(bindingSet);
        commandContext.DispatchRaysIndirect(
            indirectDispatch.Signature,
            IndirectCommandExecutionDesc{ .ArgumentBuffer = &indirectDispatch.Arguments });
        commandContext.InsertDescriptorSetOutputBarriers(bindingSet);
    }
//Modify End
}

//Modify Begin:2026-08-20 by Hui
void RaytracingDemoPasses::Builder::AddActivePixelCompactionPasses(
    RenderGraph::RenderGraphBuilder& renderGraphBuilder,
    const RaytracingDemoPassResources& resources,
    const RaytracingDemoPassConfig& config,
    const bool preparePathTracingDirectLighting,
    const bool preparePathTracingIndirectLighting,
    const bool prepareComputeDispatch)
{
    using namespace RenderGraph;
    const PathTracingBackend backend = config.FrameState->Backend;
    Assert(
        config.FrameState->DispatchMode == PathTracingDispatchMode::CompactedIndirect,
        "Active-pixel compaction requires compacted indirect dispatch mode.");

    renderGraphBuilder.AddPass<PathTracingActivePixelCompactionPassData>(
        L"Active Ray-Traced Pixel Compaction",
        [&resources, config](
            RenderGraphPassBuilder& passBuilder,
            PathTracingActivePixelCompactionPassData& passData)
        {
            passData.Resources.emplace(resources);
            passData.Config = config;
            passBuilder.ReadToken(DemoResourceIds::BaseResourcesFinishedToken);
            passBuilder.ReadTexture(DemoResourceIds::DepthBuffer);
            passBuilder.WriteUav(DemoResourceIds::ActiveRayPixelIndices);
            passBuilder.WriteUav(DemoResourceIds::ActiveRayPixelCount);
            passBuilder.WriteToken(DemoResourceIds::ActiveRayPixelCompactionFinishedToken);
        },
        [](const PathTracingActivePixelCompactionPassData& passData, const RenderContext& context, CommandList& cmd)
        {
            const RaytracingDemoPassResources& resources = passData.Resources.value();
            ComputeShader& shader = resources.ActivePixels.GetCompactionShader();
            CommandContext commandContext(cmd);
            commandContext.BindBindlessDescriptorHeap(resources.Scene.GetBindlessDescriptorHeap());
            const UINT clearValues[4] = { 0u, 0u, 0u, 0u };
            commandContext.ClearUnorderedAccessUint(
                context.GetResource(DemoResourceIds::ActiveRayPixelCount),
                clearValues);
            const ActivePixelCompactionConstants compactionConstants = {
                .Width = context.GetMetadata().m_ScreenWidth,
                .Height = context.GetMetadata().m_ScreenHeight,
            };
            commandContext.SetConstantBuffer(
                shader,
                "ActivePixelCompactionConstants",
                compactionConstants);
            commandContext.SetTexture(
                shader,
                "DepthTexture",
                ShaderResourceView::DepthAsFloat(context.GetTexture(DemoResourceIds::DepthBuffer)));
            commandContext.SetUnorderedAccessView(
                shader,
                "ActivePixelIndices",
                UnorderedAccessView(context.GetBuffer(DemoResourceIds::ActiveRayPixelIndices)));
            commandContext.SetUnorderedAccessView(
                shader,
                "ActivePixelCount",
                UnorderedAccessView(context.GetBuffer(DemoResourceIds::ActiveRayPixelCount)));
            commandContext.BindPipeline(shader);
            commandContext.BindDescriptorSet(shader.GetDescriptorSet());
            commandContext.Dispatch(
                Math::DivideByMultiple(context.GetMetadata().m_ScreenWidth, 8u),
                Math::DivideByMultiple(context.GetMetadata().m_ScreenHeight, 8u));
            commandContext.InsertDescriptorSetOutputBarriers(shader.GetDescriptorSet());
        });

    if (backend == PathTracingBackend::ShaderTableDxr)
    {
        renderGraphBuilder.AddPass<PathTracingDxrCompactedDispatchTemplatePassData>(
            L"Path Tracing DXR Compact Dispatch Template",
            [&resources, preparePathTracingDirectLighting, preparePathTracingIndirectLighting](
                RenderGraphPassBuilder& passBuilder,
                PathTracingDxrCompactedDispatchTemplatePassData& passData)
            {
                passData.Resources.emplace(resources);
                passData.PrepareDirectLighting = preparePathTracingDirectLighting;
                passData.PrepareIndirectLighting = preparePathTracingIndirectLighting;
                passBuilder.ReadToken(DemoResourceIds::BaseResourcesFinishedToken);
                if (preparePathTracingDirectLighting)
                {
                    passBuilder.WriteExternal(
                        resources.Pipelines.GetCompactedIndirectDispatch(PathTracingBackend::ShaderTableDxr, true).Arguments,
                        D3D12_RESOURCE_STATE_COPY_DEST);
                }
                if (preparePathTracingIndirectLighting)
                {
                    passBuilder.WriteExternal(
                        resources.Pipelines.GetCompactedIndirectDispatch(PathTracingBackend::ShaderTableDxr, false).Arguments,
                        D3D12_RESOURCE_STATE_COPY_DEST);
                }
                passBuilder.WriteToken(DemoResourceIds::DxrCompactedDispatchTemplateFinishedToken);
            },
            [](const PathTracingDxrCompactedDispatchTemplatePassData& passData, const RenderContext& context, CommandList& cmd)
            {
                const RaytracingDemoPassResources& resources = passData.Resources.value();
                if (passData.PrepareDirectLighting)
                {
                    resources.Pipelines.PrepareDxrCompactedDispatchTemplate(
                        cmd,
                        context.GetMetadata().m_ScreenWidth,
                        context.GetMetadata().m_ScreenHeight,
                        true);
                }
                if (passData.PrepareIndirectLighting)
                {
                    resources.Pipelines.PrepareDxrCompactedDispatchTemplate(
                        cmd,
                        context.GetMetadata().m_ScreenWidth,
                        context.GetMetadata().m_ScreenHeight,
                        false);
                }
            });
    }

    renderGraphBuilder.AddPass<PathTracingCompactDispatchFinalizePassData>(
        L"Active Pixel Dispatch Finalize",
        [&resources, backend, preparePathTracingDirectLighting, preparePathTracingIndirectLighting, prepareComputeDispatch](
            RenderGraphPassBuilder& passBuilder,
            PathTracingCompactDispatchFinalizePassData& passData)
        {
            passData.Resources.emplace(resources);
            passData.Backend = backend;
            passData.PrepareDirectLighting = preparePathTracingDirectLighting;
            passData.PrepareIndirectLighting = preparePathTracingIndirectLighting;
            passData.PrepareComputeDispatch = prepareComputeDispatch;
            passBuilder.ReadToken(DemoResourceIds::ActiveRayPixelCompactionFinishedToken);
            if (backend == PathTracingBackend::ShaderTableDxr)
            {
                passBuilder.ReadToken(DemoResourceIds::DxrCompactedDispatchTemplateFinishedToken);
            }
            passBuilder.ReadBuffer(DemoResourceIds::ActiveRayPixelCount);
            passBuilder.WriteUav(DemoResourceIds::ActivePixelDispatchData);
            passBuilder.WriteToken(DemoResourceIds::ActivePixelDispatchFinalizedToken);

            const Resource* declaredArguments = nullptr;
            const auto declareArguments = [&passBuilder, &declaredArguments](const PathTracingIndirectDispatch& indirectDispatch)
            {
                if (&indirectDispatch.Arguments != declaredArguments)
                {
                    passBuilder.WriteExternal(
                        indirectDispatch.Arguments,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        true);
                    declaredArguments = &indirectDispatch.Arguments;
                }
            };
            if (preparePathTracingDirectLighting)
            {
                declareArguments(resources.Pipelines.GetCompactedIndirectDispatch(backend, true));
                passBuilder.WriteToken(DemoResourceIds::DirectLightingIndirectArgumentsReadyToken);
            }
            if (preparePathTracingIndirectLighting)
            {
                declareArguments(resources.Pipelines.GetCompactedIndirectDispatch(backend, false));
                passBuilder.WriteToken(DemoResourceIds::IndirectLightingIndirectArgumentsReadyToken);
            }
            if (prepareComputeDispatch)
            {
                passBuilder.WriteToken(DemoResourceIds::ActivePixelComputeDispatchReadyToken);
            }
        },
        [](const PathTracingCompactDispatchFinalizePassData& passData, const RenderContext& context, CommandList& cmd)
        {
            const RaytracingDemoPassResources& resources = passData.Resources.value();
            const auto finalize = [&resources, &context, &cmd](
                ComputeShader& shader,
                const UnorderedAccessView& indirectArguments,
                const char* countBinding)
            {
                CommandContext commandContext(cmd);
                commandContext.BindBindlessDescriptorHeap(resources.Scene.GetBindlessDescriptorHeap());
                commandContext.SetShaderResource(
                    shader,
                    countBinding,
                    0u,
                    context.GetResource(DemoResourceIds::ActiveRayPixelCount));
                commandContext.SetUnorderedAccessView(
                    shader,
                    "IndirectArguments",
                    indirectArguments);
                commandContext.BindPipeline(shader);
                commandContext.BindDescriptorSet(shader.GetDescriptorSet());
                commandContext.Dispatch(1u);
                commandContext.InsertDescriptorSetOutputBarriers(shader.GetDescriptorSet());
            };

            finalize(
                resources.ActivePixels.GetDispatchFinalizeShader(),
                UnorderedAccessView(context.GetBuffer(DemoResourceIds::ActivePixelDispatchData)),
                "ActivePixelCount");

            if (passData.Backend == PathTracingBackend::InlineRayQuery)
            {
                if (passData.PrepareDirectLighting)
                {
                    finalize(
                        resources.Pipelines.GetInlineCompactedDispatchFinalizeShader(),
                        UnorderedAccessView(resources.Pipelines.GetCompactedIndirectDispatch(PathTracingBackend::InlineRayQuery, true).Arguments),
                        "ActiveRayPixelCount");
                }
                if (passData.PrepareIndirectLighting)
                {
                    finalize(
                        resources.Pipelines.GetInlineCompactedDispatchFinalizeShader(),
                        UnorderedAccessView(resources.Pipelines.GetCompactedIndirectDispatch(PathTracingBackend::InlineRayQuery, false).Arguments),
                        "ActiveRayPixelCount");
                }
                return;
            }

            if (passData.PrepareDirectLighting)
            {
                finalize(
                    resources.Pipelines.GetDxrCompactedDispatchFinalizeShader(),
                    UnorderedAccessView(resources.Pipelines.GetCompactedIndirectDispatch(PathTracingBackend::ShaderTableDxr, true).Arguments),
                    "ActiveRayPixelCount");
            }
            if (passData.PrepareIndirectLighting)
            {
                finalize(
                    resources.Pipelines.GetDxrCompactedDispatchFinalizeShader(),
                    UnorderedAccessView(resources.Pipelines.GetCompactedIndirectDispatch(PathTracingBackend::ShaderTableDxr, false).Arguments),
                    "ActiveRayPixelCount");
            }
        });

    renderGraphBuilder.AddPass<PathTracingActiveRayCountReadbackPassData>(
        L"Active Ray-Traced Pixel Dispatch Readback",
        [&resources](
            RenderGraphPassBuilder& passBuilder,
            PathTracingActiveRayCountReadbackPassData& passData)
        {
            passData.Resources.emplace(resources);
            passBuilder.ReadToken(DemoResourceIds::ActivePixelDispatchFinalizedToken);
            passBuilder.ReadCopySource(DemoResourceIds::ActivePixelDispatchData);
            passBuilder.WriteToken(DemoResourceIds::ActiveRayPixelCountReadbackFinishedToken);
        },
        [](const PathTracingActiveRayCountReadbackPassData& passData, const RenderContext& context, CommandList& cmd)
        {
            const RaytracingDemoPassResources& resources = passData.Resources.value();
            static_cast<void>(resources.ActivePixels.RecordCountReadback(
                cmd,
                context.GetResource(DemoResourceIds::ActivePixelDispatchData)));
        });
}
//Modify End

//Modify Begin:2026-08-19 by Hui
void RaytracingDemoPasses::Builder::AddDirectLightingPass(
    RenderGraph::RenderGraphBuilder& renderGraphBuilder,
    const RaytracingDemoPassResources& resources,
    const RaytracingDemoPassConfig& config)
{
    using namespace RenderGraph;
    const PathTracingBackend backend = config.FrameState->Backend;
    const PathTracingDispatchMode dispatchMode = config.FrameState->DispatchMode;
    renderGraphBuilder.AddPass<PathTracingLightingPassData>(
        L"Direct Lighting",
        [&resources, config, backend, dispatchMode](RenderGraphPassBuilder& passBuilder, PathTracingLightingPassData& passData)
        {
            passData.Resources.emplace(resources);
            passData.Config = config;
            passData.Backend = backend;
            passData.DispatchMode = dispatchMode;
            passBuilder.ReadToken(DemoResourceIds::BaseResourcesFinishedToken);
            if (dispatchMode == PathTracingDispatchMode::CompactedIndirect)
            {
                passBuilder.ReadToken(DemoResourceIds::DirectLightingIndirectArgumentsReadyToken);
                passBuilder.ReadIndirectArgument(resources.Pipelines.GetCompactedIndirectDispatch(backend, true).Arguments);
                passBuilder.ReadBuffer(DemoResourceIds::ActiveRayPixelIndices);
                if (backend == PathTracingBackend::InlineRayQuery)
                {
                    passBuilder.ReadBuffer(DemoResourceIds::ActiveRayPixelCount);
                }
            }
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
            const bool compactedDispatch = passData.DispatchMode == PathTracingDispatchMode::CompactedIndirect;
            const RaytracingDemoRenderGraph::FrameGBufferResources gbuffer = RaytracingDemoRenderGraph::GetFrameGBufferResources(context);
            const RaytracingDemoCameraConstants camera = RaytracingDemoPassBindings::BuildPassCameraConstants(resources, config, context);
            const std::shared_ptr<StructuredBuffer> activeRayPixelIndices = compactedDispatch
                ? std::dynamic_pointer_cast<StructuredBuffer>(context.GetBuffer(DemoResourceIds::ActiveRayPixelIndices))
                : nullptr;
            const std::shared_ptr<ByteAddressBuffer> activeRayPixelCount =
                compactedDispatch && backend == PathTracingBackend::InlineRayQuery
                ? std::dynamic_pointer_cast<ByteAddressBuffer>(context.GetBuffer(DemoResourceIds::ActiveRayPixelCount))
                : nullptr;
            if (compactedDispatch)
            {
                Assert(activeRayPixelIndices != nullptr, "Compacted path tracing requires a structured active-pixel index buffer.");
                if (backend == PathTracingBackend::InlineRayQuery)
                {
                    Assert(activeRayPixelCount != nullptr, "Compacted inline path tracing requires a raw active-pixel count buffer.");
                }
            }

            if (backend == PathTracingBackend::InlineRayQuery)
            {
                ComputeShader& directLightingShader = resources.Pipelines.GetInlineDirectLightingShader();
                CommandContext commandContext(cmd);
                if (compactedDispatch)
                {
                    const UINT clearValues[4] = { 0u, 0u, 0u, 0u };
                    commandContext.ClearUnorderedAccessUint(context.GetResource(DemoResourceIds::DirectLighting), clearValues);
                }
                RaytracingDemoPassBindings::BindInlinePathTracingInputs(
                    resources,
                    commandContext,
                    directLightingShader,
                    gbuffer,
                    camera,
                    activeRayPixelIndices.get(),
                    activeRayPixelCount.get());
                commandContext.SetUnorderedAccessView(directLightingShader, "DirectLighting", UnorderedAccessView(context.GetTexture(DemoResourceIds::DirectLighting)));
                commandContext.BindBindlessDescriptorHeap(resources.Scene.GetBindlessDescriptorHeap());
                commandContext.BindPipeline(directLightingShader);
                commandContext.BindDescriptorSet(directLightingShader.GetDescriptorSet());
                if (compactedDispatch)
                {
                    const PathTracingIndirectDispatch indirectDispatch = resources.Pipelines.GetCompactedIndirectDispatch(backend, true);
                    commandContext.DispatchIndirect(
                        indirectDispatch.Signature,
                        IndirectCommandExecutionDesc{ .ArgumentBuffer = &indirectDispatch.Arguments });
                }
                else
                {
                    commandContext.Dispatch(
                        Math::DivideByMultiple(camera.Width, 8u),
                        Math::DivideByMultiple(camera.Height, 8u));
                }
            }
            else
            {
                RayTracingBindingSet& directBindingSet = resources.Pipelines.GetDirectRayTracingBindingSet();
                if (compactedDispatch)
                {
                    CommandContext clearContext(cmd);
                    const UINT clearValues[4] = { 0u, 0u, 0u, 0u };
                    clearContext.ClearUnorderedAccessUint(context.GetResource(DemoResourceIds::DirectLighting), clearValues);
                }
                directBindingSet.SetUnorderedAccessView("DirectLighting", UnorderedAccessView(context.GetTexture(DemoResourceIds::DirectLighting)));
                RaytracingDemoPassBindings::BindDxrPathTracingInputs(
                    resources,
                    directBindingSet,
                    gbuffer,
                    camera,
                    activeRayPixelIndices.get());
                if (compactedDispatch)
                {
                    DispatchDxrLightingPass(
                        cmd,
                        resources.Scene.GetBindlessDescriptorHeap(),
                        resources.Pipelines.GetRayTracingShader(),
                        directBindingSet,
                        resources.Pipelines.GetCompactedIndirectDispatch(backend, true));
                }
                else
                {
                    CommandContext commandContext(cmd);
                    commandContext.BindBindlessDescriptorHeap(resources.Scene.GetBindlessDescriptorHeap());
                    commandContext.BindPipeline(resources.Pipelines.GetRayTracingShader());
                    commandContext.BindDescriptorSet(directBindingSet);
                    commandContext.DispatchRays(RayTracingDispatchDesc{ "DirectLightingRayGen", camera.Width, camera.Height, 1u });
                    commandContext.InsertDescriptorSetOutputBarriers(directBindingSet);
                }
            }
        });
}
//Modify End

//Modify Begin:2026-08-19 by Hui
void RaytracingDemoPasses::Builder::AddIndirectLightingPass(
    RenderGraph::RenderGraphBuilder& renderGraphBuilder,
    const RaytracingDemoPassResources& resources,
    const RaytracingDemoPassConfig& config)
{
    using namespace RenderGraph;
    const PathTracingBackend backend = config.FrameState->Backend;
    const PathTracingDispatchMode dispatchMode = config.FrameState->DispatchMode;
    const bool useAsyncCompute =
        config.FrameState->AsyncComputeEnabled && backend == PathTracingBackend::InlineRayQuery;

    renderGraphBuilder.AddPass<PathTracingLightingPassData>(
        L"Indirect Lighting",
        [&resources, config, backend, dispatchMode, useAsyncCompute](RenderGraphPassBuilder& passBuilder, PathTracingLightingPassData& passData)
        {
            passData.Resources.emplace(resources);
            passData.Config = config;
            passData.Backend = backend;
            passData.DispatchMode = dispatchMode;
            passBuilder.ReadToken(DemoResourceIds::BaseResourcesFinishedToken);
            if (dispatchMode == PathTracingDispatchMode::CompactedIndirect)
            {
                passBuilder.ReadToken(DemoResourceIds::IndirectLightingIndirectArgumentsReadyToken);
                passBuilder.ReadIndirectArgument(resources.Pipelines.GetCompactedIndirectDispatch(backend, false).Arguments);
                passBuilder.ReadBuffer(DemoResourceIds::ActiveRayPixelIndices);
                if (backend == PathTracingBackend::InlineRayQuery)
                {
                    passBuilder.ReadBuffer(DemoResourceIds::ActiveRayPixelCount);
                }
            }
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
            const bool compactedDispatch = passData.DispatchMode == PathTracingDispatchMode::CompactedIndirect;
            const RaytracingDemoRenderGraph::FrameGBufferResources gbuffer = RaytracingDemoRenderGraph::GetFrameGBufferResources(context);
            const RaytracingDemoCameraConstants camera = RaytracingDemoPassBindings::BuildPassCameraConstants(resources, config, context);
            const std::shared_ptr<StructuredBuffer> activeRayPixelIndices = compactedDispatch
                ? std::dynamic_pointer_cast<StructuredBuffer>(context.GetBuffer(DemoResourceIds::ActiveRayPixelIndices))
                : nullptr;
            const std::shared_ptr<ByteAddressBuffer> activeRayPixelCount =
                compactedDispatch && backend == PathTracingBackend::InlineRayQuery
                ? std::dynamic_pointer_cast<ByteAddressBuffer>(context.GetBuffer(DemoResourceIds::ActiveRayPixelCount))
                : nullptr;
            if (compactedDispatch)
            {
                Assert(activeRayPixelIndices != nullptr, "Compacted path tracing requires a structured active-pixel index buffer.");
                if (backend == PathTracingBackend::InlineRayQuery)
                {
                    Assert(activeRayPixelCount != nullptr, "Compacted inline path tracing requires a raw active-pixel count buffer.");
                }
            }

            if (backend == PathTracingBackend::InlineRayQuery)
            {
                ComputeShader& indirectLightingShader = resources.Pipelines.GetInlineIndirectLightingShader();
                CommandContext commandContext(cmd);
                if (compactedDispatch)
                {
                    const UINT clearValues[4] = { 0u, 0u, 0u, 0u };
                    commandContext.ClearUnorderedAccessUint(context.GetResource(DemoResourceIds::IndirectLighting), clearValues);
                }
                RaytracingDemoPassBindings::BindInlinePathTracingInputs(
                    resources,
                    commandContext,
                    indirectLightingShader,
                    gbuffer,
                    camera,
                    activeRayPixelIndices.get(),
                    activeRayPixelCount.get());
                commandContext.SetUnorderedAccessView(indirectLightingShader, "IndirectLighting", UnorderedAccessView(context.GetTexture(DemoResourceIds::IndirectLighting)));
                commandContext.BindBindlessDescriptorHeap(resources.Scene.GetBindlessDescriptorHeap());
                commandContext.BindPipeline(indirectLightingShader);
                commandContext.BindDescriptorSet(indirectLightingShader.GetDescriptorSet());
                if (compactedDispatch)
                {
                    const PathTracingIndirectDispatch indirectDispatch = resources.Pipelines.GetCompactedIndirectDispatch(backend, false);
                    commandContext.DispatchIndirect(
                        indirectDispatch.Signature,
                        IndirectCommandExecutionDesc{ .ArgumentBuffer = &indirectDispatch.Arguments });
                }
                else
                {
                    commandContext.Dispatch(
                        Math::DivideByMultiple(camera.Width, 8u),
                        Math::DivideByMultiple(camera.Height, 8u));
                }
            }
            else
            {
                RayTracingBindingSet& indirectBindingSet = resources.Pipelines.GetIndirectRayTracingBindingSet();
                if (compactedDispatch)
                {
                    CommandContext clearContext(cmd);
                    const UINT clearValues[4] = { 0u, 0u, 0u, 0u };
                    clearContext.ClearUnorderedAccessUint(context.GetResource(DemoResourceIds::IndirectLighting), clearValues);
                }
                indirectBindingSet.SetUnorderedAccessView("IndirectLighting", UnorderedAccessView(context.GetTexture(DemoResourceIds::IndirectLighting)));
                RaytracingDemoPassBindings::BindDxrPathTracingInputs(
                    resources,
                    indirectBindingSet,
                    gbuffer,
                    camera,
                    activeRayPixelIndices.get());
                if (compactedDispatch)
                {
                    DispatchDxrLightingPass(
                        cmd,
                        resources.Scene.GetBindlessDescriptorHeap(),
                        resources.Pipelines.GetRayTracingShader(),
                        indirectBindingSet,
                        resources.Pipelines.GetCompactedIndirectDispatch(backend, false));
                }
                else
                {
                    CommandContext commandContext(cmd);
                    commandContext.BindBindlessDescriptorHeap(resources.Scene.GetBindlessDescriptorHeap());
                    commandContext.BindPipeline(resources.Pipelines.GetRayTracingShader());
                    commandContext.BindDescriptorSet(indirectBindingSet);
                    commandContext.DispatchRays(RayTracingDispatchDesc{ "IndirectLightingRayGen", camera.Width, camera.Height, 1u });
                    commandContext.InsertDescriptorSetOutputBarriers(indirectBindingSet);
                }
            }
        });
}
//Modify End

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
