#pragma once

//Modify Begin:2026-07-27 by Hui

#include <Framework/Rendering/Pipeline/PipelineBindingSet.h>
#include <Framework/Rendering/Pipeline/PipelineLayout.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>
#include <Framework/Rendering/Texture/UnorderedAccessView.h>

#include <DX12Library/DescriptorAllocation.h>

#include <cstddef>
#include <cstdint>
#include <array>
#include <map>
#include <optional>
#include <span>
#include <vector>

class RayTracingAccelerationStructure;
class CommandList;
class PipelineDescriptorPool;
class Resource;
class StructuredBuffer;

//Modify Begin:2026-07-29 by Hui
enum class PipelineDescriptorHeapType : uint32_t
{
    Resource = 0,
    Sampler = 1,
    Count
};

struct PipelineDescriptorSetAllocation
{
    uint32_t SetIndex = 0;
    std::array<uint32_t, static_cast<size_t>(PipelineDescriptorHeapType::Count)> HeapOffsets = {};
    std::array<uint32_t, static_cast<size_t>(PipelineDescriptorHeapType::Count)> DescriptorCounts = {};
};
//Modify End

//Modify Begin:2026-07-29 by Hui
struct PipelineDescriptorTableAllocation
{
    PipelineDescriptorHeapType HeapType = PipelineDescriptorHeapType::Resource;
    uint32_t HeapOffset = 0;
    uint32_t NumHandles = 0;
    uint64_t Revision = 1;
    DescriptorAllocation CpuDescriptors;

    bool IsValid() const { return NumHandles > 0; }
    D3D12_CPU_DESCRIPTOR_HANDLE GetDescriptorHandle(uint32_t offset = 0) const;
    uint32_t GetNumHandles() const { return NumHandles; }
    uint64_t GetRevision() const { return Revision; }
    void MarkDirty() { ++Revision; }
};
//Modify End

struct PipelineShaderResourceBinding
{
    const Resource* Resource = nullptr;
//Modify Begin:2026-07-30 by Hui
    ID3D12Resource* ResourceIdentity = nullptr;
//Modify End
    D3D12_RESOURCE_STATES StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    UINT FirstSubresource = 0;
    UINT NumSubresources = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    bool HasDesc = false;
    D3D12_SHADER_RESOURCE_VIEW_DESC Desc = {};
//Modify Begin:2026-07-27 by Hui
    bool AutoTransition = true;
//Modify End
};

struct PipelineBoundResource
{
    std::optional<UnorderedAccessView> UnorderedAccessView;
//Modify Begin:2026-07-30 by Hui
    ID3D12Resource* UnorderedAccessViewResourceIdentity = nullptr;
//Modify End
    std::vector<std::optional<ShaderResourceView>> ShaderResourceViews;
    std::vector<std::optional<PipelineShaderResourceBinding>> ShaderResources;
    const StructuredBuffer* StructuredBufferResource = nullptr;
    const RayTracingAccelerationStructure* AccelerationStructure = nullptr;
    std::vector<uint8_t> ConstantBufferData;
};

class PipelineDescriptorSet
{
public:
    PipelineDescriptorSet() = default;
    explicit PipelineDescriptorSet(const PipelineLayout& layout);

    void Reset(const PipelineLayout& layout);

    bool HasBinding(std::string_view name) const;
    bool HasBinding(std::string_view name, DescriptorBindingKind expectedKind) const;
    const PipelineDescriptorRangeDesc& GetRange(std::string_view name, DescriptorBindingKind expectedKind) const;
    const DescriptorBindingInfo& GetBinding(std::string_view name, DescriptorBindingKind expectedKind) const;
    void ValidateArrayIndex(std::string_view name, DescriptorBindingKind expectedKind, UINT arrayIndex) const;

    UINT SetShaderResourceView(std::string_view name, UINT arrayIndex, const ShaderResourceView& shaderResourceView, D3D12_RESOURCE_STATES stateAfter = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
    UINT SetShaderResourceViews(std::string_view name, std::span<const ShaderResourceView> shaderResourceViews, D3D12_RESOURCE_STATES stateAfter = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
    UINT SetShaderResource(std::string_view name, UINT arrayIndex, const Resource& resource, D3D12_RESOURCE_STATES stateAfter);
    UINT SetShaderResource(
        std::string_view name,
        UINT arrayIndex,
        const Resource& resource,
        D3D12_RESOURCE_STATES stateAfter,
        const D3D12_SHADER_RESOURCE_VIEW_DESC& srvDesc);
    UINT SetUnorderedAccessView(std::string_view name, const UnorderedAccessView& unorderedAccessView);
//Modify Begin:2026-08-03 by Hui
    UINT SetStructuredBuffer(
        std::string_view name,
        const StructuredBuffer& buffer,
        D3D12_RESOURCE_STATES stateAfter = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
//Modify End
    UINT SetAccelerationStructure(std::string_view name, const RayTracingAccelerationStructure& accelerationStructure);
    UINT SetConstantBufferData(std::string_view name, const void* data, size_t size);
    void ClearShaderResourceViews(std::string_view name);

    const PipelineLayout& GetLayout() const;
//Modify Begin:2026-07-27 by Hui
    void SetDescriptorTableAllocation(UINT rootParameterIndex, PipelineDescriptorTableAllocation allocation);
    const PipelineDescriptorTableAllocation* FindDescriptorTableAllocation(UINT rootParameterIndex) const;
//Modify End
//Modify Begin:2026-07-29 by Hui
    void SetAllocationInfo(
        const PipelineDescriptorPool* descriptorPool,
        uint32_t setIndex,
        uint32_t resourceDescriptorOffset,
        uint32_t samplerDescriptorOffset,
        uint32_t resourceDescriptorCount,
        uint32_t samplerDescriptorCount);
    const PipelineDescriptorPool* GetDescriptorPool() const { return m_DescriptorPool; }
    uint32_t GetSetIndex() const { return m_SetIndex; }
    uint32_t GetResourceDescriptorOffset() const { return m_ResourceDescriptorOffset; }
    uint32_t GetSamplerDescriptorOffset() const { return m_SamplerDescriptorOffset; }
    const PipelineDescriptorSetAllocation& GetAllocation() const { return m_Allocation; }
//Modify End
    const PipelineBoundResource* FindBoundResource(UINT rootParameterIndex) const;
    const PipelineBoundResource& GetBoundResource(UINT rootParameterIndex) const;
    const std::map<UINT, PipelineBoundResource>& GetBoundResources() const { return m_BoundResources; }
    const RayTracingAccelerationStructure* GetAccelerationStructure() const { return m_AccelerationStructure; }

private:
    PipelineBindingSet m_Bindings;
    const PipelineLayout* m_Layout = nullptr;
    std::map<UINT, PipelineBoundResource> m_BoundResources;
//Modify Begin:2026-07-27 by Hui
    std::map<UINT, PipelineDescriptorTableAllocation> m_DescriptorTableAllocations;
    PipelineDescriptorTableAllocation* FindMutableDescriptorTableAllocation(UINT rootParameterIndex);
//Modify End
//Modify Begin:2026-07-29 by Hui
    const PipelineDescriptorPool* m_DescriptorPool = nullptr;
    uint32_t m_SetIndex = 0;
    uint32_t m_ResourceDescriptorOffset = 0;
    uint32_t m_SamplerDescriptorOffset = 0;
    PipelineDescriptorSetAllocation m_Allocation = {};
//Modify End
    const RayTracingAccelerationStructure* m_AccelerationStructure = nullptr;
};

//Modify End
