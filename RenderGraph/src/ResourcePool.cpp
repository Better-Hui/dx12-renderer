#include "ResourcePool.h"

#include <DX12Library/Buffer.h>
#include <DX12Library/ByteAddressBuffer.h>
#include <DX12Library/CommandQueue.h>
#include <DX12Library/D3D12DeviceContext.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/StructuredBuffer.h>
#include <DX12Library/Texture.h>

#include "RenderMetadata.h"
#include "RenderPass.h"
#include "ResourceDescription.h"

#include <stdexcept>

using namespace Microsoft::WRL;

namespace
{
    UINT GetMsaaQualityLevels(const ComPtr<ID3D12Device2>& pDevice, DXGI_FORMAT format, UINT sampleCount)
    {
        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msLevels;
        msLevels.Format = format;
        msLevels.SampleCount = sampleCount;
        msLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;

        ThrowIfFailed(pDevice->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &msLevels, sizeof(msLevels)));
        return msLevels.NumQualityLevels;
    }

    D3D12_RESOURCE_ALLOCATION_INFO GetResourceAllocationInfo(const ComPtr<ID3D12Device2>& pDevice, const CD3DX12_RESOURCE_DESC& desc)
    {
        return pDevice->GetResourceAllocationInfo(0, 1, &desc);
    }

    D3D12_RESOURCE_ALLOCATION_INFO GetStructuredBufferResourceAllocationInfo(const ComPtr<ID3D12Device2>& pDevice, const D3D12_RESOURCE_DESC& desc, const D3D12_RESOURCE_DESC& counterDesc)
    {
        const D3D12_RESOURCE_DESC descs[2] = { counterDesc, desc };
        return pDevice->GetResourceAllocationInfo(0, 2, descs);
    }

    std::shared_ptr<Texture> CreateTextureImpl(
        const RenderGraph::ResourceDescription& desc,
        const ComPtr<ID3D12Heap>& pHeap,
        const std::shared_ptr<D3D12DeviceContext>& deviceContext
    )
    {
        const bool useClearValue =
        desc.m_TextureUsageType == TextureUsageType::RenderTarget ||
        desc.m_TextureUsageType == TextureUsageType::Depth;
        constexpr UINT64 heapOffset = 0u;
        auto texture = std::make_shared<Texture>(
            desc.m_DxDesc,
            pHeap,
            heapOffset,
            useClearValue ? desc.m_TextureDescription.m_ClearValue : ClearValue{},
            desc.m_TextureUsageType,
            RenderGraph::ResourceIds::GetResourceName(desc.m_TextureDescription.m_Id),
            deviceContext
        );
        texture->SetAutoBarriersEnabled(false);
        return texture;
    }

//Modify Begin:2026-07-28 by Hui
    std::shared_ptr<Texture> CreateDedicatedTextureImpl(
        const RenderGraph::ResourceDescription& desc,
        const std::shared_ptr<D3D12DeviceContext>& deviceContext)
    {
        const bool useClearValue =
            desc.m_TextureUsageType == TextureUsageType::RenderTarget ||
            desc.m_TextureUsageType == TextureUsageType::Depth;
        auto texture = std::make_shared<Texture>(
            desc.m_DxDesc,
            desc.m_HeapFlags,
            useClearValue ? desc.m_TextureDescription.m_ClearValue : ClearValue{},
            desc.m_TextureUsageType,
            RenderGraph::ResourceIds::GetResourceName(desc.m_TextureDescription.m_Id),
            deviceContext
        );
        texture->SetAutoBarriersEnabled(false);
        return texture;
    }
//Modify End

    std::shared_ptr<Buffer> CreateBufferImpl(
        const RenderGraph::ResourceDescription& desc,
        const ComPtr<ID3D12Heap>& pHeap,
        const std::shared_ptr<D3D12DeviceContext>& deviceContext
    )
    {
        constexpr UINT64 heapOffset = 0u;
        std::shared_ptr<Buffer> pBuffer;
        if (desc.m_BufferDescription.m_Kind == RenderGraph::BufferKind::Raw)
        {
            pBuffer = std::make_shared<ByteAddressBuffer>(
                desc.m_DxDesc,
                pHeap,
                heapOffset,
                desc.m_ElementsCount, desc.m_BufferDescription.m_Stride,
                RenderGraph::ResourceIds::GetResourceName(desc.m_BufferDescription.m_Id),
                deviceContext
            );
        }
        else
        {
            const auto pStructuredBuffer = std::make_shared<StructuredBuffer>(
                desc.m_DxDesc,
                pHeap,
                heapOffset,
                desc.m_ElementsCount, desc.m_BufferDescription.m_Stride,
                RenderGraph::ResourceIds::GetResourceName(desc.m_BufferDescription.m_Id),
                deviceContext
            );
            pStructuredBuffer->GetCounterBuffer().SetAutoBarriersEnabled(false);
            pBuffer = pStructuredBuffer;
        }

        pBuffer->SetAutoBarriersEnabled(false);

        return pBuffer;
    }
}

