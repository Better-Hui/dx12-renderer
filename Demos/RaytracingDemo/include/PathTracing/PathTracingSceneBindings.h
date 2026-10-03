#pragma once

#include <RenderGraph/RaytracingDemoGraphResources.h>
//Modify Begin:2026-07-30 by Hui
#include <Passes/RaytracingDemoPassResources.h>
//Modify End

class CommandList;
class CommandContext;
class ComputeShader;
class ByteAddressBuffer;
class RayTracingBindingSet;
class StructuredBuffer;

namespace RenderGraph
{
    class FrameContext;
}

struct RaytracingDemoPassBindings
{
    static RaytracingDemoCameraConstants BuildPassCameraConstants(
        const RaytracingDemoPassResources& resources,
        const RaytracingDemoPassConfig& config,
        const RenderGraph::FrameContext& context);

    static void BindBlueNoiseInputs(
        const RaytracingDemoPassResources& resources,
        CommandContext& commandContext,
        ComputeShader& shader);

    static void BindBlueNoiseInputs(
        const RaytracingDemoPassResources& resources,
        RayTracingBindingSet& shader);

    static void BindInlinePathTracingInputs(
        const RaytracingDemoPassResources& resources,
        CommandContext& commandContext,
        ComputeShader& shader,
        const RaytracingDemoRenderGraph::FrameGBufferResources& gbuffer,
        const RaytracingDemoCameraConstants& camera,
        const StructuredBuffer* activeRayPixelIndices = nullptr,
        const ByteAddressBuffer* activeRayPixelCount = nullptr);

    static void BindDxrPathTracingInputs(
        const RaytracingDemoPassResources& resources,
        RayTracingBindingSet& shader,
        const RaytracingDemoRenderGraph::FrameGBufferResources& gbuffer,
        const RaytracingDemoCameraConstants& camera,
        const StructuredBuffer* activeRayPixelIndices = nullptr);

    static ComputeShader& BindCompositeInputs(
        const RaytracingDemoPassResources& resources,
        CommandList& cmd,
        const RenderGraph::FrameContext& context,
        const RaytracingDemoRenderGraph::FrameGBufferResources& gbuffer,
        const RaytracingDemoCameraConstants& camera,
        const PathTracingCompositeFeatures& features);
};
