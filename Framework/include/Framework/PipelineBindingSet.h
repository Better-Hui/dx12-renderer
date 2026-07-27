#pragma once

//Modify Begin:2026-07-24 by BestHui

#include "PipelineLayout.h"

#include <d3d12.h>

#include <string>
#include <string_view>

class PipelineBindingSet
{
public:
    PipelineBindingSet() = default;
    explicit PipelineBindingSet(const PipelineLayout& layout);

    void Reset(const PipelineLayout& layout);

    bool HasBinding(std::string_view name) const;
    bool HasBinding(std::string_view name, DescriptorBindingKind expectedKind) const;
    const PipelineDescriptorRangeDesc* FindRange(std::string_view name, DescriptorBindingKind expectedKind) const;
    const PipelineDescriptorRangeDesc& GetRange(std::string_view name, DescriptorBindingKind expectedKind) const;
    const PipelineDescriptorRangeDesc& GetFirstRange(DescriptorBindingKind expectedKind) const;
    const DescriptorBindingInfo& GetBinding(std::string_view name, DescriptorBindingKind expectedKind) const;
    const DescriptorBindingInfo& GetFirstBinding(DescriptorBindingKind expectedKind) const;
    UINT GetRootParameterIndex(std::string_view name, DescriptorBindingKind expectedKind) const;
    UINT GetDescriptorCount(std::string_view name, DescriptorBindingKind expectedKind) const;
    void ValidateArrayIndex(std::string_view name, DescriptorBindingKind expectedKind, UINT arrayIndex) const;

private:
    const PipelineLayout& GetLayout() const;

    const PipelineLayout* m_Layout = nullptr;
};

//Modify End
