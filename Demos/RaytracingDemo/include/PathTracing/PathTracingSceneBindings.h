#pragma once

#include <RenderGraph/RaytracingDemoGraphResources.h>
//Modify Begin:2026-07-30 by BestHui
#include <Passes/RaytracingDemoPassResources.h>
//Modify End

class CommandList;
class CommandContext;
class ComputeShader;
class RayTracingBindingSet;

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

    static void BindInlinePathTracingInputs(
        const RaytracingDemoPassResources& resources,
        CommandContext& commandContext,
        ComputeShader& shader,
        const RaytracingDemoRenderGraph::FrameGBufferResources& gbuffer,
        const RaytracingDemoCameraConstants& camera);

    static void BindDxrPathTracingInputs(
        const RaytracingDemoPassResources& resources,
        RayTracingBindingSet& shader,
        const RaytracingDemoRenderGraph::FrameGBufferResources& gbuffer,
        const RaytracingDemoCameraConstants& camera);

    static ComputeShader& BindCompositeInputs(
        const RaytracingDemoPassResources& resources,
        CommandList& cmd,
        const RenderGraph::FrameContext& context,
        const RaytracingDemoRenderGraph::FrameGBufferResources& gbuffer,
        const RaytracingDemoCameraConstants& camera);
};
