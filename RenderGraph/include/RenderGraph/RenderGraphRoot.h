#pragma once

#include <map>
#include <memory>
#include <vector>

#include <DX12Library/CommandQueue.h>
#include <DX12Library/CommandList.h>
#include <DX12Library/Window.h>

#include "RenderPass.h"
//Modify Begin:2026-07-30 by BestHui
#include "RenderGraphCommandExecutor.h"
#include "RenderGraphExecutionPlan.h"
#include "RenderGraphProfiler.h"
//Modify End
//Modify Begin:2026-07-30 by BestHui
#include "RenderGraphQueueScheduler.h"
#include "RenderGraphResourceStateTracker.h"
//Modify End
#include "RenderMetadata.h"
#include "ResourceDescription.h"
#include "ResourcePool.h"

class Texture;
//Modify Begin:2026-07-29 by BestHui
//Modify End

namespace RenderGraph
{
    class RenderGraphRoot
    {
    public:
        RenderGraphRoot(
//Modify Begin:2026-07-30 by BestHui
            Microsoft::WRL::ComPtr<ID3D12Device2> device,
            std::shared_ptr<CommandQueue> directCommandQueue,
            std::shared_ptr<CommandQueue> asyncComputeCommandQueue,
//Modify End
            std::vector<std::unique_ptr<RenderPass>>&& renderPasses,
            std::vector<TextureDescription>&& textures,
            std::vector<BufferDescription>&& buffers,
            std::vector<TokenDescription>&& tokens
//Modify Begin:2026-07-28 by BestHui
            , std::vector<ResourceId> externalOutputs = { ResourceIds::GRAPH_OUTPUT }
//Modify End
        );

        void Execute(const RenderMetadata& renderMetadata);
//Modify Begin:2026-07-29 by BestHui
        void SetGpuTimestampProfiler(GpuTimestampProfiler* profiler) { m_Profiler.SetQueueProfiler(RenderPassQueue::Direct, profiler); }
//Modify End
//Modify Begin:2026-08-03 by BestHui
        void SetAsyncComputeGpuTimestampProfiler(GpuTimestampProfiler* profiler) { m_Profiler.SetQueueProfiler(RenderPassQueue::AsyncCompute, profiler); }
//Modify End
//Modify Begin:2026-07-30 by BestHui
        void SetDebugSerializeAsyncCompute(bool enabled) { m_DebugSerializeAsyncCompute = enabled; }
//Modify End
        void Present(const std::shared_ptr<Window>& pWindow, ResourceId resourceId = ResourceIds::GRAPH_OUTPUT);
//Modify Begin:2026-07-28 by BestHui
        void PresentWithOverlay(const std::shared_ptr<Window>& pWindow, ResourceId resourceId, const std::function<void(CommandList&)>& drawCallback);
        void PresentWithOverlayBlit(
            const std::shared_ptr<Window>& pWindow,
            ResourceId resourceId,
            const std::function<void(CommandList&, const std::shared_ptr<Texture>&)>& blitCallback,
            const std::function<void(CommandList&)>& overlayCallback);
        void CopyTexture(const RenderMetadata& renderMetadata, ResourceId sourceId, ResourceId destinationId, bool waitForCompletion = false);
        void DrawToTexture(const RenderMetadata& renderMetadata, ResourceId resourceId, const std::function<void(CommandList&)>& drawCallback);
        void TransitionTexture(const RenderMetadata& renderMetadata, ResourceId resourceId, D3D12_RESOURCE_STATES stateAfter, bool waitForCompletion = false);
//Modify End
        void DrawToGraphOutput(const RenderMetadata& renderMetadata, const std::function<void(CommandList&)>& drawCallback);
//Modify Begin:2026-07-27 by BestHui
        const std::shared_ptr<Texture>& GetTexture(ResourceId resourceId) const;
//Modify End
        void MarkDirty();

    private:
        void RebuildIfNecessary(const RenderMetadata& renderMetadata);
        void CheckPotentiallyDirtyResources(const RenderMetadata& renderMetadata);
        void Build(const RenderMetadata& renderMetadata);
//Modify Begin:2026-07-29 by BestHui
        void BuildPassResourceStatePlans();
//Modify End
        D3D12_RESOURCE_STATES GetCurrentResourceState(const Resource& resource) const;
        void SetCurrentResourceState(const Resource& resource, D3D12_RESOURCE_STATES state);
        void TransitionBarrier(const Resource& resource, D3D12_RESOURCE_STATES stateAfter);
        void UavBarrier(const Resource& resource);
        void AliasingBarrier(const Resource& resourceAfter);
        void FlushBarriers(const CommandList& commandList);

        bool IsResourceDefined(ResourceId id) const;

//Modify Begin:2026-07-30 by BestHui
        Microsoft::WRL::ComPtr<ID3D12Device2> m_Device;
        std::shared_ptr<CommandQueue> m_DirectCommandQueue;
//Modify Begin:2026-08-03 by BestHui
        std::shared_ptr<CommandQueue> m_AsyncComputeCommandQueue;
//Modify End
//Modify End
//Modify Begin:2026-07-30 by BestHui
        RenderGraphQueueScheduler m_QueueScheduler;
        bool m_DebugSerializeAsyncCompute = false;
//Modify End

        std::vector<std::unique_ptr<RenderPass>> m_RenderPassesDescription;
        std::vector<std::vector<RenderPass*>> m_RenderPassesSorted;
        std::vector<RenderPass*> m_RenderPassesBuilt;

        std::vector<TextureDescription> m_TextureDescriptions;
        std::vector<BufferDescription> m_BufferDescriptions;
        std::vector<TokenDescription> m_TokenDescriptions;
//Modify Begin:2026-07-28 by BestHui
        std::vector<ResourceId> m_ExternalOutputIds;
//Modify End

        std::shared_ptr<ResourcePool> m_ResourcePool;
        std::map<const RenderPass*, RenderTargetInfo> m_RenderTargets;
        std::shared_ptr<RenderTarget> m_GraphOutputRenderTarget;
        RenderGraphResourceStateTracker m_ResourceStateTracker;
//Modify Begin:2026-07-30 by BestHui
        std::map<const RenderPass*, PassResourceStatePlan> m_PassResourceStatePlans;
        RenderGraphProfiler m_Profiler;
        std::unique_ptr<RenderGraphCommandExecutor> m_CommandExecutor;
//Modify End

        bool m_Dirty = true;
    };
}
