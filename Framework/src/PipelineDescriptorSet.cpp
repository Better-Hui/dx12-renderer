#include "PipelineDescriptorSet.h"

//Modify Begin:2026-07-27 by BestHui

#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/StructuredBuffer.h>
#include <Framework/CommandContext.h>
#include <Framework/RayTracingAccelerationStructure.h>

#include <cstring>

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
    shaderResources[arrayIndex] = resourceBinding;
    return binding.RootParameterIndex;
}

UINT PipelineDescriptorSet::SetUnorderedAccessView(
    std::string_view name,
    const UnorderedAccessView& unorderedAccessView)
{
    Assert(unorderedAccessView.m_Resource != nullptr, "Pipeline UAV resource must not be null.");

    const DescriptorBindingInfo& binding = GetBinding(name, DescriptorBindingKind::UnorderedAccessView);
    m_BoundResources[binding.RootParameterIndex].UnorderedAccessView = unorderedAccessView;
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
