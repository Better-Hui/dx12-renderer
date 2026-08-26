#pragma once

#include <map>
#include <memory>
#include <vector>

#include <DX12Library/CommandQueue.h>
#include <DX12Library/CommandList.h>
#include <DX12Library/Window.h>

#include "RenderPass.h"
//Modify Begin:2026-08-26 by Hui
#include "ExternalFrameProcessor.h"
//Modify End
//Modify Begin:2026-08-07 by Hui
#include "RenderGraphCommandExecutor.h"
#include "RenderGraphCompiler.h"
#include "RenderGraphExecutionPlan.h"
#include "RenderGraphProfiler.h"
//Modify End
//Modify Begin:2026-07-30 by Hui
#include "RenderGraphQueueScheduler.h"
//Modify End
#include "RenderMetadata.h"
#include "ResourceDescription.h"
#include "ResourcePool.h"

class Texture;
class D3D12DeviceContext;
//Modify Begin:2026-08-21 by Hui
class DiagnosticTelemetrySink;
//Modify End
//Modify Begin:2026-07-29 by Hui
//Modify End

namespace RenderGraph
{
//Modify Begin:2026-08-17 by Hui
    struct RenderGraphOutputResources
    {
        ResourceId Presentation = ResourceIds::GRAPH_OUTPUT;
        std::vector<ResourceId> External = { ResourceIds::GRAPH_OUTPUT };
    };
//Modify End

    class RenderGraphRoot
    {
    public:
        RenderGraphRoot(
//Modify Begin:2026-07-30 by Hui
            std::shared_ptr<D3D12DeviceContext> deviceContext,
            Microsoft::WRL::ComPtr<ID3D12Device2> device,
            std::shared_ptr<CommandQueue> directCommandQueue,
            std::shared_ptr<CommandQueue> asyncComputeCommandQueue,
            std::shared_ptr<CommandQueue> copyCommandQueue,
//Modify End
            std::vector<std::unique_ptr<RenderPass>>&& renderPasses,
            std::vector<TextureDescription>&& textures,
            std::vector<BufferDescription>&& buffers,
            std::vector<TokenDescription>&& tokens
//Modify Begin:2026-07-28 by Hui
            , RenderGraphOutputResources outputs = {}
//Modify End
        );

        void Execute(const RenderMetadata& renderMetadata);
//Modify Begin:2026-07-29 by Hui
        void SetGpuTimestampProfiler(GpuTimestampProfiler* profiler) { m_Profiler.SetQueueProfiler(RenderPassQueue::Direct, profiler); }
//Modify End
//Modify Begin:2026-08-03 by Hui
        void SetAsyncComputeGpuTimestampProfiler(GpuTimestampProfiler* profiler) { m_Profiler.SetQueueProfiler(RenderPassQueue::AsyncCompute, profiler); }
        void SetCopyGpuTimestampProfiler(GpuTimestampProfiler* profiler) { m_Profiler.SetQueueProfiler(RenderPassQueue::Copy, profiler); }
        void SetDiagnosticTelemetrySink(DiagnosticTelemetrySink* sink) noexcept;
//Modify End
//Modify Begin:2026-07-30 by Hui
        void SetDebugSerializeAsyncCompute(bool enabled) { m_DebugSerializeAsyncCompute = enabled; }
//Modify End
//Modify Begin:2026-08-07 by Hui
        void SetParallelDirectCommandRecording(bool enabled) { m_ParallelDirectCommandRecording = enabled; }
//Modify End
        void Present(const std::shared_ptr<Window>& pWindow, ResourceId resourceId = ResourceIds::GRAPH_OUTPUT);
//Modify Begin:2026-08-24 by Hui
        void PresentWithOverlay(const std::shared_ptr<Window>& pWindow, ResourceId resourceId, const std::function<void(CommandList&)>& drawCallback);
        void PresentWithOverlayBlit(
            const std::shared_ptr<Window>& pWindow,
            ResourceId resourceId,
            const std::function<void(CommandList&, const std::shared_ptr<Texture>&)>& blitCallback,
            const std::function<void(CommandList&)>& overlayCallback);
        void PresentWithExternalFrameProcessor(
            const std::shared_ptr<Window>& pWindow,
            ResourceId displayResourceId,
            ExternalFrameProcessor& processor,
            const std::function<void(CommandList&)>& overlayCallback);
        // Presentation and readback are terminal external interactions. All graph-internal work must use a declared pass.
        void ReadbackTexture(
            const RenderMetadata& renderMetadata,
            ResourceId sourceId,
            Microsoft::WRL::ComPtr<ID3D12Resource> destination,
            const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& destinationFootprint,
            bool waitForCompletion = true);
//Modify End
//Modify Begin:2026-07-27 by Hui
        const std::shared_ptr<Texture>& GetTexture(ResourceId resourceId) const;
//Modify End
//Modify Begin:2026-08-17 by Hui
        [[nodiscard]] ResourceId GetPresentationResourceId() const { return m_PresentationResourceId; }
//Modify End
//Modify Begin:2026-08-10 by Hui
        const RenderGraphQueueFenceValues& GetFrameSubmissionFences() const;
        const RenderGraphQueueSynchronizationStats& GetFrameSynchronizationStats() const;
        RenderGraphQueueFenceValues GetResourceRetirement(ResourceId resourceId) const;
        const RenderGraphCrossQueuePlanValidation& GetCrossQueuePlanValidation() const;
//Modify End
        void MarkDirty();

    private:
        void RebuildIfNecessary(const RenderMetadata& renderMetadata);
        void CheckPotentiallyDirtyResources(const RenderMetadata& renderMetadata);
        void EmitCompiledGraphSnapshot(const RenderMetadata& renderMetadata) noexcept;
//Modify Begin:2026-08-03 by Hui
        Microsoft::WRL::ComPtr<ID3D12Device2> m_Device;
        std::shared_ptr<D3D12DeviceContext> m_DeviceContext;
        std::shared_ptr<CommandQueue> m_DirectCommandQueue;
        std::shared_ptr<CommandQueue> m_AsyncComputeCommandQueue;
        std::shared_ptr<CommandQueue> m_CopyCommandQueue;
//Modify End
//Modify Begin:2026-07-30 by Hui
        RenderGraphQueueScheduler m_QueueScheduler;
        bool m_DebugSerializeAsyncCompute = false;
//Modify End
//Modify Begin:2026-08-07 by Hui
        bool m_ParallelDirectCommandRecording = true;
//Modify End

        std::vector<std::unique_ptr<RenderPass>> m_RenderPassesDescription;

        std::vector<TextureDescription> m_TextureDescriptions;
        std::vector<BufferDescription> m_BufferDescriptions;
        std::vector<TokenDescription> m_TokenDescriptions;
//Modify Begin:2026-07-28 by Hui
        std::vector<ResourceId> m_ExternalOutputIds;
//Modify End
//Modify Begin:2026-08-17 by Hui
        ResourceId m_PresentationResourceId = ResourceIds::GRAPH_OUTPUT;
//Modify End

        std::shared_ptr<ResourcePool> m_ResourcePool;
//Modify Begin:2026-08-07 by Hui
        std::unique_ptr<RenderGraphCompiler> m_Compiler;
        std::unique_ptr<CompiledRenderGraph> m_CompiledGraph;
        RenderGraphProfiler m_Profiler;
        std::unique_ptr<RenderGraphCommandExecutor> m_CommandExecutor;
        DiagnosticTelemetrySink* m_DiagnosticTelemetrySink = nullptr;
        uint64_t m_DiagnosticSnapshotIndex = 0;
//Modify End

        bool m_Dirty = true;
    };
}
