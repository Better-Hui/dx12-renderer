//Modify Begin:2026-07-30 by BestHui
#pragma once

#include <d3d12.h>
#include <wrl.h>

#include <cstdint>
#include <map>

class CommandList;
class Resource;
struct PipelineDescriptorTableAllocation;

struct BindlessDescriptorHeapDesc
{
    uint32_t ResourceDescriptorCapacity = 65536;
};

class BindlessDescriptorHeap final
{
public:
    explicit BindlessDescriptorHeap(BindlessDescriptorHeapDesc desc = {});

    void Reset();
    uint32_t AddShaderResourceView(const Resource& resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc = nullptr);
    void UpdateShaderResourceView(uint32_t descriptorIndex, const Resource& resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc = nullptr);
    void TransitionShaderResources(CommandList& commandList, D3D12_RESOURCE_STATES stateAfter) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetOrCreateDescriptorTable(const PipelineDescriptorTableAllocation& allocation);

    ID3D12DescriptorHeap* GetResourceDescriptorHeap() const { return m_ResourceDescriptorHeap.Get(); }
    uint32_t GetResourceDescriptorCapacity() const { return m_Desc.ResourceDescriptorCapacity; }
    uint32_t GetResourceDescriptorCount() const { return m_NextResourceDescriptorIndex; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetResourceGpuHandle(uint32_t descriptorIndex = 0) const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetResourceCpuHandle(uint32_t descriptorIndex = 0) const;

private:
    BindlessDescriptorHeapDesc m_Desc;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_ResourceDescriptorHeap;
    uint32_t m_ResourceDescriptorSize = 0;
    uint32_t m_NextResourceDescriptorIndex = 0;
    struct CachedDescriptorTable
    {
        uint32_t DescriptorIndex = 0;
        uint32_t DescriptorCount = 0;
        uint64_t Revision = 0;
    };
    std::map<uint32_t, const Resource*> m_ShaderResources;
    std::map<uint64_t, CachedDescriptorTable> m_CachedDescriptorTables;
};
//Modify End
