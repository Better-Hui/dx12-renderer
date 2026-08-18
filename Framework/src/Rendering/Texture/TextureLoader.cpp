#include <Framework/Rendering/Texture/TextureLoader.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/D3D12DeviceContext.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/Texture.h>

#include <DirectXTex.h>
#include <OpenEXR/ImfRgbaFile.h>

#include <cstring>
#include <stdexcept>
#include <vector>

namespace
{
    DirectX::ScratchImage LoadOpenExrImage(
        const std::filesystem::path& path,
        DirectX::TexMetadata& metadata)
    {
        using namespace OPENEXR_IMF_NAMESPACE;

        RgbaInputFile input(path.string().c_str());
        const IMATH_NAMESPACE::Box2i& dataWindow = input.dataWindow();
        const int width = dataWindow.max.x - dataWindow.min.x + 1;
        const int height = dataWindow.max.y - dataWindow.min.y + 1;
        Assert(width > 0 && height > 0, "OpenEXR image has an invalid data window.");

        std::vector<Rgba> pixels(static_cast<size_t>(width) * static_cast<size_t>(height));
        input.setFrameBuffer(ComputeBasePointer(pixels.data(), dataWindow), 1u, static_cast<size_t>(width));
        input.readPixels(dataWindow.min.y, dataWindow.max.y);

        DirectX::ScratchImage image;
        ThrowIfFailed(image.Initialize2D(DXGI_FORMAT_R16G16B16A16_FLOAT, width, height, 1u, 1u));
        const DirectX::Image* destination = image.GetImage(0u, 0u, 0u);
        for (int row = 0; row < height; ++row)
        {
            std::memcpy(
                destination->pixels + static_cast<size_t>(row) * destination->rowPitch,
                pixels.data() + static_cast<size_t>(row) * static_cast<size_t>(width),
                static_cast<size_t>(width) * sizeof(Rgba));
        }

        metadata = image.GetMetadata();
        return image;
    }

    DirectX::ScratchImage DecodeTexture(
        const std::filesystem::path& path,
        DirectX::TexMetadata& metadata)
    {
        DirectX::ScratchImage image;
        if (path.extension() == ".dds")
        {
            ThrowIfFailed(DirectX::LoadFromDDSFile(
                path.c_str(), DirectX::DDS_FLAGS_FORCE_RGB, &metadata, image));
        }
        else if (path.extension() == ".hdr")
        {
            ThrowIfFailed(DirectX::LoadFromHDRFile(path.c_str(), &metadata, image));
        }
        else if (path.extension() == ".exr")
        {
            image = LoadOpenExrImage(path, metadata);
        }
        else if (path.extension() == ".tga")
        {
            ThrowIfFailed(DirectX::LoadFromTGAFile(path.c_str(), &metadata, image));
        }
        else
        {
            ThrowIfFailed(DirectX::LoadFromWICFile(
                path.c_str(), DirectX::WIC_FLAGS_FORCE_RGB, &metadata, image));
        }

        if (metadata.dimension != DirectX::TEX_DIMENSION_TEXTURE2D || DirectX::IsCompressed(metadata.format))
        {
            return image;
        }

        DirectX::ScratchImage mipChain;
        ThrowIfFailed(DirectX::GenerateMipMaps(
            image.GetImages(),
            image.GetImageCount(),
            metadata,
            DirectX::TEX_FILTER_DEFAULT,
            0u,
            mipChain));
        metadata = mipChain.GetMetadata();
        return mipChain;
    }

