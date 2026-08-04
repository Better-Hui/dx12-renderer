#pragma once

#include <cstdint>
#include <map>
#include <vector>

#include <wrl.h>

#include <DX12Library/Helpers.h>

#include "ResourceId.h"

namespace RenderGraph
{
    class RenderPass;
    struct ResourceDescription;

    class TransientResourceAllocator
    {
    public:
        struct ResourceLifecycle
        {
            ResourceId m_Id;
            uint32_t m_BeginPassIndex;
            uint32_t m_EndPassIndex;
//Modify Begin:2026-07-30 by BestHui
            uint8_t m_QueueMask = 0;
//Modify End

            static bool Intersect(const ResourceLifecycle& lifecycle1, const ResourceLifecycle& lifecycle2);
//Modify Begin:2026-07-30 by BestHui
            static bool CanAlias(const ResourceLifecycle& lifecycle1, const ResourceLifecycle& lifecycle2);
//Modify End
        };

        struct HeapInfo
        {
            uint64_t m_Size = 0;
            uint64_t m_Alignment = 0;
            std::vector<ResourceLifecycle> m_ResourceLifecycles;
            Microsoft::WRL::ComPtr<ID3D12Heap> m_Heap;
        };

//Modify Begin:2026-07-28 by BestHui
        static std::map<ResourceId, ResourceLifecycle> GetResourceLifecycles(
            const std::vector<RenderPass*>& renderPasses,
            const std::vector<ResourceId>& externalOutputIds = { ResourceIds::GRAPH_OUTPUT });
//Modify End
        static std::vector<HeapInfo> CreateHeaps(const std::map<ResourceId, ResourceLifecycle>& lifecycles, const std::map<ResourceId, ResourceDescription>& resourceDescriptions, const Microsoft::WRL::ComPtr<ID3D12Device2>& pDevice);
    };
}
