#include "PipelineLayout.h"

//Modify Begin:2026-07-24 by BestHui

#include <algorithm>
#include <utility>

#if defined(min)
#undef min
#endif

#if defined(max)
#undef max
#endif

namespace
{
    UINT GetDescriptorCountOverride(
        const PipelineLayoutReflectionOptions& options,
        const std::string& name,
        const UINT reflectedCount)
    {
        for (const PipelineLayoutBindingOverride& bindingOverride : options.BindingOverrides)
        {
            if (bindingOverride.Name == name)
            {
                return bindingOverride.DescriptorCount;
            }
        }

        return DescriptorLayout::NormalizeDescriptorCount(reflectedCount, options.MaxDescriptorCount);
    }

    bool IsAccelerationStructureSrv(
        const ShaderUtils::ShaderResourceViewMetadata& srv,
        const PipelineLayoutReflectionOptions& options)
    {
        const char* fallbackName = options.AccelerationStructureFallbackName.empty() ?
            nullptr :
            options.AccelerationStructureFallbackName.c_str();
        return DescriptorLayout::IsRayTracingAccelerationStructureSrv(srv, fallbackName);
    }

    void BuildDescriptorSetsFromRanges(PipelineLayoutDesc& desc)
    {
        desc.DescriptorSets.clear();
        for (const PipelineDescriptorRangeDesc& range : desc.DescriptorRanges)
        {
            auto setFindResult = std::find_if(
                desc.DescriptorSets.begin(),
                desc.DescriptorSets.end(),
                [&range](const PipelineDescriptorSetDesc& setDesc)
                {
                    return setDesc.RegisterSpace == range.RegisterSpace;
                });

            if (setFindResult == desc.DescriptorSets.end())
            {
                PipelineDescriptorSetDesc setDesc;
                setDesc.RegisterSpace = range.RegisterSpace;
                setDesc.Ranges.push_back(range);
                desc.DescriptorSets.push_back(std::move(setDesc));
            }
            else
            {
                setFindResult->Ranges.push_back(range);
            }
        }
    }

    void FlattenDescriptorSets(PipelineLayoutDesc& desc)
    {
        desc.DescriptorRanges.clear();
        for (PipelineDescriptorSetDesc& setDesc : desc.DescriptorSets)
        {
            for (PipelineDescriptorRangeDesc& range : setDesc.Ranges)
            {
                range.RegisterSpace = setDesc.RegisterSpace;
                range.RootParameterIndex = static_cast<UINT>(desc.DescriptorRanges.size());
                desc.DescriptorRanges.push_back(range);
            }
        }
    }
}

PipelineLayout::PipelineLayout(PipelineLayoutDesc desc)
{
    Reset(std::move(desc));
}

PipelineLayoutDesc PipelineLayout::CreateDescFromReflection(
    const ShaderReflectionMetadata& reflection,
    const PipelineLayoutReflectionOptions& options)
{
    PipelineLayoutDesc desc;
    desc.DescriptorRanges.reserve(
        reflection.m_ConstantBuffers.size() +
        reflection.m_ShaderResourceViews.size() +
        reflection.m_UnorderedAccessViews.size());

    const auto appendRange = [&desc](
        std::string name,
        const DescriptorBindingKind kind,
        const UINT shaderRegister,
        const UINT registerSpace,
        const UINT descriptorCount,
        const PipelineDescriptorBindingMode bindingMode)
    {
        PipelineDescriptorRangeDesc range;
        range.Name = std::move(name);
        range.Kind = kind;
        range.ShaderRegister = shaderRegister;
        range.RegisterSpace = registerSpace;
        range.DescriptorCount = std::max(1u, descriptorCount);
        range.RootParameterIndex = static_cast<UINT>(desc.DescriptorRanges.size());
        range.BindingMode = bindingMode;
        desc.DescriptorRanges.push_back(std::move(range));
    };

    for (const auto& cbuffer : reflection.m_ConstantBuffers)
    {
        appendRange(
            cbuffer.Name,
            DescriptorBindingKind::ConstantBuffer,
            cbuffer.RegisterIndex,
            cbuffer.Space,
            1u,
            PipelineDescriptorBindingMode::RootDescriptor);
    }

    for (const auto& srv : reflection.m_ShaderResourceViews)
    {
        if (IsAccelerationStructureSrv(srv, options))
        {
            appendRange(
                srv.Name,
                DescriptorBindingKind::AccelerationStructure,
                srv.RegisterIndex,
                srv.Space,
                1u,
                PipelineDescriptorBindingMode::RootDescriptor);
            continue;
        }

        appendRange(
            srv.Name,
            DescriptorBindingKind::ShaderResourceView,
            srv.RegisterIndex,
            srv.Space,
            GetDescriptorCountOverride(options, srv.Name, srv.BindCount),
            PipelineDescriptorBindingMode::DescriptorTable);
    }

    for (const auto& uav : reflection.m_UnorderedAccessViews)
    {
        appendRange(
            uav.Name,
            DescriptorBindingKind::UnorderedAccessView,
            uav.RegisterIndex,
            uav.Space,
            GetDescriptorCountOverride(options, uav.Name, uav.BindCount),
            PipelineDescriptorBindingMode::DescriptorTable);
    }

    return desc;
}

