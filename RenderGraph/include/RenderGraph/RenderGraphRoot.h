#pragma once

#include <map>
#include <memory>
#include <span>
#include <vector>

#include <DX12Library/CommandQueue.h>
#include <DX12Library/CommandList.h>
#include <DX12Library/Window.h>

#include "RenderPass.h"
//Modify Begin:2026-07-30 by BestHui
#include "RenderGraphCommandExecutor.h"
//Modify Begin:2026-08-07 by BestHui
#include "RenderGraphCompiler.h"
//Modify End
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
//Modify Begin:2026-08-07 by BestHui
        void SetParallelDirectCommandRecording(bool enabled) { m_ParallelDirectCommandRecording = enabled; }
//Modify End
        void Present(const std::shared_ptr<Window>& pWindow, ResourceId resourceId = ResourceIds::GRAPH_OUTPUT);
//Modify Begin:2026-07-28 by BestHui
        void PresentWithOverlay(const std::shared_ptr<Window>& pWindow, ResourceId resourceId, const std::function<void(CommandList&)>& drawCallback);
        void PresentWithOverlayBlit(
            const std::shared_ptr<Window>& pWindow,
            ResourceId resourceId,
            const std::function<void(CommandList&, const std::shared_ptr<Texture>&)>& blitCallback,
            const std::function<void(CommandList&)>& overlayCallback);
//Modify Begin:2026-08-07 by BestHui
        void PresentWithExternalFrameProcessor(
            const std::shared_ptr<Window>& pWindow,
            ResourceId displayResourceId,
            std::span<const ResourceId> processorResourceIds,
            const std::function<void(CommandList&, const std::shared_ptr<Texture>&)>& processorCallback,
            const std::function<void(CommandList&)>& overlayCallback,
            const std::function<void()>& beforePresentCallback,
            const std::function<void()>& afterPresentCallback);
//Modify End
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
        void TransitionBarrier(const Resource& resource, D3D12_RESOURCE_STATES stateAfter);
        void FlushBarriers(const CommandList& commandList);

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
//Modify Begin:2026-08-07 by BestHui
        bool m_ParallelDirectCommandRecording = true;
//Modify End

        std::vector<std::unique_ptr<RenderPass>> m_RenderPassesDescription;

        std::vector<TextureDescription> m_TextureDescriptions;
        std::vector<BufferDescription> m_BufferDescriptions;
        std::vector<TokenDescription> m_TokenDescriptions;
//Modify Begin:2026-07-28 by BestHui
        std::vector<ResourceId> m_ExternalOutputIds;
//Modify End

        std::shared_ptr<ResourcePool> m_ResourcePool;
        RenderGraphResourceStateTracker m_ResourceStateTracker;
//Modify Begin:2026-07-30 by BestHui
//Modify Begin:2026-08-07 by BestHui
        std::unique_ptr<RenderGraphCompiler> m_Compiler;
        std::unique_ptr<CompiledRenderGraph> m_CompiledGraph;
//Modify End
        RenderGraphProfiler m_Profiler;
        std::unique_ptr<RenderGraphCommandExecutor> m_CommandExecutor;
//Modify End

        bool m_Dirty = true;
    };
}
