#pragma once

#include "cstdint"
#include <functional>
//Modify Begin:2026-08-19 by Hui
#include <stdexcept>
//Modify End

#include "dxgi.h"
#include <d3d12.h>

#include "DX12Library/ClearValue.h"
#include "DX12Library/TextureUsageType.h"

#include "RenderMetadata.h"
#include "ResourceId.h"

namespace RenderGraph
{
    template <typename T>
    using RenderMetadataExpression = std::function<T(const RenderMetadata&)>;

    enum ResourceInitAction
    {
        Clear,
        Discard,
        CopyDestination,
//Modify Begin:2026-08-19 by Hui
        Preserve,
//Modify End
    };

    struct TextureDescription
    {
        ResourceId m_Id;
        RenderMetadataExpression<uint32_t> m_WidthExpression;
        RenderMetadataExpression<uint32_t> m_HeightExpression;
        DXGI_FORMAT m_Format;
        ClearValue m_ClearValue;
        ResourceInitAction m_InitAction;

        uint32_t m_ArraySize = 1;
        uint32_t m_MipLevels = 1;
        uint32_t m_SampleCount = 1;
//Modify Begin:2026-08-19 by Hui
        D3D12_RESOURCE_FLAGS m_ExtraResourceFlags = D3D12_RESOURCE_FLAG_NONE;
        D3D12_HEAP_FLAGS m_HeapFlags = D3D12_HEAP_FLAG_NONE;
        bool m_DedicatedResource = false;
//Modify End

        TextureDescription()
            : m_Id(0)
            , m_WidthExpression(nullptr)
            , m_HeightExpression(nullptr)
            , m_Format(DXGI_FORMAT_UNKNOWN)
            , m_InitAction(Clear)
        { }

        TextureDescription(const ResourceId id,
            const RenderMetadataExpression<uint32_t>& widthExpression, const RenderMetadataExpression<uint32_t>& heightExpression,
            const DXGI_FORMAT format, const ClearValue::COLOR clearColor, ResourceInitAction initAction
//Modify Begin:2026-08-19 by Hui
            , D3D12_RESOURCE_FLAGS extraResourceFlags = D3D12_RESOURCE_FLAG_NONE,
            D3D12_HEAP_FLAGS heapFlags = D3D12_HEAP_FLAG_NONE,
            bool dedicatedResource = false
//Modify End
        )
            : m_Id(id)
            , m_WidthExpression(widthExpression)
            , m_HeightExpression(heightExpression)
            , m_Format(format)
            , m_ClearValue(format, clearColor)
            , m_InitAction(initAction)
//Modify Begin:2026-08-19 by Hui
            , m_ExtraResourceFlags(extraResourceFlags)
            , m_HeapFlags(heapFlags)
            , m_DedicatedResource(dedicatedResource)
//Modify End
        { }

        TextureDescription(const ResourceId id,
            const RenderMetadataExpression<uint32_t>& widthExpression, const RenderMetadataExpression<uint32_t>& heightExpression,
            const DXGI_FORMAT format, const ClearValue::DEPTH_STENCIL_VALUE clearDepthStencilValue, ResourceInitAction initAction
//Modify Begin:2026-08-19 by Hui
            , D3D12_RESOURCE_FLAGS extraResourceFlags = D3D12_RESOURCE_FLAG_NONE,
            D3D12_HEAP_FLAGS heapFlags = D3D12_HEAP_FLAG_NONE,
            bool dedicatedResource = false
//Modify End
        )
            : m_Id(id)
            , m_WidthExpression(widthExpression)
            , m_HeightExpression(heightExpression)
            , m_Format(format)
            , m_ClearValue(format, clearDepthStencilValue)
            , m_InitAction(initAction)
//Modify Begin:2026-08-19 by Hui
            , m_ExtraResourceFlags(extraResourceFlags)
            , m_HeapFlags(heapFlags)
            , m_DedicatedResource(dedicatedResource)
//Modify End
        { }
    };

//Modify Begin:2026-08-19 by Hui
    enum class BufferKind : uint8_t
    {
        Structured,
        Raw,
    };

    enum class BufferUsage : uint8_t
    {
        None = 0,
        ShaderResource = 1 << 0,
        UnorderedAccess = 1 << 1,
    };

    constexpr BufferUsage operator|(const BufferUsage left, const BufferUsage right)
    {
        return static_cast<BufferUsage>(
            static_cast<uint8_t>(left) | static_cast<uint8_t>(right));
    }

    constexpr bool HasBufferUsage(const BufferUsage usage, const BufferUsage requestedUsage)
    {
        return (static_cast<uint8_t>(usage) & static_cast<uint8_t>(requestedUsage)) != 0;
    }
    //Modify End

    struct BufferDescription
    {
        ResourceId m_Id;
        RenderMetadataExpression<size_t> m_SizeExpression;
        size_t m_Stride;
        ResourceInitAction m_InitAction;
        BufferKind m_Kind;
        BufferUsage m_Usage;
        //Modify End

        BufferDescription()
            : m_Id(0)
            , m_SizeExpression(nullptr)
            , m_Stride(0)
            , m_InitAction(Clear)
            , m_Kind(BufferKind::Structured)
            , m_Usage(BufferUsage::ShaderResource)
            //Modify End
        { }

        BufferDescription(
            const ResourceId id,
            const RenderMetadataExpression<size_t>& sizeExpression,
            const size_t stride,
            const ResourceInitAction initAction,
            const BufferKind kind = BufferKind::Structured,
            const BufferUsage usage = BufferUsage::ShaderResource)
            : m_Id(id)
            , m_SizeExpression(sizeExpression)
            , m_Stride(stride)
            , m_InitAction(initAction)
            , m_Kind(kind)
            , m_Usage(usage)
            //Modify End
        { }
    };

    struct TokenDescription
    {
        ResourceId m_Id;
    };

    enum class ResourceType
    {
        Texture,
        Buffer,
        Token,
    };

    struct ResourceDescription
    {
        ResourceId m_Id;
        D3D12_RESOURCE_DESC m_DxDesc;
        uint64_t m_TotalSize;
        uint64_t m_ElementsCount;
        uint64_t m_Alignment;
        ResourceType m_ResourceType;

        TextureDescription m_TextureDescription;
        TextureUsageType m_TextureUsageType;
        D3D12_HEAP_FLAGS m_HeapFlags = D3D12_HEAP_FLAG_NONE;
        bool m_DedicatedResource = false;
//Modify End

        BufferDescription m_BufferDescription;

        TokenDescription m_TokenDescription;

        ResourceInitAction GetInitAction() const
        {
            switch (m_ResourceType)
            {
            case ResourceType::Texture:
                return m_TextureDescription.m_InitAction;
            case ResourceType::Buffer:
                return m_BufferDescription.m_InitAction;
            default:
//Modify Begin:2026-08-19 by Hui
                throw std::logic_error("Resource init action is undefined for this resource type.");
//Modify End
            }
        }

        const ClearValue& GetClearValue() const
        {
            switch (m_ResourceType)
            {
            case ResourceType::Texture:
                return m_TextureDescription.m_ClearValue;
            default:
//Modify Begin:2026-08-19 by Hui
                throw std::logic_error("Clear value is undefined for this resource type.");
//Modify End
            }
        }
    };
}
