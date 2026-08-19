#include <Framework/Rendering/Pipeline/DescriptorLayout.h>

//Modify Begin:2026-07-30 by Hui

#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>
#include <Framework/Core/FrameworkDeviceContext.h>

#include <algorithm>
#include <stdexcept>

std::string DescriptorLayout::GetBaseResourceName(const std::string& name)
{
    const size_t arraySuffix = name.find("[0]");
    if (arraySuffix == std::string::npos)
    {
        return name;
    }

    return name.substr(0, arraySuffix);
}

bool DescriptorLayout::IsArrayIndexInBounds(const UINT bindCount, const UINT arrayIndex)
{
    return bindCount == 0u || bindCount == UnboundedBindCount || arrayIndex < bindCount;
}

UINT DescriptorLayout::NormalizeDescriptorCount(const UINT descriptorCount, const UINT maxDescriptorCount)
{
    return descriptorCount == 0u || descriptorCount == UnboundedBindCount ? maxDescriptorCount : descriptorCount;
}

D3D12_SHADER_RESOURCE_VIEW_DESC DescriptorLayout::CreateNullShaderResourceViewDesc(const ShaderUtils::ShaderResourceViewMetadata& srv)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    switch (srv.Dimension)
    {
    case D3D_SRV_DIMENSION_BUFFER:
        desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        if (srv.InputType == D3D_SIT_STRUCTURED)
        {
            desc.Format = DXGI_FORMAT_UNKNOWN;
            desc.Buffer.NumElements = 1;
            desc.Buffer.StructureByteStride = sizeof(uint32_t);
            desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        }
        else if (srv.InputType == D3D_SIT_BYTEADDRESS)
        {
            desc.Format = DXGI_FORMAT_R32_TYPELESS;
            desc.Buffer.NumElements = 1;
            desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
        }
        else
        {
            desc.Format = DXGI_FORMAT_R32_UINT;
            desc.Buffer.NumElements = 1;
            desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        }
        break;
    case D3D_SRV_DIMENSION_TEXTURECUBE:
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.TextureCube.MipLevels = 1;
        desc.TextureCube.MostDetailedMip = 0;
        break;
    case D3D_SRV_DIMENSION_TEXTURE3D:
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.Texture3D.MipLevels = 1;
        desc.Texture3D.MostDetailedMip = 0;
        break;
    case D3D_SRV_DIMENSION_TEXTURE2D:
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.Texture2D.MipLevels = 1;
        desc.Texture2D.MostDetailedMip = 0;
        break;
    default:
        throw std::invalid_argument("Cannot create a null SRV for the reflected resource dimension.");
    }

    return desc;
}

D3D12_UNORDERED_ACCESS_VIEW_DESC DescriptorLayout::CreateNullUnorderedAccessViewDesc(const ShaderUtils::UnorderedAccessViewMetadata& uav)
{
    D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};

    const bool isRawBuffer = uav.InputType == D3D_SIT_UAV_RWBYTEADDRESS;
    const bool isStructuredBuffer =
        uav.InputType == D3D_SIT_UAV_RWSTRUCTURED ||
        uav.InputType == D3D_SIT_UAV_APPEND_STRUCTURED ||
        uav.InputType == D3D_SIT_UAV_CONSUME_STRUCTURED ||
        uav.InputType == D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER;

    switch (uav.Dimension)
    {
    case D3D_SRV_DIMENSION_BUFFER:
        desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        desc.Format = isRawBuffer ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_UNKNOWN;
        desc.Buffer.NumElements = 1;
        desc.Buffer.StructureByteStride = isStructuredBuffer ? sizeof(uint32_t) : 0;
        desc.Buffer.Flags = isRawBuffer ? D3D12_BUFFER_UAV_FLAG_RAW : D3D12_BUFFER_UAV_FLAG_NONE;
        break;
    case D3D_SRV_DIMENSION_TEXTURE3D:
        desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.Texture3D.WSize = 1;
        break;
    case D3D_SRV_DIMENSION_TEXTURE2D:
        desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        break;
    default:
        throw std::invalid_argument("Cannot create a null UAV for the reflected resource dimension.");
    }

    return desc;
}

void DescriptorLayout::AddBinding(const std::string& name, DescriptorBindingInfo binding)
{
    m_Bindings[name] = binding;
    m_Bindings[GetBaseResourceName(name)] = binding;
}

