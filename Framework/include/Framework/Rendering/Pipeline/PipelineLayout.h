#pragma once

//Modify Begin:2026-08-12 by Hui

#include <Framework/Rendering/Pipeline/DescriptorLayout.h>
#include <Framework/Rendering/Pipeline/ShaderReflection.h>
#include <DX12Library/RootSignature.h>

#include <d3d12.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class FrameworkDeviceContext;

enum class PipelineDescriptorBindingMode
{
    RootDescriptor,
    DescriptorTable
};

enum class PipelineShaderStageFlags : uint32_t
{
    None = 0,
    Vertex = 1 << 0,
    Pixel = 1 << 1,
    Compute = 1 << 2,
    RayTracing = 1 << 3,
    Mesh = 1 << 4,
    AllGraphics = (1 << 0) | (1 << 1) | (1 << 4),
    All = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4)
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

struct PipelineDescriptorRangeDesc
{
    std::string Name;
    DescriptorBindingKind Kind = DescriptorBindingKind::ShaderResourceView;
    UINT ShaderRegister = 0;
    UINT RegisterSpace = 0;
    UINT DescriptorCount = 1;
    UINT RootParameterIndex = 0;
    PipelineDescriptorBindingMode BindingMode = PipelineDescriptorBindingMode::DescriptorTable;
    PipelineShaderStageFlags ShaderStages = PipelineShaderStageFlags::All;
    PipelineDescriptorRangeFlags Flags = PipelineDescriptorRangeFlags::None;
};

struct PipelineDescriptorSetDesc
{
    UINT RegisterSpace = 0;
    std::vector<PipelineDescriptorRangeDesc> Ranges;
    PipelineDescriptorSetFlags Flags = PipelineDescriptorSetFlags::None;
};

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

struct PipelineStaticSamplerContract
{
    std::string Name;
    UINT ShaderRegister = 0;
    UINT RegisterSpace = 0;
    D3D12_STATIC_SAMPLER_DESC Desc = {};
};

namespace PipelineStaticSamplers
{
    PipelineStaticSamplerContract PointWrap(UINT shaderRegister, UINT registerSpace = 0);
    PipelineStaticSamplerContract LinearWrap(UINT shaderRegister, UINT registerSpace = 0);
    PipelineStaticSamplerContract PointClamp(UINT shaderRegister, UINT registerSpace = 0);
    PipelineStaticSamplerContract LinearClamp(UINT shaderRegister, UINT registerSpace = 0);
    PipelineStaticSamplerContract ShadowCompareClamp(UINT shaderRegister, UINT registerSpace = 0);

    void AddCommonRootSignatureContracts(std::vector<PipelineStaticSamplerContract>& contracts);
}

struct PipelineLayoutBindingOverride
{
    std::string Name;
    UINT DescriptorCount = 1;
};

struct PipelineLayoutReflectionOptions
{
    std::vector<PipelineLayoutBindingOverride> BindingOverrides;
    std::vector<PipelineStaticSamplerContract> StaticSamplerContracts;
    std::vector<std::string> RootConstantBufferNames;
    UINT MaxDescriptorCount = 1024;
    PipelineShaderStageFlags ShaderStages = PipelineShaderStageFlags::All;
};

struct PipelineLayoutDesc
{
    UINT RootRegisterSpace = 0;
    std::vector<PipelineRootConstantDesc> RootConstants;
    std::vector<PipelineRootDescriptorDesc> RootDescriptors;
    std::vector<PipelineRootSamplerDesc> RootSamplers;
    std::vector<PipelineDescriptorSetDesc> DescriptorSets;
    std::vector<PipelineDescriptorRangeDesc> DescriptorRanges;
    PipelineShaderStageFlags ShaderStages = PipelineShaderStageFlags::All;
};

struct PipelineRootSignatureBuildDesc
{
    D3D12_ROOT_SIGNATURE_FLAGS Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    std::vector<D3D12_STATIC_SAMPLER_DESC> StaticSamplers;
};

class PipelineLayout
{
public:
    explicit PipelineLayout(FrameworkDeviceContext& deviceContext);
    PipelineLayout(FrameworkDeviceContext& deviceContext, PipelineLayoutDesc desc);

    static PipelineLayoutDesc CreateDescFromReflection(
        const ShaderReflectionMetadata& reflection,
        const PipelineLayoutReflectionOptions& options);

    void Reset(PipelineLayoutDesc desc);

    const PipelineLayoutDesc& GetDesc() const { return m_Desc; }
    FrameworkDeviceContext& GetDeviceContext() const { return *m_DeviceContext; }
    const std::vector<PipelineDescriptorSetDesc>& GetDescriptorSets() const { return m_Desc.DescriptorSets; }
    void SetRootSignature(std::shared_ptr<RootSignature> rootSignature);
    const RootSignature* GetRootSignature() const { return m_RootSignature.get(); }
    std::shared_ptr<RootSignature> CreateRootSignature(const PipelineRootSignatureBuildDesc& buildDesc) const;

    bool HasBinding(const std::string& name, DescriptorBindingKind expectedKind) const;
    const PipelineRootConstantDesc* FindRootConstant(const std::string& name) const;
    const PipelineDescriptorRangeDesc* FindRange(const std::string& name, DescriptorBindingKind expectedKind) const;
    const PipelineDescriptorRangeDesc* FindRangeByRootParameterIndex(UINT rootParameterIndex) const;
    const DescriptorBindingInfo& GetBinding(const std::string& name, DescriptorBindingKind expectedKind) const;

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
    const DescriptorAllocation* FindDefaultDescriptorTable(UINT rootParameterIndex) const;

private:
    void RebuildDescriptorLayout();

    PipelineLayoutDesc m_Desc;
    DescriptorLayout m_DescriptorLayout;
    FrameworkDeviceContext* m_DeviceContext = nullptr;
    std::shared_ptr<RootSignature> m_RootSignature;
};

//Modify End
