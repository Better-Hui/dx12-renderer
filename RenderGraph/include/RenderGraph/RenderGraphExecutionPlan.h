//Modify Begin:2026-07-30 by BestHui
#pragma once

#include <d3d12.h>

#include <cstdint>
#include <map>
#include <vector>

#include "RenderTargetInfo.h"
#include "ResourceId.h"

namespace RenderGraph
{
    class RenderGraphCompiler;
    class RenderPass;

    struct PassResourceTransition
    {
        ResourceId Id = 0;
        D3D12_RESOURCE_STATES StateAfter = D3D12_RESOURCE_STATE_COMMON;
        bool InsertUavBarrier = false;
    };

    struct PassResourceStatePlan
    {
        std::vector<PassResourceTransition> InputTransitions;
        std::vector<ResourceId> AliasingOutputs;
        std::vector<PassResourceTransition> OutputTransitions;
        std::vector<ResourceId> InitOutputs;
    };

    struct RenderGraphExecutionBatch
    {
        std::vector<RenderPass*> Passes;
        bool ParallelRecordingEligible = false;
    };

    class CompiledRenderGraph final
    {
    public:
        const std::vector<RenderPass*>& GetRenderPasses() const { return m_RenderPasses; }
        const std::vector<RenderGraphExecutionBatch>& GetExecutionBatches() const { return m_ExecutionBatches; }
        const std::map<const RenderPass*, RenderTargetInfo>& GetRenderTargets() const { return m_RenderTargets; }
        const std::map<const RenderPass*, PassResourceStatePlan>& GetResourceStatePlans() const { return m_ResourceStatePlans; }

    private:
        friend class RenderGraphCompiler;

        std::vector<RenderPass*> m_RenderPasses;
        std::vector<RenderGraphExecutionBatch> m_ExecutionBatches;
        std::map<const RenderPass*, RenderTargetInfo> m_RenderTargets;
        std::map<const RenderPass*, PassResourceStatePlan> m_ResourceStatePlans;
    };
}
//Modify End
