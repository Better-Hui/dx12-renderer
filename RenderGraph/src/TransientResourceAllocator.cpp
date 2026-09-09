#include "TransientResourceAllocator.h"

#include "RenderPass.h"
#include "ResourceDescription.h"

#include <algorithm>

using namespace RenderGraph;

namespace
{
//Modify Begin:2026-09-09 by Hui
    constexpr uint8_t DirectQueueMask = 1u << 0u;
    constexpr uint8_t AsyncComputeQueueMask = 1u << 1u;
    constexpr uint8_t CopyQueueMask = 1u << 2u;

    uint8_t GetQueueMask(const RenderPassQueue queue)
    {
        switch (queue)
        {
        case RenderPassQueue::AsyncCompute:
            return AsyncComputeQueueMask;
        case RenderPassQueue::Copy:
            return CopyQueueMask;
        case RenderPassQueue::Direct:
        default:
            return DirectQueueMask;
        }
    }

    bool HasSingleQueue(const uint8_t queueMask)
    {
        return queueMask == DirectQueueMask ||
            queueMask == AsyncComputeQueueMask ||
            queueMask == CopyQueueMask;
    }

    bool IsProducerOutput(const RenderGraph::OutputType outputType)
    {
        return outputType == RenderGraph::OutputType::Token ||
            outputType == RenderGraph::OutputType::RenderTarget ||
            outputType == RenderGraph::OutputType::DepthWrite ||
            outputType == RenderGraph::OutputType::UnorderedAccess ||
            outputType == RenderGraph::OutputType::ExternalAccess ||
            outputType == RenderGraph::OutputType::CopyDestination;
    }

    bool DirectlyDependsOn(
        const RenderGraph::RenderPass& pass,
        const RenderGraph::RenderPass& potentialProducer)
    {
        for (const RenderGraph::Input& input : pass.GetInputs())
        {
            for (const RenderGraph::Output& output : potentialProducer.GetOutputs())
            {
                if (IsProducerOutput(output.m_Type) && input.m_Id == output.m_Id)
                {
                    return true;
                }
            }
        }
        return false;
    }

    TransientResourceAllocator::PassReachability BuildPassReachability(
        const std::vector<RenderGraph::RenderPass*>& renderPasses)
    {
        const size_t passCount = renderPasses.size();
        TransientResourceAllocator::PassReachability reachability(
            passCount,
            std::vector<uint8_t>(passCount, 0u));
        for (size_t producerIndex = 0; producerIndex < passCount; ++producerIndex)
        {
            for (size_t consumerIndex = producerIndex + 1u; consumerIndex < passCount; ++consumerIndex)
            {
                if (DirectlyDependsOn(*renderPasses[consumerIndex], *renderPasses[producerIndex]))
                {
                    reachability[producerIndex][consumerIndex] = 1u;
                }
            }
        }

        for (size_t intermediate = 0; intermediate < passCount; ++intermediate)
        {
            for (size_t producerIndex = 0; producerIndex < passCount; ++producerIndex)
            {
                if (reachability[producerIndex][intermediate] == 0u)
                {
                    continue;
                }
                for (size_t consumerIndex = 0; consumerIndex < passCount; ++consumerIndex)
                {
                    reachability[producerIndex][consumerIndex] |=
                        reachability[intermediate][consumerIndex];
                }
            }
        }
        return reachability;
    }

    TransientResourceAllocator::ResourceLifecycle& GetOrAdd(
        std::map<ResourceId, TransientResourceAllocator::ResourceLifecycle>& map,
        const ResourceId& id,
        const uint32_t passIndex,
        const uint8_t queueMask)
    {
        if (!map.contains(id))
        {
            map.insert(std::pair{ id, TransientResourceAllocator::ResourceLifecycle{ id, passIndex, passIndex, queueMask } });
        }

        map[id].m_QueueMask |= queueMask;

        return map[id];
    }
//Modify End

    bool IntersectHelper(const TransientResourceAllocator::ResourceLifecycle& l, const TransientResourceAllocator::ResourceLifecycle& r)
    {
        return
        r.m_BeginPassIndex <= l.m_BeginPassIndex && l.m_BeginPassIndex <= r.m_EndPassIndex ||
        r.m_BeginPassIndex <= l.m_EndPassIndex && l.m_EndPassIndex <= r.m_EndPassIndex;
    }
}

bool TransientResourceAllocator::ResourceLifecycle::Intersect(const ResourceLifecycle& lifecycle1, const ResourceLifecycle& lifecycle2)
{
    return IntersectHelper(lifecycle1, lifecycle2) || IntersectHelper(lifecycle2, lifecycle1);
}