//Modify Begin:2026-07-30 by Hui
RenderGraph::ResourcePool::ResourcePool(
    std::shared_ptr<D3D12DeviceContext> deviceContext,
    std::shared_ptr<CommandQueue> directCommandQueue,
    std::shared_ptr<CommandQueue> asyncComputeCommandQueue,
    std::shared_ptr<CommandQueue> copyCommandQueue)
    : m_DirectCommandQueue(std::move(directCommandQueue))
    , m_AsyncComputeCommandQueue(std::move(asyncComputeCommandQueue))
    , m_CopyCommandQueue(std::move(copyCommandQueue))
    , m_DeviceContext(std::move(deviceContext))
{
    Assert(m_DirectCommandQueue != nullptr, "Resource pool requires a direct command queue.");
    Assert(m_AsyncComputeCommandQueue != nullptr, "Resource pool requires an async compute command queue.");
    Assert(m_CopyCommandQueue != nullptr, "Resource pool requires a copy command queue.");
    Assert(m_DeviceContext != nullptr, "Resource pool requires a D3D12 device context.");
    m_ResourceStateRegistry = m_DirectCommandQueue->GetResourceStateRegistry();
    Assert(m_ResourceStateRegistry != nullptr, "Resource pool requires a resource state registry.");
}
//Modify End

void RenderGraph::ResourcePool::BeginFrame(CommandList& commandList)
{
    ForEachResource([&commandList, this](const ResourceDescription& desc)
    {
        GetResource(desc.m_Id).ForEachResourceRecursive([&commandList](const Resource& resource)
        {
            commandList.TrackResource(resource);
        });
        return true;
    });

    while (!m_DeferredDeletionQueue.empty())
    {
//Modify Begin:2026-07-30 by Hui
        if (IsRetirementComplete(m_DeferredDeletionQueue.front().FenceValues))
//Modify End
        {
            m_DeferredDeletionQueue.pop();
        }
        else
        {
            break;
        }
    }
}

//Modify Begin:2026-07-30 by Hui
bool RenderGraph::ResourcePool::IsRetirementComplete(const RenderGraphQueueFenceValues& fenceValues) const
{
    return (fenceValues.Direct == 0 || m_DirectCommandQueue->IsFenceComplete(fenceValues.Direct)) &&
        (fenceValues.AsyncCompute == 0 || m_AsyncComputeCommandQueue->IsFenceComplete(fenceValues.AsyncCompute)) &&
        (fenceValues.Copy == 0 || m_CopyCommandQueue->IsFenceComplete(fenceValues.Copy));
}
//Modify End
const Resource& RenderGraph::ResourcePool::GetResource(const ResourceId resourceId) const
{
    Assert(IsRegistered(resourceId), "Resource is not registered.");
//Modify Begin:2026-07-30 by Hui
    const auto resourceInstance = m_ResourceInstances.find(resourceId);
    Assert(resourceInstance != m_ResourceInstances.end(), "Registered resource has not been created.");
    return resourceInstance->second.GetResource();
//Modify End
}

const std::shared_ptr<Texture>& RenderGraph::ResourcePool::GetTexture(const ResourceId resourceId) const
{
    Assert(IsRegistered(resourceId), "Resource is not registered.");
    Assert(m_ResourceDescriptions.at(resourceId).m_ResourceType == ResourceType::Texture, "Invalid resource type.");

//Modify Begin:2026-07-30 by Hui
    const auto resourceInstance = m_ResourceInstances.find(resourceId);
    Assert(resourceInstance != m_ResourceInstances.end(), "Registered texture has not been created.");
    Assert(resourceInstance->second.m_Type == ResourceInstanceType::Texture, "Invalid resource type.");
    return resourceInstance->second.m_Texture;
//Modify End
}