const DescriptorBindingInfo& DescriptorLayout::GetBinding(const std::string& name, const DescriptorBindingKind expectedKind) const
{
    const auto findResult = m_Bindings.find(name);
    if (findResult == m_Bindings.end())
    {
        throw std::runtime_error("Shader variable not found: " + name);
    }

    if (findResult->second.Kind != expectedKind)
    {
        throw std::runtime_error("Shader variable binding type does not match the setter: " + name);
    }

    return findResult->second;
}

void DescriptorLayout::AddDefaultShaderResourceViewTable(
    const UINT rootParameterIndex,
    const UINT descriptorCount,
    const ShaderUtils::ShaderResourceViewMetadata& srv)
{
    const D3D12_SHADER_RESOURCE_VIEW_DESC nullDesc = CreateNullShaderResourceViewDesc(srv);
    AddDefaultShaderResourceViewTable(rootParameterIndex, descriptorCount, nullDesc);
}

void DescriptorLayout::AddDefaultShaderResourceViewTable(
    const UINT rootParameterIndex,
    const UINT descriptorCount,
    const D3D12_SHADER_RESOURCE_VIEW_DESC& srvDesc)
{
    Assert(m_DeviceContext != nullptr, "Descriptor layout requires a framework device context.");
    const auto& device = m_DeviceContext->GetDevice();
    DescriptorAllocation descriptors = m_DeviceContext->AllocateDescriptors(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        descriptorCount);
    for (UINT i = 0; i < descriptorCount; ++i)
    {
        device->CreateShaderResourceView(nullptr, &srvDesc, descriptors.GetDescriptorHandle(i));
    }

    DefaultDescriptorTable table;
    table.RootParameterIndex = rootParameterIndex;
    table.DescriptorCount = descriptorCount;
    table.Descriptors = std::move(descriptors);
    m_DefaultDescriptorTables.push_back(std::move(table));
}

void DescriptorLayout::AddDefaultUnorderedAccessViewTable(
    const UINT rootParameterIndex,
    const UINT descriptorCount,
    const ShaderUtils::UnorderedAccessViewMetadata& uav)
{
    const D3D12_UNORDERED_ACCESS_VIEW_DESC nullDesc = CreateNullUnorderedAccessViewDesc(uav);
    AddDefaultUnorderedAccessViewTable(rootParameterIndex, descriptorCount, nullDesc);
}

void DescriptorLayout::AddDefaultUnorderedAccessViewTable(
    const UINT rootParameterIndex,
    const UINT descriptorCount,
    const D3D12_UNORDERED_ACCESS_VIEW_DESC& uavDesc)
{
    Assert(m_DeviceContext != nullptr, "Descriptor layout requires a framework device context.");
    const auto& device = m_DeviceContext->GetDevice();
    DescriptorAllocation descriptors = m_DeviceContext->AllocateDescriptors(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        descriptorCount);
    for (UINT i = 0; i < descriptorCount; ++i)
    {
        device->CreateUnorderedAccessView(nullptr, nullptr, &uavDesc, descriptors.GetDescriptorHandle(i));
    }

    DefaultDescriptorTable table;
    table.RootParameterIndex = rootParameterIndex;
    table.DescriptorCount = descriptorCount;
    table.Descriptors = std::move(descriptors);
    m_DefaultDescriptorTables.push_back(std::move(table));
}

void DescriptorLayout::StageDefaultDescriptorTables(CommandList& commandList) const
{
    for (const DefaultDescriptorTable& table : m_DefaultDescriptorTables)
    {
        commandList.StageDynamicDescriptors(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
            table.RootParameterIndex,
            0u,
            table.DescriptorCount,
            table.Descriptors.GetDescriptorHandle());
    }
}

const DescriptorAllocation* DescriptorLayout::FindDefaultDescriptorTable(const UINT rootParameterIndex) const
{
    const auto findResult = std::find_if(
        m_DefaultDescriptorTables.begin(),
        m_DefaultDescriptorTables.end(),
        [rootParameterIndex](const DefaultDescriptorTable& table)
        {
            return table.RootParameterIndex == rootParameterIndex;
        });

    return findResult != m_DefaultDescriptorTables.end() ? &findResult->Descriptors : nullptr;
}

//Modify End
