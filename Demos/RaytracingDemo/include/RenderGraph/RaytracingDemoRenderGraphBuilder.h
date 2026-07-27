//Modify Begin:2026-07-27 by BestHui
#pragma once

#include <memory>

#include <RenderGraph/RenderGraphRoot.h>

class CommandList;
class RaytracingDemo;

class RaytracingDemoRenderGraphBuilder final
{
public:
    static std::unique_ptr<RenderGraph::RenderGraphRoot> Create(RaytracingDemo& demo, CommandList& commandList);
};
//Modify End
