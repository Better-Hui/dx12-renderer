#pragma once

#include <RenderGraph/RaytracingDemoGraphResources.h>
#include <RaytracingDemo.h>

class CommandList;
class ComputeShader;
class RayTracingBindingSet;

namespace RenderGraph
{
    struct RenderContext;
}

struct RaytracingDemoPassAccess
{
    static RaytracingDemo::CameraConstants BuildPassCameraConstants(
        RaytracingDemo& demo,
        const RenderGraph::RenderContext& context);

    static void BindInlinePathTracingInputs(
        RaytracingDemo& demo,
        CommandList& cmd,
        ComputeShader& shader,
        const RaytracingDemoRenderGraph::FrameGBufferResources& gbuffer,
        const RaytracingDemo::CameraConstants& camera);

    static void BindDxrPathTracingInputs(
        RaytracingDemo& demo,
        RayTracingBindingSet& shader,
        const RaytracingDemoRenderGraph::FrameGBufferResources& gbuffer,
        const RaytracingDemo::CameraConstants& camera);

    static void BindCompositeInputs(
        RaytracingDemo& demo,
        CommandList& cmd,
        const RenderGraph::RenderContext& context,
        const RaytracingDemoRenderGraph::FrameGBufferResources& gbuffer,
        const RaytracingDemo::CameraConstants& camera);
};
