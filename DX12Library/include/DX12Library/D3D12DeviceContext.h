#pragma once

//Modify Begin:2026-08-07 by Hui
#include "DescriptorAllocator.h"
#include "DescriptorRetirementClock.h"
#include "ResourceStateRegistry.h"

#include <d3d12.h>
#include <wrl.h>

#include <cstdint>
#include <cassert>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

//Modify Begin:2026-08-12 by Hui
struct D3D12TextureCacheKey
{
    std::wstring Path;
    uint32_t Usage = 0;

    bool operator<(const D3D12TextureCacheKey& other) const
    {
        return Path < other.Path || (Path == other.Path && Usage < other.Usage);
    }
};
//Modify End

struct D3D12DeviceContextDesc
{
    Microsoft::WRL::ComPtr<ID3D12Device2> Device;
    std::shared_ptr<ResourceStateRegistry> ResourceStateRegistry;
};

//Modify Begin:2026-08-12 by Hui
struct D3D12TextureCacheEntry
{
    Microsoft::WRL::ComPtr<ID3D12Resource> Resource;
    std::shared_ptr<ResourceStateRegistration> StateRegistration;
};
//Modify End

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
                m_Desc.Device,
                m_DescriptorRetirementClock);
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

    void SetDescriptorRetirementFrame(const uint64_t frameNumber) const
    {
        m_DescriptorRetirementClock->SetCurrentFrame(frameNumber);
    }

//Modify Begin:2026-08-12 by Hui
    uint64_t GetDescriptorRetirementFrame() const
    {
        return m_DescriptorRetirementClock->GetCurrentFrame();
    }

    bool FindCachedTexture(const D3D12TextureCacheKey& key, Microsoft::WRL::ComPtr<ID3D12Resource>& resource) const
    {
        std::lock_guard lock(m_TextureCacheMutex);
        const auto iter = m_TextureCache.find(key);
        if (iter == m_TextureCache.end())
        {
            return false;
        }

        resource = iter->second.Resource;
        return true;
    }

    void CacheTexture(D3D12TextureCacheKey key, Microsoft::WRL::ComPtr<ID3D12Resource> resource)
    {
        assert(resource != nullptr);
        std::lock_guard lock(m_TextureCacheMutex);
        m_TextureCache.insert_or_assign(
            std::move(key),
            D3D12TextureCacheEntry{
                .Resource = resource,
                .StateRegistration = m_Desc.ResourceStateRegistry->AcquireResource(
                    resource.Get(),
                    D3D12_RESOURCE_STATE_COMMON)
            });
    }
//Modify End

private:
    D3D12DeviceContextDesc m_Desc;
    std::shared_ptr<DescriptorRetirementClock> m_DescriptorRetirementClock =
        std::make_shared<DescriptorRetirementClock>();
    std::unique_ptr<DescriptorAllocator> m_DescriptorAllocators[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES];
    std::map<D3D12TextureCacheKey, D3D12TextureCacheEntry> m_TextureCache;
    mutable std::mutex m_TextureCacheMutex;
};
//Modify End
