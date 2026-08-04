#pragma once

#include <RenderGraph/RaytracingDemoGraphResources.h>
//Modify Begin:2026-07-30 by BestHui
#include <Passes/RaytracingDemoPassResources.h>
//Modify End

class CommandList;
class ComputeShader;
class RayTracingBindingSet;

namespace RenderGraph
{
    struct RenderContext;
}

struct RaytracingDemoPassBindings
{
    static RaytracingDemoCameraConstants BuildPassCameraConstants(
        const RaytracingDemoPassResources& resources,
        const RaytracingDemoPassConfig& config,
        const RenderGraph::RenderContext& context);

    static void BindInlinePathTracingInputs(
        const RaytracingDemoPassResources& resources,
        CommandList& cmd,
        ComputeShader& shader,
        const RaytracingDemoRenderGraph::FrameGBufferResources& gbuffer,
        const RaytracingDemoCameraConstants& camera);

    static void BindDxrPathTracingInputs(
        const RaytracingDemoPassResources& resources,
        RayTracingBindingSet& shader,
        const RaytracingDemoRenderGraph::FrameGBufferResources& gbuffer,
        const RaytracingDemoCameraConstants& camera);

    static void BindCompositeInputs(
        const RaytracingDemoPassResources& resources,
        CommandList& cmd,
        const RenderGraph::RenderContext& context,
        const RaytracingDemoRenderGraph::FrameGBufferResources& gbuffer,
        const RaytracingDemoCameraConstants& camera);
};
