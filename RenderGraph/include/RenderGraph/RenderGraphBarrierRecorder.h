//Modify Begin:2026-09-28 by Hui
#pragma once

#include <d3d12.h>

#include <DX12Library/BarrierContext.h>

class CommandList;
class Resource;
class BarrierContext;
namespace RenderGraph { class RenderPassContext; }

namespace RenderGraph
{
    // RenderGraph owns pass-boundary barrier decisions and recording. The DX12
    // library remains responsible for queueing native barriers and committing
    // resource states when a command list is submitted.
    class RenderGraphBarrierRecorder final
    {
    public:
        explicit RenderGraphBarrierRecorder(RenderPassContext& passContext) noexcept
            : m_PassContext(passContext)
        {
        }

        void Use(
            const Resource& resource,
            D3D12_RESOURCE_STATES stateAfter,
            ResourceUse use,
            bool insertUavBarrier = false);

        void Uav(const Resource& resource);
        void AliasingBeforeFirstUse(const Resource& resource);
        void Flush();

        // Utility recording outside a graph pass still goes through the same
        // RenderPassContext/BarrierContext path.
        static void Use(
            CommandList& commandList,
            const Resource& resource,
            D3D12_RESOURCE_STATES stateAfter,
            ResourceUse use = ResourceUse::Read,
            bool insertUavBarrier = false);
        static void AliasingBeforeFirstUse(CommandList& commandList, const Resource& resource);
        static void Flush(CommandList& commandList);

        RenderPassContext& GetPassContext() const noexcept { return m_PassContext; }

    private:
        RenderPassContext& m_PassContext;
    };
}
//Modify End
