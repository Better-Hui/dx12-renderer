#include <Framework/RayTracingShader.h>

#include <DX12Library/ShaderUtils.h>
#include <Framework/DescriptorLayout.h>
#include <Framework/ShaderBlob.h>
#include <Framework/ShaderReflection.h>

#include <algorithm>
#include <utility>

//Modify Begin:2026-07-24 by BestHui

namespace
{
    bool IsBufferSrvDimension(const D3D_SRV_DIMENSION dimension)
    {
        return dimension == D3D_SRV_DIMENSION_BUFFER;
    }

    bool IsRayTracingAccelerationStructureSrv(const ShaderUtils::ShaderResourceViewMetadata& srv)
    {
        return DescriptorLayout::IsRayTracingAccelerationStructureSrv(srv, "Scene");
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC CreateDefaultNullTextureUavDesc()
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
        desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        return desc;
    }
}

RayTracingPipelineDescBuilder::RayTracingPipelineDescBuilder() = default;

RayTracingPipelineDescBuilder::RayTracingPipelineDescBuilder(RayTracingPipelineDesc desc)
    : m_Desc(std::move(desc))
{
}

RayTracingPipelineDescBuilder RayTracingPipelineDescBuilder::Default()
{
    return RayTracingPipelineDescBuilder(RayTracingShader::CreateDefaultPipelineDesc());
}

RayTracingPipelineDescBuilder RayTracingPipelineDescBuilder::ReflectedDefault(const ShaderBlob& shaderLibrary)
{
    RayTracingPipelineDesc desc = RayTracingShader::CreateDefaultPipelineDesc();
    desc.Bindings.clear();

    const ShaderReflectionMetadata reflection = ShaderReflection::CollectLibrary(shaderLibrary.GetBlob());
//Modify Begin:2026-07-30 by BestHui
    PipelineLayoutReflectionOptions layoutOptions;
    layoutOptions.MaxDescriptorCount = desc.MaxDescriptorCount;
    layoutOptions.AccelerationStructureFallbackName = "Scene";
    layoutOptions.ShaderStages = PipelineShaderStageFlags::RayTracing;
    desc.RootSamplers = PipelineLayout::CreateDescFromReflection(reflection, layoutOptions).RootSamplers;
//Modify End

    for (const auto& cbuffer : reflection.m_ConstantBuffers)
    {
        desc.Bindings.push_back({
            cbuffer.Name,
            RayTracingShaderBindingType::ConstantBuffer,
            cbuffer.RegisterIndex,
            cbuffer.Space,
            1
        });
    }

    for (const auto& srv : reflection.m_ShaderResourceViews)
    {
        RayTracingShaderBindingType bindingType = RayTracingShaderBindingType::StructuredBuffer;
        const uint32_t descriptorCount = DescriptorLayout::NormalizeDescriptorCount(srv.BindCount, desc.MaxDescriptorCount);

        if (IsRayTracingAccelerationStructureSrv(srv))
        {
            bindingType = RayTracingShaderBindingType::AccelerationStructure;
        }
        else if (srv.Name == "VertexBuffers")
        {
            bindingType = RayTracingShaderBindingType::VertexBufferArray;
        }
        else if (srv.Name == "IndexBuffers")
        {
            bindingType = RayTracingShaderBindingType::IndexBufferArray;
        }
        else if (srv.InputType == D3D_SIT_TEXTURE || !IsBufferSrvDimension(srv.Dimension) || descriptorCount > 1u)
        {
            bindingType = RayTracingShaderBindingType::TextureArray;
        }

        desc.Bindings.push_back({
            srv.Name,
            bindingType,
            srv.RegisterIndex,
            srv.Space,
            descriptorCount
        });
    }

    for (const auto& uav : reflection.m_UnorderedAccessViews)
    {
        desc.Bindings.push_back({
            uav.Name,
            RayTracingShaderBindingType::OutputTexture,
            uav.RegisterIndex,
            uav.Space,
            DescriptorLayout::NormalizeDescriptorCount(uav.BindCount, desc.MaxDescriptorCount),
            DescriptorLayout::CreateNullUnorderedAccessViewDesc(uav),
            true
        });
    }

    return RayTracingPipelineDescBuilder(std::move(desc));
}

RayTracingPipelineDescBuilder& RayTracingPipelineDescBuilder::WithExport(std::wstring exportName)
{
    m_Desc.Exports.push_back(std::move(exportName));
    return *this;
}

RayTracingPipelineDescBuilder& RayTracingPipelineDescBuilder::WithTriangleHitGroup(
    std::wstring hitGroupName,
    std::wstring closestHitShader,
    std::wstring anyHitShader,
    std::wstring intersectionShader)
{
    m_Desc.HitGroups.push_back({
        std::move(hitGroupName),
        std::move(closestHitShader),
        std::move(anyHitShader),
        std::move(intersectionShader),
        D3D12_HIT_GROUP_TYPE_TRIANGLES
    });
    return *this;
}

RayTracingPipelineDescBuilder& RayTracingPipelineDescBuilder::WithRayGenerationPass(
    std::string passName,
    std::wstring rayGenerationShader,
    std::vector<std::wstring> missShaders,
    std::vector<std::wstring> hitGroups)
{
    RayTracingShaderPassDesc passDesc;
    passDesc.Name = std::move(passName);
    passDesc.RayGenerationShader = std::move(rayGenerationShader);
    passDesc.MissShaderRecords.reserve(missShaders.size());
    passDesc.HitGroupRecords.reserve(hitGroups.size());

    for (std::wstring& missShader : missShaders)
    {
        passDesc.MissShaderRecords.push_back({ std::move(missShader), {} });
    }

    for (std::wstring& hitGroup : hitGroups)
    {
        passDesc.HitGroupRecords.push_back({ std::move(hitGroup), {} });
    }

    m_Desc.Passes.push_back(std::move(passDesc));
    return *this;
}