const std::shared_ptr<Buffer>& RenderGraph::ResourcePool::GetBuffer(const ResourceId resourceId) const
{
    Assert(IsRegistered(resourceId), "Resource is not registered.");
    Assert(m_ResourceDescriptions.at(resourceId).m_ResourceType == ResourceType::Buffer, "Invalid resource type.");

//Modify Begin:2026-07-30 by Hui
    const auto resourceInstance = m_ResourceInstances.find(resourceId);
    Assert(resourceInstance != m_ResourceInstances.end(), "Registered buffer has not been created.");
    Assert(resourceInstance->second.m_Type == ResourceInstanceType::Buffer, "Invalid resource type.");
    return resourceInstance->second.m_Buffer;
//Modify End
}

std::shared_ptr<StructuredBuffer> RenderGraph::ResourcePool::GetStructuredBuffer(const ResourceId resourceId) const
{
//Modify Begin:2026-08-07 by Hui
    Assert(
        GetDescription(resourceId).m_BufferDescription.m_Kind == BufferKind::Structured,
        "Render graph buffer is not declared as structured.");
//Modify End
    const std::shared_ptr<Buffer>& pBuffer = GetBuffer(resourceId);
    const std::shared_ptr<StructuredBuffer> pStructuredBuffer = std::dynamic_pointer_cast<StructuredBuffer>(pBuffer);
    Assert(pStructuredBuffer != nullptr, "Invalid cast");
    return pStructuredBuffer;
}

std::shared_ptr<ByteAddressBuffer> RenderGraph::ResourcePool::GetByteAddressBuffer(ResourceId resourceId) const
{
//Modify Begin:2026-08-07 by Hui
    Assert(
        GetDescription(resourceId).m_BufferDescription.m_Kind == BufferKind::Raw,
        "Render graph buffer is not declared as raw.");
//Modify End
    const std::shared_ptr<Buffer>& pBuffer = GetBuffer(resourceId);
    const std::shared_ptr<ByteAddressBuffer> pByteAddressBuffer = std::dynamic_pointer_cast<ByteAddressBuffer>(pBuffer);
    Assert(pByteAddressBuffer != nullptr, "Invalid cast");
    return pByteAddressBuffer;
}

void RenderGraph::ResourcePool::ForEachResource(const std::function<bool(const ResourceDescription&)>& func)
{
    for (const auto& [resourceId, resourceDescription] : m_ResourceDescriptions)
    {
        if (const bool shouldContinue = func(resourceDescription); !shouldContinue)
        {
            break;
        }
    }
}

const RenderGraph::TransientResourceAllocator::ResourceLifecycle& RenderGraph::ResourcePool::GetResourceLifecycle(ResourceId resourceId)
{
//Modify Begin:2026-07-28 by Hui
    return m_ResourceLifecycles.at(resourceId);
//Modify End
}

//Modify Begin:2026-07-28 by Hui
bool RenderGraph::ResourcePool::HasResourceLifecycle(const ResourceId resourceId) const
{
    return m_ResourceLifecycles.contains(resourceId);
}
//Modify End

bool RenderGraph::ResourcePool::IsRegistered(const ResourceId resourceId) const
{
    return m_ResourceDescriptions.contains(resourceId);
}

const RenderGraph::ResourceDescription& RenderGraph::ResourcePool::GetDescription(const ResourceId resourceId) const
{
    Assert(IsRegistered(resourceId), "The resource is not registered.");
    return m_ResourceDescriptions.find(resourceId)->second;
}

