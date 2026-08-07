#pragma once

//Modify Begin:2026-08-07 by BestHui
#include "DescriptorAllocator.h"

#include <d3d12.h>
#include <wrl.h>

#include <cstdint>
#include <cassert>
#include <functional>
#include <memory>
#include <utility>

class ResourceStateRegistry;

struct D3D12DeviceContextDesc
{
    Microsoft::WRL::ComPtr<ID3D12Device2> Device;
    std::shared_ptr<ResourceStateRegistry> ResourceStateRegistry;
};

class D3D12DeviceContext final
{
public:
    explicit D3D12DeviceContext(D3D12DeviceContextDesc desc)
        : m_Desc(std::move(desc))
    {
        assert(m_Desc.Device != nullptr);
        assert(m_Desc.ResourceStateRegistry != nullptr);
        for (int type = 0; type < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++type)
        {
            m_DescriptorAllocators[type] = std::make_unique<DescriptorAllocator>(
                static_cast<D3D12_DESCRIPTOR_HEAP_TYPE>(type),
                m_Desc.Device);
        }
    }

    const Microsoft::WRL::ComPtr<ID3D12Device2>& GetDevice() const { return m_Desc.Device; }
    const std::shared_ptr<ResourceStateRegistry>& GetResourceStateRegistry() const
    {
        return m_Desc.ResourceStateRegistry;
    }

    DescriptorAllocation AllocateDescriptors(
        const D3D12_DESCRIPTOR_HEAP_TYPE type,
        const uint32_t descriptorCount = 1) const
    {
        return m_DescriptorAllocators[type]->Allocate(descriptorCount);
    }

    void ReleaseStaleDescriptors(const uint64_t frameNumber) const
    {
        for (int type = 0; type < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++type)
        {
            m_DescriptorAllocators[type]->ReleaseStaleDescriptors(frameNumber);
        }
    }

private:
    D3D12DeviceContextDesc m_Desc;
    std::unique_ptr<DescriptorAllocator> m_DescriptorAllocators[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES];
};
//Modify End
