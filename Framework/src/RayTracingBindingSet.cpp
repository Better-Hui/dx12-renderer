//Modify Begin:2026-07-27 by BestHui

#include "RayTracingShaderInternal.h"
#include "RayTracingDispatchTables.h"

#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/IndexBuffer.h>
#include <DX12Library/StructuredBuffer.h>
#include <DX12Library/Texture.h>
#include <DX12Library/VertexBuffer.h>
#include <Framework/CommandContext.h>
#include <Framework/Mesh.h>
#include <Framework/PipelineDescriptorSet.h>
#include <Framework/ShaderResourceView.h>
#include <Framework/UnorderedAccessView.h>

#include <algorithm>

namespace
{
    using namespace RayTracingShaderInternal;

//Modify Begin:2026-07-27 by BestHui
    const RayTracingShaderBindingDesc* FindBinding(
        const RayTracingPipelineDesc& desc,
        const std::string_view name)
    {
        const std::string bindingName(name);
        const auto findResult = std::find_if(
            desc.Bindings.begin(),
            desc.Bindings.end(),
            [&bindingName](const RayTracingShaderBindingDesc& binding)
            {
                return binding.Name == bindingName;
            });

        return findResult != desc.Bindings.end() ? &*findResult : nullptr;
    }

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
//Modify End
}

RayTracingBindingSet::Impl::Impl(const RayTracingShader& shader)
    : Shader(shader)
    , DescriptorSet(DescriptorPool.AllocateDescriptorSetValue(shader.GetPipelineLayout()))
{
}

const RayTracingShaderBindingDesc& RayTracingBindingSet::Impl::GetBinding(std::string_view name, RayTracingShaderBindingType expectedType) const
{
    DescriptorSet.GetBinding(name, GetDescriptorBindingKind(expectedType));
    const RayTracingShaderBindingDesc* binding = FindBinding(Shader.GetDesc(), name);
    Assert(binding != nullptr, "Ray tracing shader binding was not found.");

    if (binding->Type != expectedType)
    {
        const std::string message =
            "Ray tracing shader binding type does not match the setter. Name=" +
            std::string(name) +
            " Expected=" +
            GetRayTracingBindingTypeName(expectedType) +
            " Actual=" +
            GetRayTracingBindingTypeName(binding->Type);
        throw std::exception(message.c_str());
    }
    return *binding;
}

bool RayTracingBindingSet::Impl::HasBinding(std::string_view name) const
{
    return DescriptorSet.HasBinding(name);
}

const RayTracingShaderBindingDesc& RayTracingBindingSet::Impl::GetShaderResourceBinding(std::string_view name) const
{
    DescriptorSet.GetBinding(name, DescriptorBindingKind::ShaderResourceView);
    const RayTracingShaderBindingDesc* binding = FindBinding(Shader.GetDesc(), name);
    Assert(binding != nullptr, "Ray tracing shader binding was not found.");

    if (binding->Type != RayTracingShaderBindingType::TextureArray &&
        binding->Type != RayTracingShaderBindingType::VertexBufferArray &&
        binding->Type != RayTracingShaderBindingType::IndexBufferArray)
    {
        const std::string message =
            "Ray tracing shader binding type does not match the SRV setter. Name=" +
            std::string(name) +
            " Actual=" +
            GetRayTracingBindingTypeName(binding->Type);
        throw std::exception(message.c_str());
    }
    return *binding;
}

uint32_t RayTracingBindingSet::Impl::GetBindingIndex(const RayTracingShaderBindingDesc& binding) const
{
    const DescriptorBindingInfo& bindingInfo = DescriptorSet.GetBinding(
        binding.Name,
        GetDescriptorBindingKind(binding.Type));
    return bindingInfo.RootParameterIndex;
}

void RayTracingBindingSet::Impl::MarkDescriptorsDirty(const RayTracingShaderBindingDesc& binding)
{
    (void)binding;
}

