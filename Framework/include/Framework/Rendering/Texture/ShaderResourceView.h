#pragma once

#include <d3d12.h>
#include <DX12Library/Resource.h>

#include <memory>
#include <stdexcept>

//Modify Begin:2026-08-06 by BestHui
enum class EnvironmentTextureProjection : uint32_t
{
    Cubemap = 0u,
    Equirectangular = 1u,
    CubemapHorizontalStrip = 2u,
};
//Modify End

struct ShaderResourceView
{
//Modify Begin:2026-07-30 by BestHui
    static EnvironmentTextureProjection GetEnvironmentTextureProjection(const Resource& resource)
    {
        const D3D12_RESOURCE_DESC resourceDesc = resource.GetD3D12ResourceDesc();
        if (resourceDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
        {
            throw std::invalid_argument("Environment texture must be a Texture2D resource.");
        }

        if (resourceDesc.DepthOrArraySize >= 6u && resourceDesc.DepthOrArraySize % 6u == 0u)
        {
            return EnvironmentTextureProjection::Cubemap;
        }

        if (resourceDesc.DepthOrArraySize != 1u)
        {
            throw std::invalid_argument("Environment texture array must contain one image or complete cubemap faces.");
        }

        const uint64_t width = resourceDesc.Width;
        const uint64_t height = resourceDesc.Height;
        if (width == height * 2u)
        {
            return EnvironmentTextureProjection::Equirectangular;
        }
        if (width == height * 6u)
        {
            return EnvironmentTextureProjection::CubemapHorizontalStrip;
        }

        throw std::invalid_argument("Unsupported 2D environment texture layout. Expected a 2:1 equirectangular image or a 6:1 cubemap strip.");
    }

    static bool IsEquirectangularEnvironment(const Resource& resource)
    {
        return GetEnvironmentTextureProjection(resource) == EnvironmentTextureProjection::Equirectangular;
    }

    static ShaderResourceView EnvironmentTexture(const std::shared_ptr<Resource>& resource)
    {
        if (resource == nullptr)
        {
            throw std::invalid_argument("Environment texture must not be null.");
        }
        if (GetEnvironmentTextureProjection(*resource) == EnvironmentTextureProjection::Cubemap)
        {
            return TextureCube(resource);
        }
        return ShaderResourceView(resource);
    }
//Modify End

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
        desc.TextureCube.MipLevels = resource.GetD3D12ResourceDesc().MipLevels;
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
