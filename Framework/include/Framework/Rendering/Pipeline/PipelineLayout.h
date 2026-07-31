#pragma once

//Modify Begin:2026-07-24 by BestHui

#include <Framework/Rendering/Pipeline/DescriptorLayout.h>
#include <Framework/Rendering/Pipeline/ShaderReflection.h>
//Modify Begin:2026-07-27 by BestHui
#include <DX12Library/RootSignature.h>
//Modify End

#include <d3d12.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

enum class PipelineDescriptorBindingMode
{
    RootDescriptor,
    DescriptorTable
};

//Modify Begin:2026-07-27 by BestHui
enum class PipelineShaderStageFlags : uint32_t
{
    None = 0,
    Vertex = 1 << 0,
    Pixel = 1 << 1,
    Compute = 1 << 2,
    RayTracing = 1 << 3,
//Modify Begin:2026-07-30 by BestHui
    Mesh = 1 << 4,
    AllGraphics = (1 << 0) | (1 << 1) | (1 << 4),
    All = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4)
//Modify End
};

inline PipelineShaderStageFlags operator|(const PipelineShaderStageFlags lhs, const PipelineShaderStageFlags rhs)
{
    return static_cast<PipelineShaderStageFlags>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

inline bool HasPipelineShaderStage(const PipelineShaderStageFlags stages, const PipelineShaderStageFlags stage)
{
    return (static_cast<uint32_t>(stages) & static_cast<uint32_t>(stage)) != 0;
}

inline D3D12_SHADER_VISIBILITY GetD3D12ShaderVisibility(const PipelineShaderStageFlags stages)
{
    if (stages == PipelineShaderStageFlags::Vertex)
    {
        return D3D12_SHADER_VISIBILITY_VERTEX;
    }
    if (stages == PipelineShaderStageFlags::Pixel)
    {
        return D3D12_SHADER_VISIBILITY_PIXEL;
    }
    return D3D12_SHADER_VISIBILITY_ALL;
}

enum class PipelineDescriptorRangeFlags : uint32_t
{
    None = 0,
    VariableSizedArray = 1 << 0,
    AllowUpdateAfterSet = 1 << 1
};

enum class PipelineDescriptorSetFlags : uint32_t
{
    None = 0,
    AllowUpdateAfterSet = 1 << 0
};
//Modify End

struct PipelineDescriptorRangeDesc
{
    std::string Name;
    DescriptorBindingKind Kind = DescriptorBindingKind::ShaderResourceView;
    UINT ShaderRegister = 0;
    UINT RegisterSpace = 0;
    UINT DescriptorCount = 1;
    UINT RootParameterIndex = 0;
    PipelineDescriptorBindingMode BindingMode = PipelineDescriptorBindingMode::DescriptorTable;
//Modify Begin:2026-07-27 by BestHui
    PipelineShaderStageFlags ShaderStages = PipelineShaderStageFlags::All;
    PipelineDescriptorRangeFlags Flags = PipelineDescriptorRangeFlags::None;
//Modify End
};

struct PipelineDescriptorSetDesc
{
    UINT RegisterSpace = 0;
    std::vector<PipelineDescriptorRangeDesc> Ranges;
//Modify Begin:2026-07-27 by BestHui
    PipelineDescriptorSetFlags Flags = PipelineDescriptorSetFlags::None;
//Modify End
};

//Modify Begin:2026-07-27 by BestHui
struct PipelineRootConstantDesc
{
    std::string Name;
    UINT ShaderRegister = 0;
    UINT RegisterSpace = 0;
    UINT SizeInBytes = 0;
    UINT RootParameterIndex = 0;
    PipelineShaderStageFlags ShaderStages = PipelineShaderStageFlags::All;
};

struct PipelineRootDescriptorDesc
{
    std::string Name;
    DescriptorBindingKind Kind = DescriptorBindingKind::ConstantBuffer;
    UINT ShaderRegister = 0;
    UINT RegisterSpace = 0;
    UINT RootParameterIndex = 0;
    PipelineShaderStageFlags ShaderStages = PipelineShaderStageFlags::All;
};

struct PipelineRootSamplerDesc
{
    std::string Name;
    UINT ShaderRegister = 0;
    UINT RegisterSpace = 0;
    D3D12_STATIC_SAMPLER_DESC Desc = {};
    PipelineShaderStageFlags ShaderStages = PipelineShaderStageFlags::All;
};
//Modify End

struct PipelineLayoutBindingOverride
{
    std::string Name;
    UINT DescriptorCount = 1;
};

struct PipelineLayoutReflectionOptions
{
    std::vector<PipelineLayoutBindingOverride> BindingOverrides;
//Modify Begin:2026-07-31 by BestHui
    std::vector<std::string> RootConstantBufferNames;
//Modify End
    UINT MaxDescriptorCount = 1024;
    std::string AccelerationStructureFallbackName;
//Modify Begin:2026-07-27 by BestHui
    PipelineShaderStageFlags ShaderStages = PipelineShaderStageFlags::All;
//Modify End
};

struct PipelineLayoutDesc
{
//Modify Begin:2026-07-27 by BestHui
    UINT RootRegisterSpace = 0;
    std::vector<PipelineRootConstantDesc> RootConstants;
    std::vector<PipelineRootDescriptorDesc> RootDescriptors;
    std::vector<PipelineRootSamplerDesc> RootSamplers;
//Modify End
    std::vector<PipelineDescriptorSetDesc> DescriptorSets;
    std::vector<PipelineDescriptorRangeDesc> DescriptorRanges;
//Modify Begin:2026-07-27 by BestHui
    PipelineShaderStageFlags ShaderStages = PipelineShaderStageFlags::All;
//Modify End
};

//Modify Begin:2026-07-27 by BestHui
struct PipelineRootSignatureBuildDesc
{
    D3D12_ROOT_SIGNATURE_FLAGS Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    std::vector<D3D12_STATIC_SAMPLER_DESC> StaticSamplers;
};
//Modify End

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
//Modify Begin:2026-07-27 by BestHui
    void SetRootSignature(std::shared_ptr<RootSignature> rootSignature);
    const RootSignature* GetRootSignature() const { return m_RootSignature.get(); }
    std::shared_ptr<RootSignature> CreateRootSignature(const PipelineRootSignatureBuildDesc& buildDesc) const;
//Modify End

    bool HasBinding(const std::string& name, DescriptorBindingKind expectedKind) const;
//Modify Begin:2026-07-31 by BestHui
    const PipelineRootConstantDesc* FindRootConstant(const std::string& name) const;
//Modify End
    const PipelineDescriptorRangeDesc* FindRange(const std::string& name, DescriptorBindingKind expectedKind) const;
    const PipelineDescriptorRangeDesc* FindRangeByRootParameterIndex(UINT rootParameterIndex) const;
    const DescriptorBindingInfo& GetBinding(const std::string& name, DescriptorBindingKind expectedKind) const;
    const DescriptorBindingInfo& GetFirstBinding(DescriptorBindingKind expectedKind) const;

    void AddDefaultShaderResourceViewTable(
        UINT rootParameterIndex,
        UINT descriptorCount,
        const ShaderUtils::ShaderResourceViewMetadata& srv);
    void AddDefaultShaderResourceViewTable(
        UINT rootParameterIndex,
        UINT descriptorCount,
        const D3D12_SHADER_RESOURCE_VIEW_DESC& srvDesc);
    void AddDefaultUnorderedAccessViewTable(
        UINT rootParameterIndex,
        UINT descriptorCount,
        const ShaderUtils::UnorderedAccessViewMetadata& uav);
    void AddDefaultUnorderedAccessViewTable(
        UINT rootParameterIndex,
        UINT descriptorCount,
        const D3D12_UNORDERED_ACCESS_VIEW_DESC& uavDesc);
    void StageDefaultDescriptorTables(CommandList& commandList) const;
//Modify Begin:2026-07-27 by BestHui
    const DescriptorAllocation* FindDefaultDescriptorTable(UINT rootParameterIndex) const;
//Modify End

private:
    void RebuildDescriptorLayout();

    PipelineLayoutDesc m_Desc;
    DescriptorLayout m_DescriptorLayout;
//Modify Begin:2026-07-27 by BestHui
    std::shared_ptr<RootSignature> m_RootSignature;
//Modify End
};

//Modify End