void RenderGraph::ResourcePool::Clear(
    const std::map<ResourceId, RenderGraphQueueFenceValues>& resourceRetirements)
{
//Modify Begin:2026-07-30 by Hui
    std::map<uint32_t, DeferredDeletionBatch> heapBatches;
    std::vector<DeferredDeletionBatch> dedicatedBatches;

    ForEachResource([this, &resourceRetirements, &heapBatches, &dedicatedBatches](const ResourceDescription& desc)
    {
        RenderGraphQueueFenceValues fenceValues;
        if (const auto retirement = resourceRetirements.find(desc.m_Id); retirement != resourceRetirements.end())
        {
            fenceValues = retirement->second;
        }

        DeferredDeletionBatch* deletionBatch = nullptr;
        if (desc.m_DedicatedResource)
        {
            dedicatedBatches.emplace_back();
            deletionBatch = &dedicatedBatches.back();
        }
        else
        {
            const auto heapInfo = m_ResourceHeapInfo.find(desc.m_Id);
            Assert(heapInfo != m_ResourceHeapInfo.end(), "Transient resource has no heap retirement information.");
            deletionBatch = &heapBatches[heapInfo->second.m_HeapIndex];
        }

        deletionBatch->FenceValues.Merge(fenceValues);
        GetResource(desc.m_Id).ForEachResourceRecursive([deletionBatch](const Resource& resource)
        {
            const Microsoft::WRL::ComPtr<ID3D12Resource> d3d12Resource = resource.GetD3D12Resource();
            deletionBatch->Resources.push_back(d3d12Resource);
            deletionBatch->StateRegistrations.push_back(resource.GetStateRegistration());
        });
        return true;
    });

    for (auto& [heapIndex, deletionBatch] : heapBatches)
    {
        deletionBatch.Heaps.push_back(m_HeapInfos[heapIndex].m_Heap);
        m_DeferredDeletionQueue.push(std::move(deletionBatch));
    }
    for (DeferredDeletionBatch& deletionBatch : dedicatedBatches)
    {
        m_DeferredDeletionQueue.push(std::move(deletionBatch));
    }
//Modify End

    m_ResourceDescriptions.clear();
    m_HeapInfos.clear();
//Modify Begin:2026-07-28 by Hui
    m_ResourceLifecycles.clear();
//Modify End
    m_ResourceHeapInfo.clear();

    m_ResourceInstances.clear();
}

//Modify Begin:2026-07-28 by Hui
void RenderGraph::ResourcePool::InitHeaps(
    const std::vector<RenderPass*>& renderPasses,
    const ComPtr<ID3D12Device2>& pDevice,
    const std::vector<ResourceId>& externalOutputIds)
//Modify End
{
//Modify Begin:2026-07-28 by Hui
    const auto lifecycles = TransientResourceAllocator::GetResourceLifecycles(renderPasses, m_ResourceDescriptions, externalOutputIds);
//Modify End
//Modify Begin:2026-07-28 by Hui
    m_ResourceLifecycles = lifecycles;
//Modify End
//Modify Begin:2026-07-28 by Hui
    m_HeapInfos = TransientResourceAllocator::CreateHeaps(m_ResourceLifecycles, m_ResourceDescriptions, pDevice);
//Modify End

    for (uint32_t heapIndex = 0; heapIndex < m_HeapInfos.size(); ++heapIndex)
    {
        const auto& heapInfo = m_HeapInfos[heapIndex];

        for (uint32_t lifecycleIndex = 0; lifecycleIndex < heapInfo.m_ResourceLifecycles.size(); ++lifecycleIndex)
        {
            const auto& lifecycle = heapInfo.m_ResourceLifecycles[lifecycleIndex];
            m_ResourceHeapInfo[lifecycle.m_Id] = { heapIndex, lifecycleIndex };
        }
    }
}

//Modify Begin:2026-08-10 by Hui
void RenderGraph::ResourcePool::CreateResources()
{
    Assert(m_ResourceInstances.empty(), "Render graph resources must be cleared before recreation.");

    for (const auto& [resourceId, resourceDescription] : m_ResourceDescriptions)
    {
        switch (resourceDescription.m_ResourceType)
        {
        case ResourceType::Texture:
            CreateTexture(resourceId);
            break;
        case ResourceType::Buffer:
            CreateBuffer(resourceId);
            break;
        default:
//Modify Begin:2026-07-30 by Hui
            throw std::logic_error("Unsupported render graph resource type.");
//Modify End
        }
    }
}
//Modify End

