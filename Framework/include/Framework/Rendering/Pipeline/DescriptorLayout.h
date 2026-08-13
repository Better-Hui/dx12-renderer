#pragma once

//Modify Begin:2026-07-24 by BestHui

#include <DX12Library/DescriptorAllocation.h>
#include <DX12Library/ShaderUtils.h>

#include <d3d12.h>

#include <map>
#include <string>
#include <vector>

class CommandList;
//Modify Begin:2026-07-30 by BestHui
class FrameworkDeviceContext;
//Modify End

enum class DescriptorBindingKind
{
    ConstantBuffer,
    ShaderResourceView,
    UnorderedAccessView,
    AccelerationStructure
};

struct DescriptorBindingInfo
{
    DescriptorBindingKind Kind = DescriptorBindingKind::ShaderResourceView;
    UINT RootParameterIndex = 0;
    UINT DescriptorCount = 1;
};

class DescriptorLayout
{
public:
//Modify Begin:2026-07-30 by BestHui
    DescriptorLayout() = default;
    explicit DescriptorLayout(FrameworkDeviceContext& deviceContext)
        : m_DeviceContext(&deviceContext)
    {
    }
//Modify End
    using BindingMap = std::map<std::string, DescriptorBindingInfo>;

    static constexpr UINT UnboundedBindCount = 0xffffffffu;

    static std::string GetBaseResourceName(const std::string& name);
    static bool IsArrayIndexInBounds(UINT bindCount, UINT arrayIndex);
    static UINT NormalizeDescriptorCount(UINT descriptorCount, UINT maxDescriptorCount);
    static D3D12_SHADER_RESOURCE_VIEW_DESC CreateNullShaderResourceViewDesc(const ShaderUtils::ShaderResourceViewMetadata& srv);
    static D3D12_UNORDERED_ACCESS_VIEW_DESC CreateNullUnorderedAccessViewDesc(const ShaderUtils::UnorderedAccessViewMetadata& uav);

    void AddBinding(const std::string& name, DescriptorBindingInfo binding);
    const DescriptorBindingInfo& GetBinding(const std::string& name, DescriptorBindingKind expectedKind) const;
    const BindingMap& GetBindings() const { return m_Bindings; }

    void AddDefaultShaderResourceViewTable(UINT rootParameterIndex, UINT descriptorCount, const ShaderUtils::ShaderResourceViewMetadata& srv);
    void AddDefaultShaderResourceViewTable(UINT rootParameterIndex, UINT descriptorCount, const D3D12_SHADER_RESOURCE_VIEW_DESC& srvDesc);
    void AddDefaultUnorderedAccessViewTable(UINT rootParameterIndex, UINT descriptorCount, const ShaderUtils::UnorderedAccessViewMetadata& uav);
    void AddDefaultUnorderedAccessViewTable(UINT rootParameterIndex, UINT descriptorCount, const D3D12_UNORDERED_ACCESS_VIEW_DESC& uavDesc);
    void StageDefaultDescriptorTables(CommandList& commandList) const;
//Modify Begin:2026-07-27 by BestHui
    const DescriptorAllocation* FindDefaultDescriptorTable(UINT rootParameterIndex) const;
//Modify End

private:
    struct DefaultDescriptorTable
    {
        UINT RootParameterIndex = 0;
        UINT DescriptorCount = 0;
        DescriptorAllocation Descriptors;
    };

    BindingMap m_Bindings;
    std::vector<DefaultDescriptorTable> m_DefaultDescriptorTables;
//Modify Begin:2026-07-30 by BestHui
    FrameworkDeviceContext* m_DeviceContext = nullptr;
//Modify End
};

//Modify End