    D3D12_RESOURCE_DESC BuildTextureDescription(const DirectX::TexMetadata& metadata)
    {
        switch (metadata.dimension)
        {
        case DirectX::TEX_DIMENSION_TEXTURE1D:
            return CD3DX12_RESOURCE_DESC::Tex1D(
                metadata.format,
                metadata.width,
                static_cast<UINT16>(metadata.arraySize),
                static_cast<UINT16>(metadata.mipLevels));
        case DirectX::TEX_DIMENSION_TEXTURE2D:
            return CD3DX12_RESOURCE_DESC::Tex2D(
                metadata.format,
                metadata.width,
                static_cast<UINT>(metadata.height),
                static_cast<UINT16>(metadata.arraySize),
                static_cast<UINT16>(metadata.mipLevels));
        case DirectX::TEX_DIMENSION_TEXTURE3D:
            return CD3DX12_RESOURCE_DESC::Tex3D(
                metadata.format,
                metadata.width,
                static_cast<UINT>(metadata.height),
                static_cast<UINT16>(metadata.depth),
                static_cast<UINT16>(metadata.mipLevels));
        default:
            throw std::invalid_argument("Unsupported texture dimension.");
        }
    }
}

//Modify Begin:2026-08-18 by Hui
TextureLoader::TextureLoader(std::shared_ptr<D3D12DeviceContext> deviceContext)
    : m_DeviceContext(std::move(deviceContext))
    , m_Uploader(m_DeviceContext)
    , m_MipGenerator(m_DeviceContext)
{
    Assert(m_DeviceContext != nullptr, "Texture loader requires a D3D12 device context.");
}

bool TextureLoader::Load(
    CommandList& commandList,
    Texture& texture,
    const std::filesystem::path& requestedPath,
    const TextureUsageType usage,
    const bool throwOnNotFound)
{
    Assert(
        commandList.GetDeviceContext().get() == m_DeviceContext.get(),
        "Texture loader and command list belong to different D3D12 device contexts.");

    std::filesystem::path path = requestedPath;
    if (path.extension() != ".dds")
    {
        std::filesystem::path ddsPath = path;
        ddsPath.replace_extension(".dds");
        if (exists(ddsPath))
        {
            path = std::move(ddsPath);
        }
    }
    if (!exists(path))
    {
        if (throwOnNotFound)
        {
            throw std::runtime_error("Texture file not found: " + path.string());
        }
        return false;
    }

    const std::wstring canonicalPath = std::filesystem::weakly_canonical(path).wstring();
    const D3D12TextureCacheKey cacheKey = { canonicalPath, static_cast<uint32_t>(usage) };
    Microsoft::WRL::ComPtr<ID3D12Resource> cachedTexture;
    if (m_DeviceContext->FindCachedTexture(cacheKey, cachedTexture))
    {
        m_Uploader.AssignTextureResource(commandList, texture, cachedTexture, usage, canonicalPath);
        return true;
    }

    DirectX::TexMetadata metadata = {};
    DirectX::ScratchImage image = DecodeTexture(path, metadata);
    if (usage == TextureUsageType::Albedo)
    {
        metadata.format = DirectX::MakeSRGB(metadata.format);
    }

    const D3D12_RESOURCE_DESC textureDesc = BuildTextureDescription(metadata);
    const auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    ThrowIfFailed(m_DeviceContext->GetDevice()->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &textureDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&resource)));
    m_Uploader.AssignTextureResource(commandList, texture, resource, usage, canonicalPath);

    std::vector<D3D12_SUBRESOURCE_DATA> subresources(image.GetImageCount());
    const DirectX::Image* sourceImages = image.GetImages();
    for (size_t index = 0u; index < image.GetImageCount(); ++index)
    {
        subresources[index].pData = sourceImages[index].pixels;
        subresources[index].RowPitch = sourceImages[index].rowPitch;
        subresources[index].SlicePitch = sourceImages[index].slicePitch;
    }
    m_Uploader.UploadTextureSubresources(
        commandList,
        texture,
        0u,
        static_cast<uint32_t>(subresources.size()),
        subresources.data());
    if (subresources.size() < resource->GetDesc().MipLevels)
    {
        m_MipGenerator.Generate(commandList, texture);
    }

    m_DeviceContext->CacheTexture(cacheKey, resource);
    return true;
}
//Modify End