RayTracingPipelineDescBuilder& RayTracingPipelineDescBuilder::WithOutputTexture(
    std::string name,
    const uint32_t shaderRegister,
    const uint32_t registerSpace,
    const uint32_t descriptorCount)
{
    return WithBinding(std::move(name), RayTracingShaderBindingType::OutputTexture, shaderRegister, registerSpace, descriptorCount);
}

RayTracingPipelineDescBuilder& RayTracingPipelineDescBuilder::WithAccelerationStructure(
    std::string name,
    const uint32_t shaderRegister,
    const uint32_t registerSpace)
{
    return WithBinding(std::move(name), RayTracingShaderBindingType::AccelerationStructure, shaderRegister, registerSpace, 1);
}

RayTracingPipelineDescBuilder& RayTracingPipelineDescBuilder::WithConstantBuffer(
    std::string name,
    const uint32_t shaderRegister,
    const uint32_t registerSpace)
{
    return WithBinding(std::move(name), RayTracingShaderBindingType::ConstantBuffer, shaderRegister, registerSpace, 1);
}

RayTracingPipelineDescBuilder& RayTracingPipelineDescBuilder::WithStructuredBuffer(
    std::string name,
    const uint32_t shaderRegister,
    const uint32_t registerSpace)
{
    return WithBinding(std::move(name), RayTracingShaderBindingType::StructuredBuffer, shaderRegister, registerSpace, 1);
}

RayTracingPipelineDescBuilder& RayTracingPipelineDescBuilder::WithTextureArray(
    std::string name,
    const uint32_t shaderRegister,
    const uint32_t registerSpace,
    const uint32_t descriptorCount)
{
    return WithBinding(std::move(name), RayTracingShaderBindingType::TextureArray, shaderRegister, registerSpace, descriptorCount);
}

RayTracingPipelineDescBuilder& RayTracingPipelineDescBuilder::WithVertexBufferArray(
    std::string name,
    const uint32_t shaderRegister,
    const uint32_t registerSpace,
    const uint32_t descriptorCount)
{
    return WithBinding(std::move(name), RayTracingShaderBindingType::VertexBufferArray, shaderRegister, registerSpace, descriptorCount);
}

RayTracingPipelineDescBuilder& RayTracingPipelineDescBuilder::WithIndexBufferArray(
    std::string name,
    const uint32_t shaderRegister,
    const uint32_t registerSpace,
    const uint32_t descriptorCount)
{
    return WithBinding(std::move(name), RayTracingShaderBindingType::IndexBufferArray, shaderRegister, registerSpace, descriptorCount);
}

RayTracingPipelineDescBuilder& RayTracingPipelineDescBuilder::WithPayloadSize(const uint32_t payloadSizeInBytes)
{
    m_Desc.PayloadSizeInBytes = payloadSizeInBytes;
    return *this;
}

RayTracingPipelineDescBuilder& RayTracingPipelineDescBuilder::WithAttributeSize(const uint32_t attributeSizeInBytes)
{
    m_Desc.AttributeSizeInBytes = attributeSizeInBytes;
    return *this;
}

RayTracingPipelineDescBuilder& RayTracingPipelineDescBuilder::WithMaxRecursionDepth(const uint32_t maxTraceRecursionDepth)
{
    m_Desc.MaxTraceRecursionDepth = maxTraceRecursionDepth;
    return *this;
}

RayTracingPipelineDescBuilder& RayTracingPipelineDescBuilder::WithMaxDescriptorCount(const uint32_t maxDescriptorCount)
{
    m_Desc.MaxDescriptorCount = maxDescriptorCount;
    return *this;
}

RayTracingPipelineDesc RayTracingPipelineDescBuilder::Build() const
{
    return m_Desc;
}

RayTracingPipelineDescBuilder& RayTracingPipelineDescBuilder::WithBinding(
    std::string name,
    const RayTracingShaderBindingType type,
    const uint32_t shaderRegister,
    const uint32_t registerSpace,
    const uint32_t descriptorCount)
{
    const auto existingBinding = std::find_if(
        m_Desc.Bindings.begin(),
        m_Desc.Bindings.end(),
        [&name](const RayTracingShaderBindingDesc& binding)
        {
            return binding.Name == name;
        });

    if (existingBinding != m_Desc.Bindings.end())
    {
        const D3D12_UNORDERED_ACCESS_VIEW_DESC previousNullUavDesc = existingBinding->NullUnorderedAccessViewDesc;
        const bool previousHasNullUavDesc = existingBinding->HasNullUnorderedAccessViewDesc;
        const bool isOutputTexture = type == RayTracingShaderBindingType::OutputTexture;
        *existingBinding = {
            std::move(name),
            type,
            shaderRegister,
            registerSpace,
            descriptorCount,
            isOutputTexture && previousHasNullUavDesc ? previousNullUavDesc : isOutputTexture ? CreateDefaultNullTextureUavDesc() : D3D12_UNORDERED_ACCESS_VIEW_DESC{},
            isOutputTexture
        };
        return *this;
    }

    m_Desc.Bindings.push_back({
        std::move(name),
        type,
        shaderRegister,
        registerSpace,
        descriptorCount,
        type == RayTracingShaderBindingType::OutputTexture ? CreateDefaultNullTextureUavDesc() : D3D12_UNORDERED_ACCESS_VIEW_DESC{},
        type == RayTracingShaderBindingType::OutputTexture
    });
    return *this;
}

//Modify End
