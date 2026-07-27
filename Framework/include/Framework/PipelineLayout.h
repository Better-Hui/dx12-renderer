#pragma once

//Modify Begin:2026-07-24 by BestHui

#include "DescriptorLayout.h"
#include "ShaderReflection.h"

#include <d3d12.h>

#include <string>
#include <vector>

enum class PipelineDescriptorBindingMode
{
    RootDescriptor,
    DescriptorTable
};

struct PipelineDescriptorRangeDesc
{
    std::string Name;
    DescriptorBindingKind Kind = DescriptorBindingKind::ShaderResourceView;
    UINT ShaderRegister = 0;
    UINT RegisterSpace = 0;
    UINT DescriptorCount = 1;
    UINT RootParameterIndex = 0;
    PipelineDescriptorBindingMode BindingMode = PipelineDescriptorBindingMode::DescriptorTable;
};

struct PipelineDescriptorSetDesc
{
    UINT RegisterSpace = 0;
    std::vector<PipelineDescriptorRangeDesc> Ranges;
};

struct PipelineLayoutBindingOverride
{
    std::string Name;
    UINT DescriptorCount = 1;
};

struct PipelineLayoutReflectionOptions
{
    std::vector<PipelineLayoutBindingOverride> BindingOverrides;
    UINT MaxDescriptorCount = 1024;
    std::string AccelerationStructureFallbackName;
};

struct PipelineLayoutDesc
{
    std::vector<PipelineDescriptorSetDesc> DescriptorSets;
    std::vector<PipelineDescriptorRangeDesc> DescriptorRanges;
};

class PipelineLayout
{
public:
    PipelineLayout() = default;
    explicit PipelineLayout(PipelineLayoutDesc desc);

    static PipelineLayoutDesc CreateDescFromReflection(
        const ShaderReflectionMetadata& reflection,
        const PipelineLayoutReflectionOptions& options);

    void Reset(PipelineLayoutDesc desc);

    const PipelineLayoutDesc& GetDesc() const { return m_Desc; }
    const std::vector<PipelineDescriptorSetDesc>& GetDescriptorSets() const { return m_Desc.DescriptorSets; }

    bool HasBinding(const std::string& name, DescriptorBindingKind expectedKind) const;
    const PipelineDescriptorRangeDesc* FindRange(const std::string& name, DescriptorBindingKind expectedKind) const;
    const PipelineDescriptorRangeDesc* FindRangeByRootParameterIndex(UINT rootParameterIndex) const;
    const DescriptorBindingInfo& GetBinding(const std::string& name, DescriptorBindingKind expectedKind) const;
    const DescriptorBindingInfo& GetFirstBinding(DescriptorBindingKind expectedKind) const;

    void AddDefaultShaderResourceViewTable(
        UINT rootParameterIndex,
        UINT descriptorCount,
        const ShaderUtils::ShaderResourceViewMetadata& srv);
    void AddDefaultUnorderedAccessViewTable(
        UINT rootParameterIndex,
        UINT descriptorCount,
        const ShaderUtils::UnorderedAccessViewMetadata& uav);
    void StageDefaultDescriptorTables(CommandList& commandList) const;

private:
    void RebuildDescriptorLayout();

    PipelineLayoutDesc m_Desc;
    DescriptorLayout m_DescriptorLayout;
};

//Modify End