//Modify Begin:2026-09-09 by Hui
bool TransientResourceAllocator::ResourceLifecycle::UsesSingleQueue() const
{
    return HasSingleQueue(m_QueueMask);
}

RenderPassQueue TransientResourceAllocator::ResourceLifecycle::GetQueue() const
{
    Assert(UsesSingleQueue(), "A transient resource lifecycle does not belong to exactly one queue.");
    if (m_QueueMask == AsyncComputeQueueMask)
    {
        return RenderPassQueue::AsyncCompute;
    }
    if (m_QueueMask == CopyQueueMask)
    {
        return RenderPassQueue::Copy;
    }
    return RenderPassQueue::Direct;
}

bool TransientResourceAllocator::ResourceLifecycle::CanAlias(
    const ResourceLifecycle& lifecycle1,
    const ResourceLifecycle& lifecycle2,
    const PassReachability& passReachability)
{
    if (!lifecycle1.UsesSingleQueue() ||
        !lifecycle2.UsesSingleQueue() ||
        Intersect(lifecycle1, lifecycle2))
    {
        return false;
    }
    if (lifecycle1.m_QueueMask == lifecycle2.m_QueueMask)
    {
        return true;
    }

    const ResourceLifecycle& earlier = lifecycle1.m_EndPassIndex < lifecycle2.m_BeginPassIndex
        ? lifecycle1
        : lifecycle2;
    const ResourceLifecycle& later = &earlier == &lifecycle1 ? lifecycle2 : lifecycle1;
    return earlier.m_EndPassIndex < passReachability.size() &&
        later.m_BeginPassIndex < passReachability[earlier.m_EndPassIndex].size() &&
        passReachability[earlier.m_EndPassIndex][later.m_BeginPassIndex] != 0u;
}
//Modify End

//Modify Begin:2026-08-24 by Hui
std::map<ResourceId, TransientResourceAllocator::ResourceLifecycle> TransientResourceAllocator::GetResourceLifecycles(
    const std::vector<RenderPass*>& renderPasses,
    const std::map<ResourceId, ResourceDescription>& resourceDescriptions,
    const std::vector<ResourceId>& externalOutputIds)
//Modify End
{
    std::map<ResourceId, ResourceLifecycle> lifecycles;

    for (uint32_t passIndex = 0; passIndex < renderPasses.size(); ++passIndex)
    {
        const auto& pass = *renderPasses[passIndex];
//Modify Begin:2026-07-30 by Hui
        const uint8_t queueMask = GetQueueMask(pass.GetQueue());
//Modify End

        for (const auto& output : pass.GetOutputs())
        {
//Modify Begin:2026-08-24 by Hui
            if (output.m_Type == OutputType::ExternalAccess)
            {
                continue;
            }
//Modify End
            auto& lifecycle = GetOrAdd(lifecycles, output.m_Id, passIndex, queueMask);
            lifecycle.m_EndPassIndex = passIndex;
        }

        for (const auto& input : pass.GetInputs())
        {
//Modify Begin:2026-08-24 by Hui
            if (input.m_Type == InputType::ExternalAccess)
            {
                continue;
            }
//Modify End
            auto& lifecycle = GetOrAdd(lifecycles, input.m_Id, passIndex, queueMask);
            const auto resourceDescription = resourceDescriptions.find(input.m_Id);
            const bool isPersistentHistory = resourceDescription != resourceDescriptions.end() &&
                resourceDescription->second.m_DedicatedResource &&
                resourceDescription->second.m_TextureDescription.m_InitAction == ResourceInitAction::Preserve;
            Assert(
                lifecycle.m_BeginPassIndex != passIndex || isPersistentHistory,
                "A transient resource's first usage cannot be as an input.");

            lifecycle.m_EndPassIndex = passIndex;
        }
    }

//Modify Begin:2026-07-28 by Hui
    {
        const uint32_t lastPassIndex = renderPasses.empty()
            ? 0u
            : static_cast<uint32_t>(renderPasses.size()) - 1u;
        for (const ResourceId externalOutputId : externalOutputIds)
        {
            auto& lifecycle = GetOrAdd(lifecycles, externalOutputId, lastPassIndex, DirectQueueMask);
            lifecycle.m_EndPassIndex = lastPassIndex;
        }
    }
//Modify End

    return lifecycles;
}


//Modify Begin:2026-09-09 by Hui
std::vector<TransientResourceAllocator::HeapInfo> TransientResourceAllocator::CreateHeaps(
    const std::map<ResourceId, ResourceLifecycle>& lifecycles,
    const std::map<ResourceId, ResourceDescription>& resourceDescriptions,
    const std::vector<RenderPass*>& renderPasses,
    const Microsoft::WRL::ComPtr<ID3D12Device2>& pDevice)
