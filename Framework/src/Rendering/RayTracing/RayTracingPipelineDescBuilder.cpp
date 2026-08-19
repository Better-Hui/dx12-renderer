#include <Framework/Rendering/RayTracing/RayTracingShader.h>

#include <DX12Library/ShaderUtils.h>
#include <Framework/Rendering/Pipeline/DescriptorLayout.h>
#include <Framework/Rendering/Pipeline/ShaderBlob.h>
#include <Framework/Rendering/Pipeline/ShaderReflection.h>

#include <algorithm>
//Modify Begin:2026-08-12 by Hui
#include <stdexcept>
//Modify End
#include <utility>

//Modify Begin:2026-08-12 by Hui

namespace
{
    bool IsBufferSrvDimension(const D3D_SRV_DIMENSION dimension)
    {
        return dimension == D3D_SRV_DIMENSION_BUFFER;
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

RayTracingPipelineDescBuilder RayTracingPipelineDescBuilder::ReflectedDefault(const ShaderBlob& shaderLibrary)
{
    RayTracingPipelineDesc desc;

    const ShaderReflectionMetadata reflection = ShaderReflection::CollectLibrary(shaderLibrary.GetBlob());

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
        RayTracingShaderBindingType bindingType =
            srv.InputType == D3D_SIT_RTACCELERATIONSTRUCTURE ?
            RayTracingShaderBindingType::AccelerationStructure :
            RayTracingShaderBindingType::StructuredBuffer;
        const uint32_t descriptorCount = DescriptorLayout::NormalizeDescriptorCount(srv.BindCount, desc.MaxDescriptorCount);

        if (bindingType != RayTracingShaderBindingType::AccelerationStructure &&
            (srv.InputType == D3D_SIT_TEXTURE || !IsBufferSrvDimension(srv.Dimension) || descriptorCount > 1u))
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

    RayTracingPipelineDescBuilder builder(std::move(desc));
    for (const auto& sampler : reflection.m_Samplers)
    {
        const uint32_t samplerCount = DescriptorLayout::NormalizeDescriptorCount(
            sampler.BindCount,
            builder.m_Desc.MaxDescriptorCount);
        for (uint32_t samplerOffset = 0u; samplerOffset < samplerCount; ++samplerOffset)
        {
            builder.m_ReflectedStaticSamplerCoordinates.emplace_back(
                sampler.RegisterIndex + samplerOffset,
                sampler.Space);
        }
    }
    return builder;
}

RayTracingPipelineDescBuilder& RayTracingPipelineDescBuilder::WithExport(std::wstring exportName)
{
    if (exportName.empty())
    {
        throw std::invalid_argument("Ray tracing export name must not be empty.");
    }

    const auto existingExport = std::find(
        m_Desc.Exports.begin(),
        m_Desc.Exports.end(),
        exportName);
    if (existingExport == m_Desc.Exports.end())
    {
        m_Desc.Exports.push_back(std::move(exportName));
    }
    return *this;
}

RayTracingPipelineDescBuilder& RayTracingPipelineDescBuilder::WithTriangleHitGroup(
    std::wstring hitGroupName,
    std::wstring closestHitShader,
    std::wstring anyHitShader,
    std::wstring intersectionShader)
{
    RayTracingHitGroupDesc hitGroup = {
        std::move(hitGroupName),
        std::move(closestHitShader),
        std::move(anyHitShader),
        std::move(intersectionShader),
        D3D12_HIT_GROUP_TYPE_TRIANGLES
    };
    if (hitGroup.Name.empty())
    {
        throw std::invalid_argument("Ray tracing hit group name must not be empty.");
    }
    if (hitGroup.ClosestHitShader.empty())
    {
        throw std::invalid_argument("Triangle ray tracing hit groups require a closest-hit shader.");
    }

    WithExport(hitGroup.ClosestHitShader);
    if (!hitGroup.AnyHitShader.empty())
    {
        WithExport(hitGroup.AnyHitShader);
    }
    if (!hitGroup.IntersectionShader.empty())
    {
        WithExport(hitGroup.IntersectionShader);
    }

    const auto existingHitGroup = std::find_if(
        m_Desc.HitGroups.begin(),
        m_Desc.HitGroups.end(),
        [&hitGroup](const RayTracingHitGroupDesc& candidate)
        {
            return candidate.Name == hitGroup.Name;
        });
    if (existingHitGroup != m_Desc.HitGroups.end())
    {
        throw std::invalid_argument("Ray tracing pipeline contains duplicate hit group names.");
    }

    m_Desc.HitGroups.push_back(std::move(hitGroup));
    return *this;
}

RayTracingPipelineDescBuilder& RayTracingPipelineDescBuilder::WithRayGenerationPass(
    std::string passName,
    std::wstring rayGenerationShader,
    std::vector<std::wstring> missShaders,
    std::vector<std::wstring> hitGroups)
{
    if (passName.empty() || rayGenerationShader.empty())
    {
        throw std::invalid_argument("Ray tracing pass name and ray-generation export must not be empty.");
    }

    WithExport(rayGenerationShader);
    for (const std::wstring& missShader : missShaders)
    {
        WithExport(missShader);
    }
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

RayTracingPipelineDescBuilder& RayTracingPipelineDescBuilder::WithStaticSamplerContract(
    PipelineStaticSamplerContract contract)
{
    const auto existingSampler = std::find_if(
        m_Desc.RootSamplers.begin(),
        m_Desc.RootSamplers.end(),
        [&contract](const PipelineRootSamplerDesc& sampler)
        {
            return sampler.ShaderRegister == contract.ShaderRegister &&
                sampler.RegisterSpace == contract.RegisterSpace;
        });

    PipelineRootSamplerDesc rootSampler;
    rootSampler.Name = std::move(contract.Name);
    rootSampler.ShaderRegister = contract.ShaderRegister;
    rootSampler.RegisterSpace = contract.RegisterSpace;
    rootSampler.Desc = contract.Desc;
    rootSampler.ShaderStages = PipelineShaderStageFlags::RayTracing;

    if (existingSampler != m_Desc.RootSamplers.end())
    {
        *existingSampler = std::move(rootSampler);
        return *this;
    }

    m_Desc.RootSamplers.push_back(std::move(rootSampler));
    return *this;
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
    for (const RayTracingShaderPassDesc& pass : m_Desc.Passes)
    {
        const auto findExport = [this](const std::wstring& exportName)
        {
            return std::find(m_Desc.Exports.begin(), m_Desc.Exports.end(), exportName);
        };

        if (pass.Name.empty() || pass.RayGenerationShader.empty() || findExport(pass.RayGenerationShader) == m_Desc.Exports.end())
        {
            throw std::invalid_argument("Ray tracing pass has an invalid ray-generation export contract.");
        }

        for (const RayTracingShaderRecordDesc& missRecord : pass.MissShaderRecords)
        {
            if (missRecord.ExportName.empty() || findExport(missRecord.ExportName) == m_Desc.Exports.end())
            {
                throw std::invalid_argument("Ray tracing pass references a miss shader that is not exported.");
            }
        }

        for (const RayTracingShaderRecordDesc& hitGroupRecord : pass.HitGroupRecords)
        {
            const auto hitGroup = std::find_if(
                m_Desc.HitGroups.begin(),
                m_Desc.HitGroups.end(),
                [&hitGroupRecord](const RayTracingHitGroupDesc& candidate)
                {
                    return candidate.Name == hitGroupRecord.ExportName;
                });
            if (hitGroupRecord.ExportName.empty() || hitGroup == m_Desc.HitGroups.end())
            {
                throw std::invalid_argument("Ray tracing pass references a hit group that was not declared.");
            }
        }
    }

    for (size_t samplerIndex = 0u; samplerIndex < m_Desc.RootSamplers.size(); ++samplerIndex)
    {
        const PipelineRootSamplerDesc& sampler = m_Desc.RootSamplers[samplerIndex];
        const auto duplicateSampler = std::find_if(
            m_Desc.RootSamplers.begin() + static_cast<std::ptrdiff_t>(samplerIndex + 1u),
            m_Desc.RootSamplers.end(),
            [&sampler](const PipelineRootSamplerDesc& candidate)
            {
                return candidate.ShaderRegister == sampler.ShaderRegister &&
                    candidate.RegisterSpace == sampler.RegisterSpace;
            });
        if (duplicateSampler != m_Desc.RootSamplers.end())
        {
            throw std::invalid_argument(
                "Ray tracing pipeline contains duplicate static sampler register and space coordinates.");
        }
    }

    for (const auto& [shaderRegister, registerSpace] : m_ReflectedStaticSamplerCoordinates)
    {
        const auto sampler = std::find_if(
            m_Desc.RootSamplers.begin(),
            m_Desc.RootSamplers.end(),
            [shaderRegister, registerSpace](const PipelineRootSamplerDesc& candidate)
            {
                return candidate.ShaderRegister == shaderRegister &&
                    candidate.RegisterSpace == registerSpace;
            });
        if (sampler == m_Desc.RootSamplers.end())
        {
            throw std::invalid_argument(
                "A reflected ray tracing sampler requires an explicit static sampler contract.");
        }
    }
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
