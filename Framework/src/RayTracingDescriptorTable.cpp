//Modify Begin:2026-07-27 by BestHui

#include "RayTracingDescriptorTable.h"

#include "RayTracingShaderInternal.h"

#include <DX12Library/Application.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/Resource.h>
#include <DX12Library/ShaderUtils.h>
#include <DX12Library/StructuredBuffer.h>
#include <Framework/CommandContext.h>
#include <Framework/DescriptorLayout.h>
#include <Framework/Mesh.h>
#include <Framework/PipelineDescriptorSet.h>
#include <Framework/RayTracingAccelerationStructure.h>
#include <Framework/ShaderResourceView.h>
#include <Framework/UnorderedAccessView.h>

#include <algorithm>

#if defined(min)
#undef min
#endif

#if defined(max)
#undef max
#endif

namespace
{
    using namespace RayTracingShaderInternal;

    D3D12_SHADER_RESOURCE_VIEW_DESC CreateVertexBufferSrvDesc(const VertexBuffer& vertexBuffer)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
        desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.Buffer.NumElements = static_cast<UINT>(vertexBuffer.GetNumVertices());
        desc.Buffer.StructureByteStride = static_cast<UINT>(vertexBuffer.GetVertexStride());
        desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        return desc;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC CreateIndexBufferSrvDesc(const IndexBuffer& indexBuffer)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
        desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        desc.Format = indexBuffer.GetIndexFormat();
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.Buffer.NumElements = static_cast<UINT>(indexBuffer.GetNumIndices());
        desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        return desc;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC CreateNullVertexBufferSrvDesc()
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
        desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.Buffer.NumElements = 1;
        desc.Buffer.StructureByteStride = sizeof(VertexAttributes);
        desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        return desc;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC CreateNullIndexBufferSrvDesc()
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
        desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        desc.Format = DXGI_FORMAT_R32_UINT;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.Buffer.NumElements = 1;
        desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        return desc;
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC CreateDefaultNullTextureUavDesc()
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
        desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        return desc;
    }
}

