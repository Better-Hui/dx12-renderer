#include "PipelineDescriptorSet.h"

//Modify Begin:2026-07-27 by BestHui

#include <DX12Library/CommandList.h>
#include <DX12Library/Application.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/StructuredBuffer.h>
#include <Framework/CommandContext.h>
#include <Framework/PipelineDescriptorPool.h>
#include <Framework/RayTracingAccelerationStructure.h>

#include <cstring>

//Modify Begin:2026-07-29 by BestHui
namespace
{
    bool IsSameShaderResourceView(const ShaderResourceView& lhs, const ShaderResourceView& rhs)
    {
        if (lhs.m_Resource.get() != rhs.m_Resource.get() ||
            lhs.m_FirstSubresource != rhs.m_FirstSubresource ||
            lhs.m_NumSubresources != rhs.m_NumSubresources ||
            lhs.m_IsDescValid != rhs.m_IsDescValid)
        {
            return false;
        }

        return !lhs.m_IsDescValid || std::memcmp(&lhs.m_Desc, &rhs.m_Desc, sizeof(D3D12_SHADER_RESOURCE_VIEW_DESC)) == 0;
    }

    bool IsSameShaderResourceBinding(const PipelineShaderResourceBinding& lhs, const PipelineShaderResourceBinding& rhs)
    {
        if (lhs.Resource != rhs.Resource ||
            lhs.StateAfter != rhs.StateAfter ||
            lhs.FirstSubresource != rhs.FirstSubresource ||
            lhs.NumSubresources != rhs.NumSubresources ||
            lhs.HasDesc != rhs.HasDesc ||
            lhs.AutoTransition != rhs.AutoTransition)
        {
            return false;
        }

        return !lhs.HasDesc || std::memcmp(&lhs.Desc, &rhs.Desc, sizeof(D3D12_SHADER_RESOURCE_VIEW_DESC)) == 0;
    }