void PipelineLayout::Reset(PipelineLayoutDesc desc)
{
    if (desc.DescriptorRanges.empty() && !desc.DescriptorSets.empty())
    {
        FlattenDescriptorSets(desc);
    }
    else if (desc.DescriptorSets.empty())
    {
        BuildDescriptorSetsFromRanges(desc);
    }

    m_Desc = std::move(desc);
    RebuildDescriptorLayout();
}

const PipelineDescriptorRangeDesc* PipelineLayout::FindRange(
    const std::string& name,
    const DescriptorBindingKind expectedKind) const
{
    const std::string baseName = DescriptorLayout::GetBaseResourceName(name);
    const auto findResult = std::find_if(
        m_Desc.DescriptorRanges.begin(),
        m_Desc.DescriptorRanges.end(),
        [&name, &baseName, expectedKind](const PipelineDescriptorRangeDesc& range)
        {
            return range.Kind == expectedKind &&
                (range.Name == name || DescriptorLayout::GetBaseResourceName(range.Name) == baseName);
        });

    return findResult != m_Desc.DescriptorRanges.end() ? &*findResult : nullptr;
}

const PipelineDescriptorRangeDesc* PipelineLayout::FindRangeByRootParameterIndex(const UINT rootParameterIndex) const
{
    const auto findResult = std::find_if(
        m_Desc.DescriptorRanges.begin(),
        m_Desc.DescriptorRanges.end(),
        [rootParameterIndex](const PipelineDescriptorRangeDesc& range)
        {
            return range.RootParameterIndex == rootParameterIndex;
        });

    return findResult != m_Desc.DescriptorRanges.end() ? &*findResult : nullptr;
}

bool PipelineLayout::HasBinding(const std::string& name, const DescriptorBindingKind expectedKind) const
{
    return FindRange(name, expectedKind) != nullptr;
}

const DescriptorBindingInfo& PipelineLayout::GetBinding(
    const std::string& name,
    const DescriptorBindingKind expectedKind) const
{
    return m_DescriptorLayout.GetBinding(name, expectedKind);
}

const DescriptorBindingInfo& PipelineLayout::GetFirstBinding(const DescriptorBindingKind expectedKind) const
{
    return m_DescriptorLayout.GetFirstBinding(expectedKind);
}

void PipelineLayout::AddDefaultShaderResourceViewTable(
    const UINT rootParameterIndex,
    const UINT descriptorCount,
    const ShaderUtils::ShaderResourceViewMetadata& srv)
{
    m_DescriptorLayout.AddDefaultShaderResourceViewTable(rootParameterIndex, descriptorCount, srv);
}

void PipelineLayout::AddDefaultUnorderedAccessViewTable(
    const UINT rootParameterIndex,
    const UINT descriptorCount,
    const ShaderUtils::UnorderedAccessViewMetadata& uav)
{
    m_DescriptorLayout.AddDefaultUnorderedAccessViewTable(rootParameterIndex, descriptorCount, uav);
}

void PipelineLayout::StageDefaultDescriptorTables(CommandList& commandList) const
{
    m_DescriptorLayout.StageDefaultDescriptorTables(commandList);
}

void PipelineLayout::RebuildDescriptorLayout()
{
    m_DescriptorLayout = DescriptorLayout();
    for (const PipelineDescriptorRangeDesc& range : m_Desc.DescriptorRanges)
    {
        DescriptorBindingInfo binding;
        binding.Kind = range.Kind;
        binding.RootParameterIndex = range.RootParameterIndex;
        binding.DescriptorCount = range.DescriptorCount;
        m_DescriptorLayout.AddBinding(range.Name, binding);
    }
}

//Modify End