RayTracingBindingSet::RayTracingBindingSet(const RayTracingShader& shader)
    : m_Impl(std::make_unique<Impl>(shader))
{}

RayTracingBindingSet::~RayTracingBindingSet() = default;
RayTracingBindingSet::RayTracingBindingSet(RayTracingBindingSet&&) noexcept = default;
RayTracingBindingSet& RayTracingBindingSet::operator=(RayTracingBindingSet&&) noexcept = default;

RayTracingBindingSet::Impl& RayTracingBindingSet::GetImpl()
{
    return *m_Impl;
}

const RayTracingBindingSet::Impl& RayTracingBindingSet::GetImpl() const
{
    return *m_Impl;
}

bool RayTracingBindingSet::HasBinding(std::string_view name) const
{
    return m_Impl->HasBinding(name);
}

void RayTracingBindingSet::SetTexture(std::string_view name, const ShaderResourceView& shaderResourceView)
{
    SetShaderResourceView(name, 0u, shaderResourceView);
}

void RayTracingBindingSet::SetTexture(std::string_view name, const uint32_t arrayIndex, const ShaderResourceView& shaderResourceView)
{
    SetShaderResourceView(name, arrayIndex, shaderResourceView);
}

void RayTracingBindingSet::SetTexture(std::string_view name, const std::shared_ptr<Resource>& texture)
{
    SetShaderResourceView(name, 0u, ShaderResourceView(texture));
}

void RayTracingBindingSet::SetShaderResourceView(std::string_view name, const ShaderResourceView& shaderResourceView)
{
    SetShaderResourceView(name, 0u, shaderResourceView);
}

void RayTracingBindingSet::SetShaderResourceView(
    std::string_view name,
    const uint32_t arrayIndex,
    const ShaderResourceView& shaderResourceView)
{
    const RayTracingShaderBindingDesc& binding = m_Impl->GetShaderResourceBinding(name);
    m_Impl->DescriptorSet.SetShaderResourceView(name, arrayIndex, shaderResourceView);
    m_Impl->MarkDescriptorsDirty(binding);
}

void RayTracingBindingSet::SetUnorderedAccessView(std::string_view name, const UnorderedAccessView& unorderedAccessView)
{
    const RayTracingShaderBindingDesc& binding = m_Impl->GetBinding(name, RayTracingShaderBindingType::OutputTexture);
    m_Impl->DescriptorSet.SetUnorderedAccessView(name, unorderedAccessView);
    m_Impl->MarkDescriptorsDirty(binding);
}

void RayTracingBindingSet::SetBuffer(std::string_view name, const StructuredBuffer& buffer)
{
    SetStructuredBuffer(name, buffer);
}

void RayTracingBindingSet::SetOutputTexture(std::string_view name, const std::shared_ptr<Texture>& texture)
{
    SetUnorderedAccessView(name, UnorderedAccessView(texture));
}

void RayTracingBindingSet::SetAccelerationStructure(std::string_view name, const RayTracingAccelerationStructure& accelerationStructure)
{
    m_Impl->GetBinding(name, RayTracingShaderBindingType::AccelerationStructure);
    m_Impl->DescriptorSet.SetAccelerationStructure(name, accelerationStructure);
//Modify Begin:2026-07-27 by BestHui
    const std::vector<std::shared_ptr<Mesh>>& meshes = accelerationStructure.GetMeshes();
    for (const RayTracingShaderBindingDesc& binding : m_Impl->Shader.GetDesc().Bindings)
    {
        if (binding.Type != RayTracingShaderBindingType::VertexBufferArray &&
            binding.Type != RayTracingShaderBindingType::IndexBufferArray)
        {
            continue;
        }

        Assert(meshes.size() <= binding.DescriptorCount, "Ray tracing mesh descriptor array exceeds binding descriptor count.");
        m_Impl->DescriptorSet.ClearShaderResourceViews(binding.Name);
        for (uint32_t i = 0; i < static_cast<uint32_t>(meshes.size()); ++i)
        {
            const Mesh& mesh = *meshes[i];
            if (binding.Type == RayTracingShaderBindingType::VertexBufferArray)
            {
                const VertexBuffer& vertexBuffer = mesh.GetVertexBuffer();
                m_Impl->DescriptorSet.SetShaderResource(
                    binding.Name,
                    i,
                    vertexBuffer,
                    D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE,
                    CreateVertexBufferSrvDesc(vertexBuffer));
            }
            else
            {
                const IndexBuffer& indexBuffer = mesh.GetIndexBuffer();
                m_Impl->DescriptorSet.SetShaderResource(
                    binding.Name,
                    i,
                    indexBuffer,
                    D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE,
                    CreateIndexBufferSrvDesc(indexBuffer));
            }
        }
    }
//Modify End
}