void RenderGraph::ResourcePool::RegisterTexture(const TextureDescription& desc, const std::vector<RenderPass*>& renderPasses, const RenderMetadata& renderMetadata, const ComPtr<ID3D12Device2>& pDevice)
{
//Modify Begin:2026-07-28 by Hui
    D3D12_RESOURCE_FLAGS resourceFlags = desc.m_ExtraResourceFlags;
//Modify End
    auto textureUsageType = TextureUsageType::Other;
    {
        bool depth = false;
        bool unorderedAccess = false;
        bool renderTarget = false;

        for (const auto& pRenderPass : renderPasses)
        {
            for (const auto& output : pRenderPass->GetOutputs())
            {
                if (desc.m_Id == output.m_Id)
                {
                    switch (output.m_Type)
                    {
                    case OutputType::RenderTarget:
                        renderTarget = true;
                        break;
                    case OutputType::DepthRead:
                    case OutputType::DepthWrite:
                        depth = true;
                        break;
                    case OutputType::UnorderedAccess:
                        unorderedAccess = true;
                        break;
//Modify Begin:2026-07-28 by Hui
                    case OutputType::ExternalAccess:
                        break;
//Modify End
                    case OutputType::CopyDestination:
                        // still valid but do not have a related flag
                        break;
                    default:
                        Assert(false, "Invalid output type.");
                        break;
                    }
                }
            }
        }

        Assert(!(depth && unorderedAccess), "Textures cannot be used for depth-stencil and unordered access at the same time.");
        Assert(!(depth && renderTarget), "Textures cannot be used for depth-stencil and render target access at the same time.");

        if (depth)
        {
            resourceFlags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
            textureUsageType = TextureUsageType::Depth;
        }
        if (unorderedAccess)
        {
            resourceFlags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        }
        if (renderTarget)
        {
            resourceFlags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            textureUsageType = TextureUsageType::RenderTarget;
        }
//Modify Begin:2026-07-28 by Hui
        if ((resourceFlags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0 && !depth)
        {
            textureUsageType = TextureUsageType::RenderTarget;
        }
//Modify End
    }

    UINT msaaQualityLevels = desc.m_SampleCount == 0 ? 0 : GetMsaaQualityLevels(pDevice, desc.m_Format, desc.m_SampleCount) - 1;

    const auto width = desc.m_WidthExpression(renderMetadata);
    const auto height = desc.m_HeightExpression(renderMetadata);

    auto dxDesc = CD3DX12_RESOURCE_DESC::Tex2D(desc.m_Format,
        width, height,
        desc.m_ArraySize, desc.m_MipLevels,
        desc.m_SampleCount, msaaQualityLevels,
        resourceFlags);

    ResourceDescription description = {};
    description.m_Id = desc.m_Id;
    description.m_TextureDescription = desc;
    description.m_DxDesc = dxDesc;
    description.m_ResourceType = ResourceType::Texture;
    description.m_TextureUsageType = textureUsageType;
//Modify Begin:2026-07-28 by Hui
    description.m_HeapFlags = desc.m_HeapFlags;
    description.m_DedicatedResource = desc.m_DedicatedResource || desc.m_HeapFlags != D3D12_HEAP_FLAG_NONE;
//Modify End

    const auto allocationInfo = GetResourceAllocationInfo(pDevice, dxDesc);
    description.m_TotalSize = allocationInfo.SizeInBytes;
    description.m_Alignment = allocationInfo.Alignment;

    m_ResourceDescriptions[desc.m_Id] = description;
}

void RenderGraph::ResourcePool::RegisterBuffer(const BufferDescription& desc, const std::vector<RenderPass*>& renderPasses, const RenderMetadata& renderMetadata, const ComPtr<ID3D12Device2>& pDevice)
{
    D3D12_RESOURCE_FLAGS resourceFlags = D3D12_RESOURCE_FLAG_NONE;

//Modify Begin:2026-08-07 by Hui
    Assert(desc.m_Stride > 0, "Render graph buffer stride must be positive.");
    Assert(
        desc.m_Kind != BufferKind::Raw || desc.m_Stride == 1,
        "Raw render graph buffers must use a byte stride of one.");

    bool unorderedAccess = HasBufferUsage(desc.m_Usage, BufferUsage::UnorderedAccess);
//Modify End

    for (const auto& pRenderPass : renderPasses)
    {
        for (const auto& output : pRenderPass->GetOutputs())
        {
            if (desc.m_Id == output.m_Id)
            {
                switch (output.m_Type)
                {
                case OutputType::UnorderedAccess:
                    unorderedAccess = true;
                    break;
//Modify Begin:2026-07-28 by Hui
                case OutputType::ExternalAccess:
                    break;
//Modify End
                case OutputType::CopyDestination:
                    // still valid but do not have a related flag
                    break;
                default:
                    Assert(false, "Invalid output type.");
                    break;
                }
            }
        }
    }

    if (unorderedAccess)
    {
        resourceFlags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }

    const size_t elementsCount = desc.m_SizeExpression(renderMetadata);
    const size_t totalSize = elementsCount * desc.m_Stride;
    const auto dxDesc = CD3DX12_RESOURCE_DESC::Buffer(totalSize, resourceFlags);

    ResourceDescription description = {};
    description.m_Id = desc.m_Id;
    description.m_BufferDescription = desc;
    description.m_DxDesc = dxDesc;
    description.m_ElementsCount = elementsCount;
    description.m_ResourceType = ResourceType::Buffer;

//Modify Begin:2026-08-07 by Hui
    const auto allocationInfo = desc.m_Kind == BufferKind::Structured
        ? GetStructuredBufferResourceAllocationInfo(pDevice, dxDesc, StructuredBuffer::COUNTER_DESC)
        : GetResourceAllocationInfo(pDevice, dxDesc);
//Modify End
    description.m_TotalSize = allocationInfo.SizeInBytes;
    description.m_Alignment = allocationInfo.Alignment;

    m_ResourceDescriptions[desc.m_Id] = description;
}

const std::shared_ptr<Texture>& RenderGraph::ResourcePool::CreateTexture(const ResourceId resourceId)
{
    Assert(IsRegistered(resourceId), "The resource is not registered.");

    const ResourceDescription& resourceDescription = m_ResourceDescriptions[resourceId];
//Modify Begin:2026-07-28 by Hui
    Assert(resourceDescription.m_DedicatedResource || m_ResourceHeapInfo.contains(resourceId), "Transient texture has no lifecycle heap.");
    const std::shared_ptr<Texture> pTexture = resourceDescription.m_DedicatedResource
        ? CreateDedicatedTextureImpl(resourceDescription, m_DeviceContext)
        : CreateTextureImpl(resourceDescription, m_HeapInfos[m_ResourceHeapInfo[resourceId].m_HeapIndex].m_Heap, m_DeviceContext);
//Modify End

    ResourceInstance resourceInstance = {};
    resourceInstance.m_Type = ResourceInstanceType::Texture;
    resourceInstance.m_Texture = pTexture;

    return AppendResourceInstance(resourceId, resourceInstance).m_Texture;
}

const std::shared_ptr<Buffer>& RenderGraph::ResourcePool::CreateBuffer(const ResourceId resourceId)
{
    Assert(IsRegistered(resourceId), "The resource is not registered.");

    const ResourceDescription& resourceMetadata = m_ResourceDescriptions[resourceId];
//Modify Begin:2026-07-28 by Hui
    Assert(m_ResourceHeapInfo.contains(resourceId), "Transient buffer has no lifecycle heap.");
//Modify End
    const uint32_t heapIndex = m_ResourceHeapInfo[resourceId].m_HeapIndex;
    const ComPtr<ID3D12Heap>& pHeap = m_HeapInfos[heapIndex].m_Heap;
    const std::shared_ptr<Buffer> pBuffer = CreateBufferImpl(resourceMetadata, pHeap, m_DeviceContext);

    ResourceInstance resourceInstance = {};
    resourceInstance.m_Type = ResourceInstanceType::Buffer;
    resourceInstance.m_Buffer = pBuffer;

    return AppendResourceInstance(resourceId, resourceInstance).m_Buffer;
}

const Resource& RenderGraph::ResourcePool::ResourceInstance::GetResource() const
{
    switch (m_Type)
    {
    case ResourceInstanceType::Texture: // NOLINT(bugprone-branch-clone)
        Assert(m_Texture != nullptr, "Invalid texture.");
        return *m_Texture;
    case ResourceInstanceType::Buffer:
        Assert(m_Buffer != nullptr, "Invalid buffer.");
        return *m_Buffer;
    default:
        Assert(false, "Invalid resource type.");
//Modify Begin:2026-07-30 by Hui
        throw std::logic_error("Invalid render graph resource instance type.");
//Modify End
    }
}

RenderGraph::ResourcePool::ResourceInstance& RenderGraph::ResourcePool::AppendResourceInstance(const ResourceId resourceId, const ResourceInstance& resourceInstance)
{
//Modify Begin:2026-07-30 by Hui
    const auto [instance, inserted] = m_ResourceInstances.emplace(resourceId, resourceInstance);
    Assert(inserted, "Render graph resource instance was created more than once.");
    return instance->second;
//Modify End
}
