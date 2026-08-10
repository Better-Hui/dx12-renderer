#pragma once

#include <functional>
#include <memory>
#include <vector>
#include <map>
#include <queue>

#include <d3d12.h>
#include <wrl.h>

#include "ResourceId.h"
#include "RenderGraphQueueFence.h"
#include "TransientResourceAllocator.h"
#include "DX12Library/CommandList.h"

class Texture;
class Buffer;
class ByteAddressBuffer;
class StructuredBuffer;
class Resource;
class ResourceStateRegistry;
class D3D12DeviceContext;

namespace RenderGraph
{
    class RenderPass;
    struct TextureDescription;
    struct BufferDescription;
    struct RenderMetadata;

    class ResourcePool
    {
    public:
//Modify Begin:2026-07-30 by BestHui
        ResourcePool(
            std::shared_ptr<D3D12DeviceContext> deviceContext,
            std::shared_ptr<CommandQueue> directCommandQueue,
            std::shared_ptr<CommandQueue> asyncComputeCommandQueue);
//Modify End
        void BeginFrame(CommandList& commandList);

        const Resource& GetResource(ResourceId resourceId) const;
        const std::shared_ptr<Texture>& GetTexture(ResourceId resourceId) const;
        const std::shared_ptr<Buffer>& GetBuffer(ResourceId resourceId) const;
        std::shared_ptr<StructuredBuffer> GetStructuredBuffer(ResourceId resourceId) const;
        std::shared_ptr<ByteAddressBuffer> GetByteAddressBuffer(ResourceId resourceId) const;

        void ForEachResource(const std::function<bool(const ResourceDescription&)>& func);

        const TransientResourceAllocator::ResourceLifecycle& GetResourceLifecycle(ResourceId resourceId);
//Modify Begin:2026-07-28 by BestHui
        bool HasResourceLifecycle(ResourceId resourceId) const;
//Modify End

        bool IsRegistered(ResourceId resourceId) const;
        const ResourceDescription& GetDescription(ResourceId resourceId) const;

        void Clear(const std::map<ResourceId, RenderGraphQueueFenceValues>& resourceRetirements = {});
//Modify Begin:2026-07-28 by BestHui
        void InitHeaps(
            const std::vector<RenderPass*>& renderPasses,
            const Microsoft::WRL::ComPtr<ID3D12Device2>& pDevice,
            const std::vector<ResourceId>& externalOutputIds = { ResourceIds::GRAPH_OUTPUT });
//Modify End
//Modify Begin:2026-08-10 by BestHui
        void CreateResources();
//Modify End

        void RegisterTexture(
            const TextureDescription& desc,
            const std::vector<RenderPass*>& renderPasses,
            const RenderMetadata& renderMetadata,
            const Microsoft::WRL::ComPtr<ID3D12Device2>& pDevice);
        void RegisterBuffer(
            const BufferDescription& desc,
            const std::vector<RenderPass*>& renderPasses,
            const RenderMetadata& renderMetadata,
            const Microsoft::WRL::ComPtr<ID3D12Device2>& pDevice);

        const std::shared_ptr<Texture>& CreateTexture(ResourceId resourceId);
        const std::shared_ptr<Buffer>& CreateBuffer(ResourceId resourceId);

    private:
        enum class ResourceInstanceType
        {
            Texture,
            Buffer,
        };

        struct ResourceInstance
        {
            ResourceInstanceType m_Type;
            std::shared_ptr<Texture> m_Texture;
            std::shared_ptr<Buffer> m_Buffer;

            const Resource& GetResource() const;
        };

        ResourceInstance& AppendResourceInstance(ResourceId resourceId, const ResourceInstance& resourceInstance);

        std::vector<ResourceInstance> m_ResourceInstances;
        std::map<ResourceId, ResourceDescription> m_ResourceDescriptions;
        std::vector<TransientResourceAllocator::HeapInfo> m_HeapInfos;
//Modify Begin:2026-07-28 by BestHui
        std::map<ResourceId, TransientResourceAllocator::ResourceLifecycle> m_ResourceLifecycles;
//Modify End

//Modify Begin:2026-07-30 by BestHui
        struct DeferredDeletionBatch
        {
            RenderGraphQueueFenceValues FenceValues;
            std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> Resources;
            std::vector<Microsoft::WRL::ComPtr<ID3D12Heap>> Heaps;
            std::vector<ID3D12Resource*> ResourceStateEntries;
        };

        bool IsRetirementComplete(const RenderGraphQueueFenceValues& fenceValues) const;

        std::shared_ptr<CommandQueue> m_DirectCommandQueue;
        std::shared_ptr<CommandQueue> m_AsyncComputeCommandQueue;
        std::shared_ptr<D3D12DeviceContext> m_DeviceContext;
        std::shared_ptr<ResourceStateRegistry> m_ResourceStateRegistry;
        std::queue<DeferredDeletionBatch> m_DeferredDeletionQueue;
//Modify End

        struct ResourceHeapInfo
        {
            uint32_t m_HeapIndex;
            uint32_t m_LifecycleIndex;
        };

        std::map<ResourceId, ResourceHeapInfo> m_ResourceHeapInfo;
    };
}