void RayTracingDescriptorTable::EnsureBuilt(
    const RayTracingPipelineDesc& desc,
    const PipelineDescriptorSet& descriptorSet)
{
    if (!m_Dirty)
    {
        return;
    }

    const auto device = Application::Get().GetDevice();

    uint32_t descriptorCount = 0;
    m_DescriptorTableOffsets.clear();
    m_DescriptorTableOffsets.resize(desc.Bindings.size(), UINT32_MAX);
    for (uint32_t i = 0; i < desc.Bindings.size(); ++i)
    {
        const RayTracingShaderBindingDesc& binding = desc.Bindings[i];
        if (IsDescriptorTableBinding(binding.Type))
        {
            m_DescriptorTableOffsets[i] = descriptorCount;
            descriptorCount += std::max(1u, binding.DescriptorCount);
        }
    }

    const uint32_t requiredDescriptorCount = std::max(1u, descriptorCount);
    if (m_DescriptorAllocation.IsNull() || m_DescriptorAllocation.GetNumHandles() < requiredDescriptorCount)
    {
        m_DescriptorAllocation = Application::Get().AllocateDescriptors(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
            requiredDescriptorCount);
    }

    const D3D12_SHADER_RESOURCE_VIEW_DESC nullVertexSrvDesc = CreateNullVertexBufferSrvDesc();
    const D3D12_SHADER_RESOURCE_VIEW_DESC nullIndexSrvDesc = CreateNullIndexBufferSrvDesc();
    ShaderUtils::ShaderResourceViewMetadata nullTextureMetadata{};
    nullTextureMetadata.InputType = D3D_SIT_TEXTURE;
    nullTextureMetadata.Dimension = D3D_SRV_DIMENSION_TEXTURE2D;
    const D3D12_SHADER_RESOURCE_VIEW_DESC nullTextureSrvDesc =
        DescriptorLayout::CreateNullShaderResourceViewDesc(nullTextureMetadata);

    for (uint32_t bindingIndex = 0; bindingIndex < desc.Bindings.size(); ++bindingIndex)
    {
        const RayTracingShaderBindingDesc& binding = desc.Bindings[bindingIndex];
        if (!IsDescriptorTableBinding(binding.Type))
        {
            continue;
        }

        const uint32_t descriptorOffset = m_DescriptorTableOffsets[bindingIndex];
        const PipelineBoundResource* boundBinding = descriptorSet.FindBoundResource(bindingIndex);

        if (binding.Type == RayTracingShaderBindingType::OutputTexture)
        {
            const D3D12_UNORDERED_ACCESS_VIEW_DESC nullUavDesc = binding.HasNullUnorderedAccessViewDesc ?
                binding.NullUnorderedAccessViewDesc :
                CreateDefaultNullTextureUavDesc();
            for (uint32_t i = 0; i < binding.DescriptorCount; ++i)
            {
                if (i == 0u &&
                    boundBinding != nullptr &&
                    boundBinding->UnorderedAccessView.has_value() &&
                    boundBinding->UnorderedAccessView->m_Resource != nullptr)
                {
                    device->CopyDescriptorsSimple(
                        1,
                        m_DescriptorAllocation.GetDescriptorHandle(descriptorOffset),
                        boundBinding->UnorderedAccessView->m_Resource->GetUnorderedAccessView(
                            boundBinding->UnorderedAccessView->GetDescOrNullptr()),
                        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                }
                else
                {
                    device->CreateUnorderedAccessView(nullptr, nullptr, &nullUavDesc, m_DescriptorAllocation.GetDescriptorHandle(descriptorOffset + i));
                }
            }
            continue;
        }

        if (binding.Type == RayTracingShaderBindingType::TextureArray)
        {
            const size_t boundShaderResourceViewCount = boundBinding != nullptr ? boundBinding->ShaderResourceViews.size() : 0u;
            Assert(boundShaderResourceViewCount <= binding.DescriptorCount, "Ray tracing texture array exceeds binding descriptor count.");
            for (uint32_t i = 0; i < binding.DescriptorCount; ++i)
            {
                if (boundBinding != nullptr &&
                    i < boundBinding->ShaderResourceViews.size() &&
                    boundBinding->ShaderResourceViews[i].has_value() &&
                    boundBinding->ShaderResourceViews[i]->m_Resource != nullptr)
                {
                    const ShaderResourceView& shaderResourceView = *boundBinding->ShaderResourceViews[i];
                    device->CopyDescriptorsSimple(
                        1,
                        m_DescriptorAllocation.GetDescriptorHandle(descriptorOffset + i),
                        shaderResourceView.m_Resource->GetShaderResourceView(shaderResourceView.GetDescOrNullptr()),
                        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                }
                else
                {
                    device->CreateShaderResourceView(nullptr, &nullTextureSrvDesc, m_DescriptorAllocation.GetDescriptorHandle(descriptorOffset + i));
                }
            }
            continue;
        }

        const RayTracingAccelerationStructure* accelerationStructure = descriptorSet.GetAccelerationStructure();
        Assert(accelerationStructure != nullptr, "Ray tracing acceleration structure is not bound.");
        const std::vector<std::shared_ptr<Mesh>>& meshes = accelerationStructure->GetMeshes();
        Assert(meshes.size() <= binding.DescriptorCount, "Ray tracing mesh descriptor array exceeds binding descriptor count.");

        for (uint32_t i = 0; i < binding.DescriptorCount; ++i)
        {
            if (i >= meshes.size())
            {
                const auto& nullDesc = binding.Type == RayTracingShaderBindingType::VertexBufferArray ?
                    nullVertexSrvDesc :
                    nullIndexSrvDesc;
                device->CreateShaderResourceView(nullptr, &nullDesc, m_DescriptorAllocation.GetDescriptorHandle(descriptorOffset + i));
                continue;
            }

            const auto& mesh = meshes[i];
            if (binding.Type == RayTracingShaderBindingType::VertexBufferArray)
            {
                const VertexBuffer& vertexBuffer = mesh->GetVertexBuffer();
                const D3D12_SHADER_RESOURCE_VIEW_DESC vertexSrvDesc = CreateVertexBufferSrvDesc(vertexBuffer);
                device->CreateShaderResourceView(
                    vertexBuffer.GetD3D12Resource().Get(),
                    &vertexSrvDesc,
                    m_DescriptorAllocation.GetDescriptorHandle(descriptorOffset + i));
            }
            else
            {
                const IndexBuffer& indexBuffer = mesh->GetIndexBuffer();
                const D3D12_SHADER_RESOURCE_VIEW_DESC indexSrvDesc = CreateIndexBufferSrvDesc(indexBuffer);
                device->CreateShaderResourceView(
                    indexBuffer.GetD3D12Resource().Get(),
                    &indexSrvDesc,
                    m_DescriptorAllocation.GetDescriptorHandle(descriptorOffset + i));
            }
        }
    }

    m_Dirty = false;
}

void RayTracingDescriptorTable::Stage(const CommandContext& context, const RayTracingPipelineDesc& desc) const
{
    for (uint32_t bindingIndex = 0; bindingIndex < desc.Bindings.size(); ++bindingIndex)
    {
        const RayTracingShaderBindingDesc& binding = desc.Bindings[bindingIndex];
        if (!IsDescriptorTableBinding(binding.Type))
        {
            continue;
        }

        context.StageDynamicDescriptors(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
            bindingIndex,
            0,
            std::max(1u, binding.DescriptorCount),
            m_DescriptorAllocation.GetDescriptorHandle(m_DescriptorTableOffsets[bindingIndex]));
    }
}

void RayTracingDescriptorTable::TransitionResources(
    const CommandContext& context,
    const RayTracingPipelineDesc& desc,
    const PipelineDescriptorSet& descriptorSet) const
{
    for (uint32_t bindingIndex = 0; bindingIndex < desc.Bindings.size(); ++bindingIndex)
    {
        const RayTracingShaderBindingDesc& binding = desc.Bindings[bindingIndex];
        const PipelineBoundResource* boundBinding = descriptorSet.FindBoundResource(bindingIndex);

        if (binding.Type == RayTracingShaderBindingType::OutputTexture)
        {
            if (boundBinding != nullptr &&
                boundBinding->UnorderedAccessView.has_value() &&
                boundBinding->UnorderedAccessView->m_Resource != nullptr)
            {
                context.TransitionUnorderedAccess(*boundBinding->UnorderedAccessView->m_Resource);
            }
        }
        else if (binding.Type == RayTracingShaderBindingType::TextureArray)
        {
            if (boundBinding == nullptr)
            {
                continue;
            }

            for (const auto& shaderResourceView : boundBinding->ShaderResourceViews)
            {
                if (shaderResourceView.has_value() && shaderResourceView->m_Resource != nullptr)
                {
                    context.TransitionShaderResource(*shaderResourceView->m_Resource);
                }
            }
        }
        else if (binding.Type == RayTracingShaderBindingType::StructuredBuffer)
        {
            Assert(boundBinding != nullptr && boundBinding->StructuredBufferResource != nullptr, "Ray tracing structured buffer is not bound.");
            context.TransitionShaderResource(*boundBinding->StructuredBufferResource);
        }
    }
}

void RayTracingDescriptorTable::InsertOutputBarriers(
    const CommandContext& context,
    const RayTracingPipelineDesc& desc,
    const PipelineDescriptorSet& descriptorSet) const
{
    for (uint32_t bindingIndex = 0; bindingIndex < desc.Bindings.size(); ++bindingIndex)
    {
        const RayTracingShaderBindingDesc& binding = desc.Bindings[bindingIndex];
        if (binding.Type != RayTracingShaderBindingType::OutputTexture)
        {
            continue;
        }

        const PipelineBoundResource* boundBinding = descriptorSet.FindBoundResource(bindingIndex);
        if (boundBinding != nullptr &&
            boundBinding->UnorderedAccessView.has_value() &&
            boundBinding->UnorderedAccessView->m_Resource != nullptr)
        {
            context.UavBarrier(*boundBinding->UnorderedAccessView->m_Resource);
        }
    }
}

//Modify End