void RayTracingBindingSet::SetConstantBufferData(std::string_view name, const void* data, const size_t size)
{
    m_Impl->GetBinding(name, RayTracingShaderBindingType::ConstantBuffer);
    m_Impl->DescriptorSet.SetConstantBufferData(name, data, size);
}

void RayTracingBindingSet::SetStructuredBuffer(std::string_view name, const StructuredBuffer& buffer)
{
    m_Impl->GetBinding(name, RayTracingShaderBindingType::StructuredBuffer);
    m_Impl->DescriptorSet.SetStructuredBuffer(name, buffer);
}

void RayTracingBindingSet::SetTextureArray(std::string_view name, const std::vector<std::shared_ptr<Texture>>& textures)
{
    std::vector<ShaderResourceView> shaderResourceViews;
    shaderResourceViews.reserve(textures.size());
    for (const auto& texture : textures)
    {
        shaderResourceViews.emplace_back(texture);
    }
    SetTextureArray(name, shaderResourceViews);
}

void RayTracingBindingSet::SetTextureArray(
    std::string_view name,
    const std::vector<std::shared_ptr<Texture>>& textures,
    const std::vector<D3D12_SHADER_RESOURCE_VIEW_DESC>& srvDescs)
{
    Assert(srvDescs.size() == textures.size(), "Ray tracing texture SRV desc count must match the texture count.");
    std::vector<ShaderResourceView> shaderResourceViews;
    shaderResourceViews.reserve(textures.size());
    for (size_t i = 0; i < textures.size(); ++i)
    {
        shaderResourceViews.emplace_back(textures[i], srvDescs[i]);
    }
    SetTextureArray(name, shaderResourceViews);
}

void RayTracingBindingSet::SetTextureArray(std::string_view name, const std::vector<ShaderResourceView>& shaderResourceViews)
{
    const RayTracingShaderBindingDesc& binding = m_Impl->GetShaderResourceBinding(name);
    Assert(shaderResourceViews.size() <= binding.DescriptorCount, "Ray tracing texture array exceeds binding descriptor count.");
    m_Impl->DescriptorSet.ClearShaderResourceViews(name);
    for (uint32_t i = 0; i < static_cast<uint32_t>(shaderResourceViews.size()); ++i)
    {
        m_Impl->DescriptorSet.SetShaderResourceView(name, i, shaderResourceViews[i]);
    }

    m_Impl->MarkDescriptorsDirty(binding);
}

//Modify Begin:2026-07-27 by BestHui
const RayTracingShader& RayTracingBindingSet::GetShader() const
{
    return m_Impl->Shader;
}

const PipelineDescriptorSet& RayTracingBindingSet::GetDescriptorSet() const
{
    return m_Impl->DescriptorSet;
}

//Modify Begin:2026-07-29 by BestHui
const PipelineDescriptorPool& RayTracingBindingSet::GetDescriptorPool() const
{
    return m_Impl->DescriptorPool;
}
//Modify End

//Modify End

//Modify End