    bool IsSameUnorderedAccessView(const UnorderedAccessView& lhs, const UnorderedAccessView& rhs)
    {
        if (lhs.m_Resource.get() != rhs.m_Resource.get() ||
            lhs.m_FirstSubresource != rhs.m_FirstSubresource ||
            lhs.m_NumSubresources != rhs.m_NumSubresources ||
            lhs.m_IsDescValid != rhs.m_IsDescValid)
        {
            return false;
        }

        return !lhs.m_IsDescValid || std::memcmp(&lhs.m_Desc, &rhs.m_Desc, sizeof(D3D12_UNORDERED_ACCESS_VIEW_DESC)) == 0;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GetDescriptorHandle(
        const PipelineDescriptorSet& descriptorSet,
        const PipelineDescriptorTableAllocation& allocation,
        const uint32_t offset = 0)
    {
        (void)descriptorSet;
        Assert(allocation.IsValid() && offset < allocation.NumHandles, "Pipeline descriptor table CPU handle is invalid.");
        return allocation.GetDescriptorHandle(offset);
    }
}
//Modify End

//Modify Begin:2026-07-29 by BestHui
D3D12_CPU_DESCRIPTOR_HANDLE PipelineDescriptorTableAllocation::GetDescriptorHandle(const uint32_t offset) const
{
    Assert(IsValid() && offset < NumHandles, "Pipeline descriptor table CPU handle is invalid.");
    return CpuDescriptors.GetDescriptorHandle(offset);
}
//Modify End

PipelineDescriptorSet::PipelineDescriptorSet(const PipelineLayout& layout)
    : m_Bindings(layout)
    , m_Layout(&layout)
{
}

void PipelineDescriptorSet::Reset(const PipelineLayout& layout)
{
    m_Bindings.Reset(layout);
    m_Layout = &layout;
    m_BoundResources.clear();
//Modify Begin:2026-07-27 by BestHui
    m_DescriptorTableAllocations.clear();
//Modify End
//Modify Begin:2026-07-29 by BestHui
    m_DescriptorPool = nullptr;
    m_SetIndex = 0;
    m_ResourceDescriptorOffset = 0;
    m_SamplerDescriptorOffset = 0;
    m_Allocation = {};
//Modify End
    m_AccelerationStructure = nullptr;
}

bool PipelineDescriptorSet::HasBinding(std::string_view name) const
{
    return m_Bindings.HasBinding(name);
}

bool PipelineDescriptorSet::HasBinding(std::string_view name, const DescriptorBindingKind expectedKind) const
{
    return m_Bindings.HasBinding(name, expectedKind);
}

const PipelineDescriptorRangeDesc& PipelineDescriptorSet::GetRange(
    std::string_view name,
    const DescriptorBindingKind expectedKind) const
{
    return m_Bindings.GetRange(name, expectedKind);
}

const DescriptorBindingInfo& PipelineDescriptorSet::GetBinding(
    std::string_view name,
    const DescriptorBindingKind expectedKind) const
{
    return m_Bindings.GetBinding(name, expectedKind);
}

void PipelineDescriptorSet::ValidateArrayIndex(
    std::string_view name,
    const DescriptorBindingKind expectedKind,
    const UINT arrayIndex) const
{
    m_Bindings.ValidateArrayIndex(name, expectedKind, arrayIndex);
}

UINT PipelineDescriptorSet::SetShaderResourceView(
    std::string_view name,
    const UINT arrayIndex,
    const ShaderResourceView& shaderResourceView)
{
    ValidateArrayIndex(name, DescriptorBindingKind::ShaderResourceView, arrayIndex);
    Assert(shaderResourceView.m_Resource != nullptr, "Pipeline SRV resource must not be null.");

    const DescriptorBindingInfo& binding = GetBinding(name, DescriptorBindingKind::ShaderResourceView);
    auto& shaderResourceViews = m_BoundResources[binding.RootParameterIndex].ShaderResourceViews;
    if (shaderResourceViews.size() <= arrayIndex)
    {
        shaderResourceViews.resize(static_cast<size_t>(arrayIndex) + 1u);
    }
//Modify Begin:2026-07-29 by BestHui
    const bool descriptorChanged =
        !shaderResourceViews[arrayIndex].has_value() ||
        !IsSameShaderResourceView(*shaderResourceViews[arrayIndex], shaderResourceView);
//Modify End
    shaderResourceViews[arrayIndex] = shaderResourceView;

    auto& shaderResources = m_BoundResources[binding.RootParameterIndex].ShaderResources;
    if (shaderResources.size() <= arrayIndex)
    {
        shaderResources.resize(static_cast<size_t>(arrayIndex) + 1u);
    }
    PipelineShaderResourceBinding resourceBinding = {};
    resourceBinding.Resource = shaderResourceView.m_Resource.get();
    resourceBinding.StateAfter = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
    resourceBinding.FirstSubresource = shaderResourceView.m_FirstSubresource;
    resourceBinding.NumSubresources = shaderResourceView.m_NumSubresources;
    resourceBinding.HasDesc = shaderResourceView.GetDescOrNullptr() != nullptr;
    if (resourceBinding.HasDesc)
    {
        resourceBinding.Desc = *shaderResourceView.GetDescOrNullptr();
    }
    shaderResources[arrayIndex] = resourceBinding;

//Modify Begin:2026-07-27 by BestHui
    if (descriptorChanged)
    {
        if (const PipelineDescriptorTableAllocation* allocation = FindDescriptorTableAllocation(binding.RootParameterIndex))
        {
                Application::Get().GetDevice()->CopyDescriptorsSimple(
                    1u,
                    GetDescriptorHandle(*this, *allocation, arrayIndex),
                    shaderResourceView.m_Resource->GetShaderResourceView(shaderResourceView.GetDescOrNullptr()),
                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
    }
//Modify End

    return binding.RootParameterIndex;
}

UINT PipelineDescriptorSet::SetShaderResource(
    std::string_view name,
    const UINT arrayIndex,
    const Resource& resource,
    const D3D12_RESOURCE_STATES stateAfter)
{
    ValidateArrayIndex(name, DescriptorBindingKind::ShaderResourceView, arrayIndex);

    const DescriptorBindingInfo& binding = GetBinding(name, DescriptorBindingKind::ShaderResourceView);
    auto& shaderResources = m_BoundResources[binding.RootParameterIndex].ShaderResources;
    if (shaderResources.size() <= arrayIndex)
    {
        shaderResources.resize(static_cast<size_t>(arrayIndex) + 1u);
    }

    PipelineShaderResourceBinding resourceBinding = {};
    resourceBinding.Resource = &resource;
    resourceBinding.StateAfter = stateAfter;
//Modify Begin:2026-07-29 by BestHui
    const bool descriptorChanged =
        !shaderResources[arrayIndex].has_value() ||
        !IsSameShaderResourceBinding(*shaderResources[arrayIndex], resourceBinding);
//Modify End
    shaderResources[arrayIndex] = resourceBinding;
//Modify Begin:2026-07-27 by BestHui
    if (descriptorChanged)
    {
        if (const PipelineDescriptorTableAllocation* allocation = FindDescriptorTableAllocation(binding.RootParameterIndex))
        {
                Application::Get().GetDevice()->CopyDescriptorsSimple(
                    1u,
                    GetDescriptorHandle(*this, *allocation, arrayIndex),
                    resource.GetShaderResourceView(nullptr),
                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
    }
//Modify End
    return binding.RootParameterIndex;
}

UINT PipelineDescriptorSet::SetShaderResource(
    std::string_view name,
    const UINT arrayIndex,
    const Resource& resource,
    const D3D12_RESOURCE_STATES stateAfter,
    const D3D12_SHADER_RESOURCE_VIEW_DESC& srvDesc)
{
    ValidateArrayIndex(name, DescriptorBindingKind::ShaderResourceView, arrayIndex);

    const DescriptorBindingInfo& binding = GetBinding(name, DescriptorBindingKind::ShaderResourceView);
    auto& shaderResources = m_BoundResources[binding.RootParameterIndex].ShaderResources;
    if (shaderResources.size() <= arrayIndex)
    {
        shaderResources.resize(static_cast<size_t>(arrayIndex) + 1u);
    }

    PipelineShaderResourceBinding resourceBinding = {};
    resourceBinding.Resource = &resource;
    resourceBinding.StateAfter = stateAfter;
    resourceBinding.HasDesc = true;
    resourceBinding.Desc = srvDesc;
    resourceBinding.AutoTransition = false;
//Modify Begin:2026-07-29 by BestHui
    const bool descriptorChanged =
        !shaderResources[arrayIndex].has_value() ||
        !IsSameShaderResourceBinding(*shaderResources[arrayIndex], resourceBinding);
//Modify End
    shaderResources[arrayIndex] = resourceBinding;
//Modify Begin:2026-07-27 by BestHui
    if (descriptorChanged)
    {
        if (const PipelineDescriptorTableAllocation* allocation = FindDescriptorTableAllocation(binding.RootParameterIndex))
        {
            Application::Get().GetDevice()->CreateShaderResourceView(
                resource.GetD3D12Resource().Get(),
                &srvDesc,
                GetDescriptorHandle(*this, *allocation, arrayIndex));
        }
    }
//Modify End
    return binding.RootParameterIndex;
}

UINT PipelineDescriptorSet::SetUnorderedAccessView(
    std::string_view name,
    const UnorderedAccessView& unorderedAccessView)
{
    Assert(unorderedAccessView.m_Resource != nullptr, "Pipeline UAV resource must not be null.");

    const DescriptorBindingInfo& binding = GetBinding(name, DescriptorBindingKind::UnorderedAccessView);
//Modify Begin:2026-07-29 by BestHui
    const auto existingResource = m_BoundResources.find(binding.RootParameterIndex);
    const bool descriptorChanged =
        existingResource == m_BoundResources.end() ||
        !existingResource->second.UnorderedAccessView.has_value() ||
        !IsSameUnorderedAccessView(*existingResource->second.UnorderedAccessView, unorderedAccessView);
//Modify End
    m_BoundResources[binding.RootParameterIndex].UnorderedAccessView = unorderedAccessView;
//Modify Begin:2026-07-27 by BestHui
    if (descriptorChanged)
    {
        if (const PipelineDescriptorTableAllocation* allocation = FindDescriptorTableAllocation(binding.RootParameterIndex))
        {
            Application::Get().GetDevice()->CopyDescriptorsSimple(
                1u,
                GetDescriptorHandle(*this, *allocation, 0u),
                unorderedAccessView.m_Resource->GetUnorderedAccessView(unorderedAccessView.GetDescOrNullptr()),
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
    }
//Modify End
    return binding.RootParameterIndex;
}

UINT PipelineDescriptorSet::SetStructuredBuffer(std::string_view name, const StructuredBuffer& buffer)
{
    const DescriptorBindingInfo& binding = GetBinding(name, DescriptorBindingKind::ShaderResourceView);
    m_BoundResources[binding.RootParameterIndex].StructuredBufferResource = &buffer;
    return binding.RootParameterIndex;
}

UINT PipelineDescriptorSet::SetAccelerationStructure(
    std::string_view name,
    const RayTracingAccelerationStructure& accelerationStructure)
{
    const DescriptorBindingInfo& binding = GetBinding(name, DescriptorBindingKind::AccelerationStructure);
    auto& boundResource = m_BoundResources[binding.RootParameterIndex];
    boundResource.AccelerationStructure = &accelerationStructure;
    m_AccelerationStructure = &accelerationStructure;
    return binding.RootParameterIndex;
}

UINT PipelineDescriptorSet::SetConstantBufferData(
    std::string_view name,
    const void* data,
    const size_t size)
{
    Assert(data != nullptr && size > 0, "Pipeline constant buffer data must not be empty.");

    const DescriptorBindingInfo& binding = GetBinding(name, DescriptorBindingKind::ConstantBuffer);
    auto& constantBufferData = m_BoundResources[binding.RootParameterIndex].ConstantBufferData;
    constantBufferData.resize(size);
    std::memcpy(constantBufferData.data(), data, size);
    return binding.RootParameterIndex;
}

void PipelineDescriptorSet::ClearShaderResourceViews(std::string_view name)
{
    const DescriptorBindingInfo& binding = GetBinding(name, DescriptorBindingKind::ShaderResourceView);
    m_BoundResources[binding.RootParameterIndex].ShaderResourceViews.clear();
    m_BoundResources[binding.RootParameterIndex].ShaderResources.clear();
//Modify Begin:2026-07-27 by BestHui
    const PipelineDescriptorTableAllocation* allocation = FindDescriptorTableAllocation(binding.RootParameterIndex);
    const DescriptorAllocation* defaultDescriptors = GetLayout().FindDefaultDescriptorTable(binding.RootParameterIndex);
    if (allocation != nullptr && defaultDescriptors != nullptr)
    {
//Modify Begin:2026-07-29 by BestHui
        const uint32_t descriptorCount = allocation->GetNumHandles();
        const uint32_t defaultDescriptorCount = defaultDescriptors->GetNumHandles();
        const uint32_t copiedDescriptorCount = (std::min)(descriptorCount, defaultDescriptorCount);
        Application::Get().GetDevice()->CopyDescriptorsSimple(
            copiedDescriptorCount,
            GetDescriptorHandle(*this, *allocation),
            defaultDescriptors->GetDescriptorHandle(),
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        for (uint32_t descriptorIndex = copiedDescriptorCount; descriptorIndex < descriptorCount; ++descriptorIndex)
        {
            Application::Get().GetDevice()->CopyDescriptorsSimple(
                1u,
                GetDescriptorHandle(*this, *allocation, descriptorIndex),
                defaultDescriptors->GetDescriptorHandle(0u),
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
//Modify End
    }
//Modify End
}

void PipelineDescriptorSet::ApplyGraphicsBinding(CommandList& commandList, const UINT rootParameterIndex) const
{
    CommandContext(commandList).ApplyGraphicsBinding(*this, rootParameterIndex);
}

void PipelineDescriptorSet::ApplyComputeBinding(CommandList& commandList, const UINT rootParameterIndex) const
{
    CommandContext(commandList).ApplyComputeBinding(*this, rootParameterIndex);
}

const PipelineLayout& PipelineDescriptorSet::GetLayout() const
{
    Assert(m_Layout != nullptr, "Pipeline descriptor set has no layout.");
    return *m_Layout;
}

//Modify Begin:2026-07-27 by BestHui
void PipelineDescriptorSet::SetDescriptorTableAllocation(
    const UINT rootParameterIndex,
    PipelineDescriptorTableAllocation allocation)
{
    m_DescriptorTableAllocations.insert_or_assign(rootParameterIndex, std::move(allocation));
}

const PipelineDescriptorTableAllocation* PipelineDescriptorSet::FindDescriptorTableAllocation(const UINT rootParameterIndex) const
{
    const auto findResult = m_DescriptorTableAllocations.find(rootParameterIndex);
    return findResult != m_DescriptorTableAllocations.end() ? &findResult->second : nullptr;
}
//Modify End

//Modify Begin:2026-07-29 by BestHui
void PipelineDescriptorSet::SetAllocationInfo(
    const PipelineDescriptorPool* descriptorPool,
    const uint32_t setIndex,
    const uint32_t resourceDescriptorOffset,
    const uint32_t samplerDescriptorOffset,
    const uint32_t resourceDescriptorCount,
    const uint32_t samplerDescriptorCount)
{
    m_DescriptorPool = descriptorPool;
    m_SetIndex = setIndex;
    m_ResourceDescriptorOffset = resourceDescriptorOffset;
    m_SamplerDescriptorOffset = samplerDescriptorOffset;
    m_Allocation = {};
    m_Allocation.SetIndex = setIndex;
    m_Allocation.HeapOffsets[static_cast<size_t>(PipelineDescriptorHeapType::Resource)] = resourceDescriptorOffset;
    m_Allocation.HeapOffsets[static_cast<size_t>(PipelineDescriptorHeapType::Sampler)] = samplerDescriptorOffset;
    m_Allocation.DescriptorCounts[static_cast<size_t>(PipelineDescriptorHeapType::Resource)] = resourceDescriptorCount;
    m_Allocation.DescriptorCounts[static_cast<size_t>(PipelineDescriptorHeapType::Sampler)] = samplerDescriptorCount;
}
//Modify End

const PipelineBoundResource* PipelineDescriptorSet::FindBoundResource(const UINT rootParameterIndex) const
{
    const auto findResult = m_BoundResources.find(rootParameterIndex);
    return findResult != m_BoundResources.end() ? &findResult->second : nullptr;
}

const PipelineBoundResource& PipelineDescriptorSet::GetBoundResource(const UINT rootParameterIndex) const
{
    const PipelineBoundResource* boundResource = FindBoundResource(rootParameterIndex);
    Assert(boundResource != nullptr, "Pipeline descriptor set resource is not bound.");
    return *boundResource;
}

//Modify End
