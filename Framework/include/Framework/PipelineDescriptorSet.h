#pragma once

//Modify Begin:2026-07-27 by BestHui

#include "PipelineBindingSet.h"
#include "ShaderResourceView.h"
#include "UnorderedAccessView.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

class RayTracingAccelerationStructure;
class CommandList;
class Resource;
class StructuredBuffer;

struct PipelineShaderResourceBinding
{
    const Resource* Resource = nullptr;
    D3D12_RESOURCE_STATES StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    UINT FirstSubresource = 0;
    UINT NumSubresources = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    bool HasDesc = false;
    D3D12_SHADER_RESOURCE_VIEW_DESC Desc = {};
};

struct PipelineBoundResource
{
    std::optional<UnorderedAccessView> UnorderedAccessView;
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

    UINT SetShaderResourceView(std::string_view name, UINT arrayIndex, const ShaderResourceView& shaderResourceView);
    UINT SetShaderResource(std::string_view name, UINT arrayIndex, const Resource& resource, D3D12_RESOURCE_STATES stateAfter);
    UINT SetUnorderedAccessView(std::string_view name, const UnorderedAccessView& unorderedAccessView);
    UINT SetStructuredBuffer(std::string_view name, const StructuredBuffer& buffer);
    UINT SetAccelerationStructure(std::string_view name, const RayTracingAccelerationStructure& accelerationStructure);
    UINT SetConstantBufferData(std::string_view name, const void* data, size_t size);
    void ClearShaderResourceViews(std::string_view name);

    void ApplyGraphicsBinding(CommandList& commandList, UINT rootParameterIndex) const;
    void ApplyComputeBinding(CommandList& commandList, UINT rootParameterIndex) const;

    const PipelineLayout& GetLayout() const;
    const PipelineBoundResource* FindBoundResource(UINT rootParameterIndex) const;
    const PipelineBoundResource& GetBoundResource(UINT rootParameterIndex) const;
    const std::map<UINT, PipelineBoundResource>& GetBoundResources() const { return m_BoundResources; }
    const RayTracingAccelerationStructure* GetAccelerationStructure() const { return m_AccelerationStructure; }

private:
    PipelineBindingSet m_Bindings;
    const PipelineLayout* m_Layout = nullptr;
    std::map<UINT, PipelineBoundResource> m_BoundResources;
    const RayTracingAccelerationStructure* m_AccelerationStructure = nullptr;
};

//Modify End
