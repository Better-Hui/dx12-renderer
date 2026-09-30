//Modify Begin:2026-09-28 by Hui
#include "RenderGraphBarrierRecorder.h"

#include <DX12Library/Helpers.h>
#include <DX12Library/Resource.h>

#include "RenderPassContext.h"

namespace RenderGraph
{
    void RenderGraphBarrierRecorder::Use(
        const Resource& resource,
        const D3D12_RESOURCE_STATES stateAfter,
        const ResourceUse use,
        const bool insertUavBarrier)
    {
        resource.ForEachResourceRecursive(
            [this, stateAfter, use, insertUavBarrier](const Resource& nestedResource)
            {
                m_PassContext.Use(nestedResource, stateAfter, use, insertUavBarrier);
            });
    }

    void RenderGraphBarrierRecorder::Uav(const Resource& resource)
    {
        BarrierContext& barrierContext = m_PassContext.GetBarrierContext();
        resource.ForEachResourceRecursive(
            [&barrierContext](const Resource& nestedResource)
            {
                barrierContext.Uav(nestedResource);
            });
    }

    void RenderGraphBarrierRecorder::AliasingBeforeFirstUse(const Resource& resource)
    {
        BarrierContext& barrierContext = m_PassContext.GetBarrierContext();
        resource.ForEachResourceRecursive(
            [&barrierContext](const Resource& nestedResource)
            {
                barrierContext.AliasingBeforeFirstUse(nestedResource);
            });
    }

    void RenderGraphBarrierRecorder::Flush()
    {
        m_PassContext.GetBarrierContext().Flush();
    }

    void RenderGraphBarrierRecorder::Use(
        CommandList& commandList,
        const Resource& resource,
        const D3D12_RESOURCE_STATES stateAfter,
        const ResourceUse use,
        const bool insertUavBarrier)
    {
        RenderPassContext passContext(commandList);
        RenderGraphBarrierRecorder recorder(passContext);
        recorder.Use(resource, stateAfter, use, insertUavBarrier);
        passContext.Finish();
    }

    void RenderGraphBarrierRecorder::AliasingBeforeFirstUse(
        CommandList& commandList,
        const Resource& resource)
    {
        RenderPassContext passContext(commandList);
        RenderGraphBarrierRecorder recorder(passContext);
        recorder.AliasingBeforeFirstUse(resource);
        passContext.Finish();
    }

    void RenderGraphBarrierRecorder::Flush(CommandList& commandList)
    {
        RenderPassContext passContext(commandList);
        passContext.Finish();
    }
}
//Modify End