//Modify End
{
    std::vector<HeapInfo> heaps;
//Modify Begin:2026-09-09 by Hui
    const PassReachability passReachability = BuildPassReachability(renderPasses);
//Modify End

    for (const auto& [id, lifecycle] : lifecycles)
    {
        // actually used resources should always be registered at this point
        if (const auto findResult = resourceDescriptions.find(id); findResult != resourceDescriptions.end())
        {
//Modify Begin:2026-07-28 by Hui
            if (findResult->second.m_DedicatedResource)
            {
                continue;
            }
//Modify End

            HeapInfo heapInfo;
            heapInfo.m_Size = findResult->second.m_TotalSize;
            heapInfo.m_Alignment = findResult->second.m_Alignment;
            heapInfo.m_ResourceLifecycles.push_back(lifecycle);

            heaps.emplace_back(std::move(heapInfo));
        }
    }

    bool compacting;

    do
    {
        compacting = false;

        for (uint32_t expandingIndex = 0; expandingIndex + 1 < heaps.size(); ++expandingIndex)
        {
            auto& expandingHeap = heaps[expandingIndex];
            uint32_t theBiggestFittingHeapIndex = -1;
            uint64_t theBiggestFittingHeapSize = 0;

            for (uint32_t otherIndex = expandingIndex + 1; otherIndex < heaps.size(); ++otherIndex)
            {
                if (const auto& otherHeap = heaps[otherIndex];
                    otherHeap.m_Size <= expandingHeap.m_Size &&
                    otherHeap.m_Alignment == expandingHeap.m_Alignment &&
                    otherHeap.m_ResourceLifecycles.size() == 1
                    )
                {
                    const auto& otherLifecycle = otherHeap.m_ResourceLifecycles[0];

                    bool intersect = false;

                    for (const auto& expandingLifecycle : expandingHeap.m_ResourceLifecycles)
                    {
                        if (!ResourceLifecycle::CanAlias(
                            expandingLifecycle,
                            otherLifecycle,
                            passReachability))
                        {
                            intersect = true;
                            break;
                        }
                    }

                    if (!intersect)
                    {
                        if (theBiggestFittingHeapIndex == -1 || otherHeap.m_Size > theBiggestFittingHeapSize)
                        {
                            theBiggestFittingHeapIndex = otherIndex;
                            theBiggestFittingHeapSize = otherHeap.m_Size;
                        }
                    }
                }
            }

            if (theBiggestFittingHeapIndex != -1)
            {
                {
                    const auto& otherHeap = heaps[theBiggestFittingHeapIndex];
                    const auto& otherLifecycle = otherHeap.m_ResourceLifecycles[0];

                    for (const auto& resourceLifecycle : expandingHeap.m_ResourceLifecycles)
                    {
                        Assert(
                            ResourceLifecycle::CanAlias(
                                resourceLifecycle,
                                otherLifecycle,
                                passReachability),
                            "Some of the existing lifecycles cannot safely alias the newly added one.");
                    }

                    expandingHeap.m_ResourceLifecycles.push_back(otherLifecycle);
                }

                heaps.erase(heaps.begin() + theBiggestFittingHeapIndex);
                compacting = true;
                break;
            }
        }

    }
    while (compacting);

//Modify Begin:2026-09-09 by Hui
    for (HeapInfo& heapInfo : heaps)
    {
        std::ranges::sort(
            heapInfo.m_ResourceLifecycles,
            [](const ResourceLifecycle& left, const ResourceLifecycle& right)
            {
                return left.m_BeginPassIndex < right.m_BeginPassIndex;
            });
    }
//Modify End

    for (uint32_t i = 0; i < heaps.size(); ++i)
    {
        auto& heapInfo = heaps[i];
        D3D12_RESOURCE_ALLOCATION_INFO allocationInfo = { heapInfo.m_Size, heapInfo.m_Alignment };
        const auto heapDesc = CD3DX12_HEAP_DESC(allocationInfo, D3D12_HEAP_TYPE_DEFAULT, D3D12_HEAP_FLAG_NONE);

        ThrowIfFailed(pDevice->CreateHeap(&heapDesc, IID_PPV_ARGS(&heapInfo.m_Heap)));

        const auto name = L"RenderGraph-TransientResourceHeap-" + std::to_wstring(i);
        heapInfo.m_Heap->SetName(name.c_str());
    }

    return heaps;
}
