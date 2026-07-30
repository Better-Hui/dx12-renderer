#pragma once

#include <d3d12.h>
#include <DX12Library/Resource.h>

#include <memory>

struct ShaderResourceView
{
    //Modify Begin:2026-07-23 by BestHui
    static ShaderResourceView TextureCube(const std::shared_ptr<Resource>& resource)
    {
        return ShaderResourceView(resource, CreateTextureCubeDesc(*resource));
    }

    static ShaderResourceView DepthAsFloat(const std::shared_ptr<Resource>& resource)
    {
        return ShaderResourceView(resource, 0, 1, CreateDepthAsFloatDesc());
    }

    static D3D12_SHADER_RESOURCE_VIEW_DESC CreateTextureCubeDesc(const Resource& resource)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
        desc.Format = resource.GetD3D12ResourceDesc().Format;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        desc.TextureCube.MipLevels = 1;
        desc.TextureCube.MostDetailedMip = 0;
        desc.TextureCube.ResourceMinLODClamp = 0.0f;
        return desc;
    }

    static D3D12_SHADER_RESOURCE_VIEW_DESC CreateDepthAsFloatDesc()
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
        desc.Format = DXGI_FORMAT_R32_FLOAT;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MipLevels = 1;
        desc.Texture2D.MostDetailedMip = 0;
        desc.Texture2D.PlaneSlice = 0;
        desc.Texture2D.ResourceMinLODClamp = 0.0f;
        return desc;
    }
    //Modify End

    explicit ShaderResourceView(const std::shared_ptr<Resource>& resource,
        UINT firstSubresource = 0, UINT numSubresources = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES
    )
        : m_Resource(resource)
        , m_FirstSubresource(firstSubresource)
        , m_NumSubresources(numSubresources)
        , m_IsDescValid(false)
        , m_Desc{}
    {

    }

    explicit ShaderResourceView(const std::shared_ptr<Resource>& resource,
        D3D12_SHADER_RESOURCE_VIEW_DESC desc
    )
        : m_Resource(resource)
        , m_FirstSubresource(0)
        , m_NumSubresources(D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
        , m_IsDescValid(true)
        , m_Desc(desc)
    {

    }

    explicit ShaderResourceView(const std::shared_ptr<Resource>& resource,
        UINT firstSubresource, UINT numSubresources,
        D3D12_SHADER_RESOURCE_VIEW_DESC desc
    )
        : m_Resource(resource)
        , m_FirstSubresource(firstSubresource)
        , m_NumSubresources(numSubresources)
        , m_IsDescValid(true)
        , m_Desc(desc)
    {

    }

    ShaderResourceView(const ShaderResourceView& other) = default;
    ShaderResourceView& operator=(const ShaderResourceView& other) = default;

    const D3D12_SHADER_RESOURCE_VIEW_DESC* GetDescOrNullptr() const
    {
        if (m_IsDescValid)
            return &m_Desc;
        return nullptr;
    }

    std::shared_ptr<Resource> m_Resource = nullptr;
    UINT m_FirstSubresource;
    UINT m_NumSubresources;
    bool m_IsDescValid;
    D3D12_SHADER_RESOURCE_VIEW_DESC m_Desc;
};
