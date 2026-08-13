//Modify Begin:2026-07-30 by BestHui
#pragma once

#include <d3d12.h>

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include "RenderTargetInfo.h"
#include "ResourceId.h"

class Resource;

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

//Modify Begin:2026-08-13 by BestHui
    struct PassExternalResourceTransition
    {
        const Resource* Resource = nullptr;
        D3D12_RESOURCE_STATES StateAfter = D3D12_RESOURCE_STATE_COMMON;
        bool InsertUavBarrier = false;
    };
//Modify End

    struct PassResourceStatePlan
    {
        std::vector<PassResourceTransition> InputTransitions;
//Modify Begin:2026-08-13 by BestHui
        std::vector<PassExternalResourceTransition> ExternalResourceTransitions;
//Modify End
        std::vector<ResourceId> AliasingOutputs;
        std::vector<PassResourceTransition> OutputTransitions;
        std::vector<ResourceId> InitOutputs;

        //Modify Begin:2026-08-07 by BestHui
        struct AsyncComputeDirectPreamble
        {
            std::vector<PassResourceTransition> DirectProducerInputTransitions;
//Modify Begin:2026-08-13 by BestHui
            std::vector<PassExternalResourceTransition> ExternalResourceTransitions;
//Modify End
            std::vector<ResourceId> AliasingOutputs;
            std::vector<PassResourceTransition> OutputTransitions;
        };

        std::optional<AsyncComputeDirectPreamble> DirectPreamble;
        //Modify End
    };

    struct RenderGraphRecordingBatch
    {
        std::vector<RenderPass*> Passes;
        bool RecordInParallel = false;
    };

    class CompiledRenderGraph final
    {
    public:
        const std::vector<RenderPass*>& GetRenderPasses() const { return m_RenderPasses; }
        const std::vector<RenderGraphRecordingBatch>& GetRecordingBatches() const { return m_RecordingBatches; }
        const std::map<const RenderPass*, RenderTargetInfo>& GetRenderTargets() const { return m_RenderTargets; }
        const std::map<const RenderPass*, PassResourceStatePlan>& GetResourceStatePlans() const { return m_ResourceStatePlans; }

    private:
        friend class RenderGraphCompiler;

        std::vector<RenderPass*> m_RenderPasses;
        std::vector<RenderGraphRecordingBatch> m_RecordingBatches;
        std::map<const RenderPass*, RenderTargetInfo> m_RenderTargets;
        std::map<const RenderPass*, PassResourceStatePlan> m_ResourceStatePlans;
    };
}
//Modify End
