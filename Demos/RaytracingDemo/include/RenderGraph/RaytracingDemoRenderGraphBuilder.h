//Modify Begin:2026-07-27 by BestHui
#pragma once

#include <memory>

#include <RenderGraph/RenderGraphRoot.h>

class RaytracingDemo;

class RaytracingDemoRenderGraphBuilder final
{
public:
    static std::unique_ptr<RenderGraph::RenderGraphRoot> Create(RaytracingDemo& demo);
};
//Modify End
