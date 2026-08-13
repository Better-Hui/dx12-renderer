#include <Framework/Rendering/Pipeline/PipelineBindingSet.h>

#include <DX12Library/Helpers.h>

#include <algorithm>
#include <exception>

//Modify Begin:2026-07-24 by BestHui

PipelineBindingSet::PipelineBindingSet(const PipelineLayout& layout)
    : m_Layout(&layout)
{
}

void PipelineBindingSet::Reset(const PipelineLayout& layout)
{
    m_Layout = &layout;
}

bool PipelineBindingSet::HasBinding(std::string_view name) const
{
    return HasBinding(name, DescriptorBindingKind::ConstantBuffer) ||
        HasBinding(name, DescriptorBindingKind::ShaderResourceView) ||
        HasBinding(name, DescriptorBindingKind::UnorderedAccessView) ||
        HasBinding(name, DescriptorBindingKind::AccelerationStructure);
}

bool PipelineBindingSet::HasBinding(std::string_view name, const DescriptorBindingKind expectedKind) const
{
    return FindRange(name, expectedKind) != nullptr;
}

const PipelineDescriptorRangeDesc* PipelineBindingSet::FindRange(
    std::string_view name,
    const DescriptorBindingKind expectedKind) const
{
    return GetLayout().FindRange(std::string(name), expectedKind);
}

const PipelineDescriptorRangeDesc& PipelineBindingSet::GetRange(
    std::string_view name,
    const DescriptorBindingKind expectedKind) const
{
    const PipelineDescriptorRangeDesc* range = FindRange(name, expectedKind);
    if (range == nullptr)
    {
        throw std::exception("Pipeline binding was not found.");
    }

    return *range;
}

const DescriptorBindingInfo& PipelineBindingSet::GetBinding(
    std::string_view name,
    const DescriptorBindingKind expectedKind) const
{
    return GetLayout().GetBinding(std::string(name), expectedKind);
}

UINT PipelineBindingSet::GetRootParameterIndex(
    std::string_view name,
    const DescriptorBindingKind expectedKind) const
{
    return GetBinding(name, expectedKind).RootParameterIndex;
}

UINT PipelineBindingSet::GetDescriptorCount(
    std::string_view name,
    const DescriptorBindingKind expectedKind) const
{
    return GetBinding(name, expectedKind).DescriptorCount;
}

void PipelineBindingSet::ValidateArrayIndex(
    std::string_view name,
    const DescriptorBindingKind expectedKind,
    const UINT arrayIndex) const
{
    const DescriptorBindingInfo& binding = GetBinding(name, expectedKind);
    if (!DescriptorLayout::IsArrayIndexInBounds(binding.DescriptorCount, arrayIndex))
    {
        throw std::exception("Pipeline binding array index is out of bounds.");
    }
}

const PipelineLayout& PipelineBindingSet::GetLayout() const
{
    Assert(m_Layout != nullptr, "PipelineBindingSet has no layout.");
    return *m_Layout;
}

//Modify End
