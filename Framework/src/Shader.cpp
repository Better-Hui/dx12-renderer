#include "Shader.h"
#include <DX12Library/Helpers.h>
#include <DX12Library/ShaderUtils.h>
#include <DX12Library/Application.h>
#include <Framework/CommandContext.h>

//Modify Begin:2026-07-24 by BestHui
#include <algorithm>

//Modify Begin:2026-07-27 by BestHui
FRAMEWORK_SUPPRESS_DEPRECATED_WARNINGS_BEGIN
//Modify End

namespace
{
    template <typename Metadata>
    void CacheMetadataName(const Metadata& metadata, const size_t index, ShaderReflectionMetadata::NameCacheMap& cache)
    {
        cache.emplace(metadata.Name, index);

        const std::string baseName = DescriptorLayout::GetBaseResourceName(metadata.Name);
        if (baseName != metadata.Name)
        {
            cache.emplace(baseName, index);
        }
    }

    template <typename Metadata>
    void AppendUniqueMetadata(
        const std::vector<Metadata>& source,
        std::vector<Metadata>& destination,
        ShaderReflectionMetadata::NameCacheMap& cache)
    {
        for (const Metadata& metadata : source)
        {
            const auto existing = std::find_if(
                destination.begin(),
                destination.end(),
                [&metadata](const Metadata& candidate)
                {
                    return candidate.Name == metadata.Name &&
                        candidate.RegisterIndex == metadata.RegisterIndex &&
                        candidate.Space == metadata.Space;
                });

            if (existing != destination.end())
            {
                continue;
            }

            destination.push_back(metadata);
            CacheMetadataName(destination.back(), destination.size() - 1u, cache);
        }
    }

    ShaderReflectionMetadata MergeGraphicsReflection(
        const ShaderReflectionMetadata& vertexShaderMetadata,
        const ShaderReflectionMetadata& pixelShaderMetadata)
    {
        ShaderReflectionMetadata merged;

        AppendUniqueMetadata(vertexShaderMetadata.m_ConstantBuffers, merged.m_ConstantBuffers, merged.m_ConstantBuffersNameCache);
        AppendUniqueMetadata(pixelShaderMetadata.m_ConstantBuffers, merged.m_ConstantBuffers, merged.m_ConstantBuffersNameCache);

        AppendUniqueMetadata(vertexShaderMetadata.m_ShaderResourceViews, merged.m_ShaderResourceViews, merged.m_ShaderResourceViewsNameCache);
        AppendUniqueMetadata(pixelShaderMetadata.m_ShaderResourceViews, merged.m_ShaderResourceViews, merged.m_ShaderResourceViewsNameCache);

        AppendUniqueMetadata(vertexShaderMetadata.m_UnorderedAccessViews, merged.m_UnorderedAccessViews, merged.m_UnorderedAccessViewsNameCache);
        AppendUniqueMetadata(pixelShaderMetadata.m_UnorderedAccessViews, merged.m_UnorderedAccessViews, merged.m_UnorderedAccessViewsNameCache);

        return merged;
    }
}
//Modify End

Shader::Shader(
    const std::shared_ptr<CommonRootSignature>& rootSignature,
    const ShaderBlob& vertexShader,
    const ShaderBlob& pixelShader,
    const std::function<void(RasterPipelineStateBuilder&)> buildPipelineState,
//Modify Begin:2026-07-21 by BestHui
    bool collectMetadata)
//Modify End
    : m_CommonRootSignature(rootSignature)
    , m_RootSignature(rootSignature)
    , m_PipelineStateBuilder(rootSignature)
{
    m_PipelineStateBuilder.WithShaders(vertexShader.GetBlob(), pixelShader.GetBlob());
    buildPipelineState(m_PipelineStateBuilder);

//Modify Begin:2026-07-21 by BestHui
    if (collectMetadata)
    {
        CollectShaderMetadata(vertexShader.GetBlob(), &m_VertexShaderMetadata);
        CollectShaderMetadata(pixelShader.GetBlob(), &m_PixelShaderMetadata);
//Modify Begin:2026-07-24 by BestHui
        BuildPipelineLayout();
//Modify End
    }
//Modify End
}

//Modify Begin:2026-07-27 by BestHui
Shader::Shader(
    const ShaderBlob& vertexShader,
    const ShaderBlob& pixelShader,
    const std::function<void(RasterPipelineStateBuilder&)> buildPipelineState)
{
    CollectShaderMetadata(vertexShader.GetBlob(), &m_VertexShaderMetadata);
    CollectShaderMetadata(pixelShader.GetBlob(), &m_PixelShaderMetadata);
    BuildPipelineLayout();
    BuildReflectedRootSignature();

    m_UseReflectedRootSignature = true;
    m_PipelineStateBuilder
        .WithRootSignature(m_RootSignature)
        .WithShaders(vertexShader.GetBlob(), pixelShader.GetBlob());
    buildPipelineState(m_PipelineStateBuilder);
}
//Modify End

void Shader::Bind(CommandList& commandList)
{
    CommandContext(commandList).BindPipeline(*this);
}

void Shader::Unbind(CommandList& commandList)
{
    if (m_CommonRootSignature)
    {
        m_CommonRootSignature->UnbindMaterialShaderResourceViews(commandList);
    }
}

//Modify Begin:2026-07-27 by BestHui
void Shader::ApplyBindings(CommandList& commandList) const
{
    if (m_UseReflectedRootSignature)
    {
        CommandContext(commandList).BindDescriptorSet(*m_DescriptorSet, PipelineBindPoint::Graphics);
    }
}
//Modify End

void Shader::SetMaterialConstantBuffer(CommandList& commandList, size_t size, const void* data)
{
    Assert(m_CommonRootSignature != nullptr, "Material constant buffer shortcut requires the common root signature.");
    m_CommonRootSignature->SetMaterialConstantBuffer(commandList, size, data);
}

//Modify Begin:2026-07-24 by BestHui
bool Shader::HasConstantBuffer(const std::string& variableName) const
{
    if (m_PipelineLayout != nullptr)
    {
        return FindPipelineBinding(variableName, DescriptorBindingKind::ConstantBuffer) != nullptr;
    }

    return m_VertexShaderMetadata.m_ConstantBuffersNameCache.find(variableName) != m_VertexShaderMetadata.m_ConstantBuffersNameCache.end() ||
        m_PixelShaderMetadata.m_ConstantBuffersNameCache.find(variableName) != m_PixelShaderMetadata.m_ConstantBuffersNameCache.end();
}

bool Shader::HasShaderResourceView(const std::string& variableName) const
{
    if (m_PipelineLayout != nullptr)
    {
        return FindPipelineBinding(variableName, DescriptorBindingKind::ShaderResourceView) != nullptr;
    }

    return m_VertexShaderMetadata.m_ShaderResourceViewsNameCache.find(variableName) != m_VertexShaderMetadata.m_ShaderResourceViewsNameCache.end() ||
        m_PixelShaderMetadata.m_ShaderResourceViewsNameCache.find(variableName) != m_PixelShaderMetadata.m_ShaderResourceViewsNameCache.end();
}

bool Shader::HasUnorderedAccessView(const std::string& variableName) const
{
    if (m_PipelineLayout != nullptr)
    {
        return FindPipelineBinding(variableName, DescriptorBindingKind::UnorderedAccessView) != nullptr;
    }

    return m_VertexShaderMetadata.m_UnorderedAccessViewsNameCache.find(variableName) != m_VertexShaderMetadata.m_UnorderedAccessViewsNameCache.end() ||
        m_PixelShaderMetadata.m_UnorderedAccessViewsNameCache.find(variableName) != m_PixelShaderMetadata.m_UnorderedAccessViewsNameCache.end();
}
//Modify End

void Shader::SetConstantBuffer(CommandList& commandList, const std::string& variableName, size_t size, const void* data)
{
//Modify Begin:2026-07-24 by BestHui
    if (m_UseReflectedRootSignature)
    {
        (void)commandList;
        m_DescriptorSet->SetConstantBufferData(variableName, data, size);
        return;
    }

    if (const PipelineDescriptorRangeDesc* binding = FindPipelineBinding(variableName, DescriptorBindingKind::ConstantBuffer))
    {
        const UINT rootParameterIndex = m_DescriptorSet->SetConstantBufferData(variableName, data, size);
        const PipelineBoundResource& boundResource = m_DescriptorSet->GetBoundResource(rootParameterIndex);
        switch (binding->RegisterSpace)
        {
        case CommonRootSignature::MATERIAL_REGISTER_SPACE:
            m_CommonRootSignature->SetMaterialConstantBuffer(commandList, boundResource.ConstantBufferData.size(), boundResource.ConstantBufferData.data());
            return;
        case CommonRootSignature::MODEL_REGISTER_SPACE:
            m_CommonRootSignature->SetModelConstantBuffer(commandList, boundResource.ConstantBufferData.size(), boundResource.ConstantBufferData.data());
            return;
        case CommonRootSignature::PIPELINE_REGISTER_SPACE:
            m_CommonRootSignature->SetPipelineConstantBuffer(commandList, boundResource.ConstantBufferData.size(), boundResource.ConstantBufferData.data());
            return;
        default:
            throw std::exception("Invalid space index for a constant buffer.");
        }
    }
//Modify End

    const auto bindConstantBuffer = [this, &commandList, size, data](const ShaderUtils::ConstantBufferMetadata& cbufferMetadata)
    {
        switch (cbufferMetadata.Space)
        {
        case CommonRootSignature::MATERIAL_REGISTER_SPACE:
            m_CommonRootSignature->SetMaterialConstantBuffer(commandList, size, data);
            break;
        case CommonRootSignature::MODEL_REGISTER_SPACE:
            m_CommonRootSignature->SetModelConstantBuffer(commandList, size, data);
            break;
        case CommonRootSignature::PIPELINE_REGISTER_SPACE:
            m_CommonRootSignature->SetPipelineConstantBuffer(commandList, size, data);
            break;
        default:
            throw std::exception("Invalid space index for a constant buffer.");
        }
    };

    bool found = false;
    const auto vsFindResult = m_VertexShaderMetadata.m_ConstantBuffersNameCache.find(variableName);
    if (vsFindResult != m_VertexShaderMetadata.m_ConstantBuffersNameCache.end())
    {
        bindConstantBuffer(m_VertexShaderMetadata.m_ConstantBuffers[vsFindResult->second]);
        found = true;
    }

    const auto psFindResult = m_PixelShaderMetadata.m_ConstantBuffersNameCache.find(variableName);
    if (psFindResult != m_PixelShaderMetadata.m_ConstantBuffersNameCache.end())
    {
        bindConstantBuffer(m_PixelShaderMetadata.m_ConstantBuffers[psFindResult->second]);
        found = true;
    }

    if (!found)
    {
        throw std::exception("Shader variable not found.");
    }
}

void Shader::SetShaderResourceView(CommandList& commandList, const std::string& variableName, const ShaderResourceView& shaderResourceView)
{
//Modify Begin:2026-07-24 by BestHui
    if (m_UseReflectedRootSignature)
    {
        (void)commandList;
        m_DescriptorSet->SetShaderResourceView(variableName, 0u, shaderResourceView);
        return;
    }

    if (const PipelineDescriptorRangeDesc* binding = FindPipelineBinding(variableName, DescriptorBindingKind::ShaderResourceView))
    {
        const UINT rootParameterIndex = m_DescriptorSet->SetShaderResourceView(variableName, 0u, shaderResourceView);
        const PipelineBoundResource& boundResource = m_DescriptorSet->GetBoundResource(rootParameterIndex);
        Assert(!boundResource.ShaderResourceViews.empty() && boundResource.ShaderResourceViews[0].has_value(), "Raster SRV is not bound.");
        const ShaderResourceView& boundShaderResourceView = *boundResource.ShaderResourceViews[0];
        switch (binding->RegisterSpace)
        {
        case CommonRootSignature::MATERIAL_REGISTER_SPACE:
            m_CommonRootSignature->SetMaterialShaderResourceView(commandList, binding->ShaderRegister, boundShaderResourceView);
            return;
        case CommonRootSignature::PIPELINE_REGISTER_SPACE:
            m_CommonRootSignature->SetPipelineShaderResourceView(commandList, binding->ShaderRegister, boundShaderResourceView);
            return;
        default:
            throw std::exception("Invalid space index for an SRV.");
        }
    }
//Modify End

    const auto vsFindResult = m_VertexShaderMetadata.m_ShaderResourceViewsNameCache.find(variableName);
    const auto vsFound = vsFindResult != m_VertexShaderMetadata.m_ShaderResourceViewsNameCache.end();
    if (vsFound)
    {
        auto index = vsFindResult->second;
        const auto& srvMetadata = m_VertexShaderMetadata.m_ShaderResourceViews[index];
        switch (srvMetadata.Space)
        {
        case CommonRootSignature::MATERIAL_REGISTER_SPACE:
            m_CommonRootSignature->SetMaterialShaderResourceView(commandList, srvMetadata.RegisterIndex, shaderResourceView);
            break;
        case CommonRootSignature::PIPELINE_REGISTER_SPACE:
            m_CommonRootSignature->SetPipelineShaderResourceView(commandList, srvMetadata.RegisterIndex, shaderResourceView);
            break;
        default:
            throw std::exception("Invalid space index for an SRV.");
        }
    }

    const auto psFindResult = m_PixelShaderMetadata.m_ShaderResourceViewsNameCache.find(variableName);
    const auto psFound = psFindResult != m_PixelShaderMetadata.m_ShaderResourceViewsNameCache.end();
    if (psFound)
    {
        auto index = psFindResult->second;
        const auto& srvMetadata = m_PixelShaderMetadata.m_ShaderResourceViews[index];
        switch (srvMetadata.Space)
        {
        case CommonRootSignature::MATERIAL_REGISTER_SPACE:
            m_CommonRootSignature->SetMaterialShaderResourceView(commandList, srvMetadata.RegisterIndex, shaderResourceView);
            break;
        case CommonRootSignature::PIPELINE_REGISTER_SPACE:
            m_CommonRootSignature->SetPipelineShaderResourceView(commandList, srvMetadata.RegisterIndex, shaderResourceView);
            break;
        default:
            throw std::exception("Invalid space index for an SRV.");
        }
    }

    if (!vsFound && !psFound)
    {
        throw std::exception("Shader variable not found.");
    }
}

void Shader::SetTexture(CommandList& commandList, const std::string& variableName, const ShaderResourceView& shaderResourceView)
{
    SetShaderResourceView(commandList, variableName, shaderResourceView);
}

void Shader::SetTexture(CommandList& commandList, const std::string& variableName, const std::shared_ptr<Resource>& texture)
{
    SetTexture(commandList, variableName, ShaderResourceView(texture));
}

//Modify Begin:2026-07-23 by BestHui
void Shader::SetUnorderedAccessView(CommandList& commandList, const std::string& variableName, const UnorderedAccessView& unorderedAccessView)
{
    //Modify Begin:2026-07-24 by BestHui
    if (m_UseReflectedRootSignature)
    {
        (void)commandList;
        m_DescriptorSet->SetUnorderedAccessView(variableName, unorderedAccessView);
        return;
    }

    if (const PipelineDescriptorRangeDesc* binding = FindPipelineBinding(variableName, DescriptorBindingKind::UnorderedAccessView))
    {
        const UINT rootParameterIndex = m_DescriptorSet->SetUnorderedAccessView(variableName, unorderedAccessView);
        const PipelineBoundResource& boundResource = m_DescriptorSet->GetBoundResource(rootParameterIndex);
        Assert(boundResource.UnorderedAccessView.has_value(), "Raster UAV is not bound.");
        if (binding->RegisterSpace != CommonRootSignature::MATERIAL_REGISTER_SPACE)
        {
            throw std::exception("Invalid space index for a UAV.");
        }

        m_CommonRootSignature->SetUnorderedAccessView(commandList, binding->ShaderRegister, *boundResource.UnorderedAccessView);
        return;
    }
    //Modify End

    const auto vsFindResult = m_VertexShaderMetadata.m_UnorderedAccessViewsNameCache.find(variableName);
    const auto vsFound = vsFindResult != m_VertexShaderMetadata.m_UnorderedAccessViewsNameCache.end();
    if (vsFound)
    {
        const auto index = vsFindResult->second;
        const auto& uavMetadata = m_VertexShaderMetadata.m_UnorderedAccessViews[index];
        if (uavMetadata.Space != CommonRootSignature::MATERIAL_REGISTER_SPACE)
        {
            throw std::exception("Invalid space index for a UAV.");
        }

        m_CommonRootSignature->SetUnorderedAccessView(commandList, uavMetadata.RegisterIndex, unorderedAccessView);
    }

    const auto psFindResult = m_PixelShaderMetadata.m_UnorderedAccessViewsNameCache.find(variableName);
    const auto psFound = psFindResult != m_PixelShaderMetadata.m_UnorderedAccessViewsNameCache.end();
    if (psFound)
    {
        const auto index = psFindResult->second;
        const auto& uavMetadata = m_PixelShaderMetadata.m_UnorderedAccessViews[index];
        if (uavMetadata.Space != CommonRootSignature::MATERIAL_REGISTER_SPACE)
        {
            throw std::exception("Invalid space index for a UAV.");
        }

        m_CommonRootSignature->SetUnorderedAccessView(commandList, uavMetadata.RegisterIndex, unorderedAccessView);
    }

    if (!vsFound && !psFound)
    {
        throw std::exception("Shader variable not found.");
    }
}
//Modify End

Microsoft::WRL::ComPtr<ID3D12PipelineState> Shader::GetPipelineState(const Microsoft::WRL::ComPtr<ID3D12Device2>& device, const RenderTargetState& renderTargetState)
{
    return m_PipelineStateObjects.GetOrCreate(
        renderTargetState,
        [this, &device, &renderTargetState]()
    {
        const auto& formats = renderTargetState.GetFormats();
        std::vector<DXGI_FORMAT> renderTargetFormats(formats.GetCount());
        memcpy(renderTargetFormats.data(), formats.GetFormats(), sizeof(DXGI_FORMAT) * renderTargetFormats.size());
        m_PipelineStateBuilder.WithRenderTargetFormats(renderTargetFormats, formats.GetDepthStencilFormat());

        m_PipelineStateBuilder.WithSampleDesc(renderTargetState.GetSampleDesc());

        return m_PipelineStateBuilder.Build(device);
    });
}

void Shader::CollectShaderMetadata(const Microsoft::WRL::ComPtr<ID3DBlob>& shader, ShaderMetadata* outMetadata)
{
    *outMetadata = ShaderReflection::CollectShader(shader);
}

//Modify Begin:2026-07-24 by BestHui
void Shader::BuildPipelineLayout()
{
    PipelineLayoutReflectionOptions layoutOptions;
    layoutOptions.MaxDescriptorCount = CommonRootSignature::PIPELINE_SRVS_COUNT;
    const ShaderReflectionMetadata mergedReflection = MergeGraphicsReflection(m_VertexShaderMetadata, m_PixelShaderMetadata);
    m_PipelineLayout = std::make_unique<PipelineLayout>(
        PipelineLayout::CreateDescFromReflection(mergedReflection, layoutOptions));
    m_BindingSet = std::make_unique<PipelineBindingSet>(*m_PipelineLayout);
    m_DescriptorSet = std::make_unique<PipelineDescriptorSet>(*m_PipelineLayout);
}

void Shader::BuildReflectedRootSignature()
{
    using RootParameter = CD3DX12_ROOT_PARAMETER1;
    using DescriptorRange = CD3DX12_DESCRIPTOR_RANGE1;
    using StaticSampler = CD3DX12_STATIC_SAMPLER_DESC;

    Assert(m_PipelineLayout != nullptr, "Pipeline layout must be built before creating a reflected root signature.");

    const ShaderReflectionMetadata mergedReflection = MergeGraphicsReflection(m_VertexShaderMetadata, m_PixelShaderMetadata);
    std::vector<DescriptorRange> descriptorRanges;
    descriptorRanges.reserve(m_PipelineLayout->GetDesc().DescriptorRanges.size());

    std::vector<RootParameter> rootParameters;
    rootParameters.reserve(m_PipelineLayout->GetDesc().DescriptorRanges.size());

    for (const PipelineDescriptorRangeDesc& range : m_PipelineLayout->GetDesc().DescriptorRanges)
    {
        RootParameter rootParameter;
        Assert(range.RootParameterIndex == rootParameters.size(), "Pipeline layout root parameter indices must be sequential.");

        if (range.Kind == DescriptorBindingKind::ConstantBuffer)
        {
            rootParameter.InitAsConstantBufferView(range.ShaderRegister, range.RegisterSpace);
        }
        else if (range.Kind == DescriptorBindingKind::AccelerationStructure)
        {
            rootParameter.InitAsShaderResourceView(range.ShaderRegister, range.RegisterSpace);
        }
        else
        {
            const D3D12_DESCRIPTOR_RANGE_TYPE rangeType = range.Kind == DescriptorBindingKind::UnorderedAccessView ?
                D3D12_DESCRIPTOR_RANGE_TYPE_UAV :
                D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            descriptorRanges.emplace_back(
                rangeType,
                range.DescriptorCount,
                range.ShaderRegister,
                range.RegisterSpace,
                D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);
            rootParameter.InitAsDescriptorTable(1, &descriptorRanges.back(), D3D12_SHADER_VISIBILITY_ALL);
        }

        rootParameters.push_back(rootParameter);

        if (range.Kind == DescriptorBindingKind::ShaderResourceView)
        {
            const auto srvFindResult = mergedReflection.m_ShaderResourceViewsNameCache.find(range.Name);
            Assert(srvFindResult != mergedReflection.m_ShaderResourceViewsNameCache.end(), "SRV reflection metadata was not found.");
            m_PipelineLayout->AddDefaultShaderResourceViewTable(
                range.RootParameterIndex,
                range.DescriptorCount,
                mergedReflection.m_ShaderResourceViews[srvFindResult->second]);
        }
        else if (range.Kind == DescriptorBindingKind::UnorderedAccessView)
        {
            const auto uavFindResult = mergedReflection.m_UnorderedAccessViewsNameCache.find(range.Name);
            Assert(uavFindResult != mergedReflection.m_UnorderedAccessViewsNameCache.end(), "UAV reflection metadata was not found.");
            m_PipelineLayout->AddDefaultUnorderedAccessViewTable(
                range.RootParameterIndex,
                range.DescriptorCount,
                mergedReflection.m_UnorderedAccessViews[uavFindResult->second]);
        }
    }

    const StaticSampler staticSamplers[] =
    {
        StaticSampler(0, D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP),
        StaticSampler(1, D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP),
        StaticSampler(2, D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP),
        StaticSampler(3, D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP),
        StaticSampler(4, D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 0, 16,
            D3D12_COMPARISON_FUNC_LESS_EQUAL)
    };

    D3D12_ROOT_SIGNATURE_DESC1 rootSignatureDesc{};
    rootSignatureDesc.NumParameters = static_cast<UINT>(rootParameters.size());
    rootSignatureDesc.pParameters = rootParameters.data();
    rootSignatureDesc.NumStaticSamplers = _countof(staticSamplers);
    rootSignatureDesc.pStaticSamplers = staticSamplers;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    m_RootSignature = std::make_shared<RootSignature>(rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1_1);
}

const PipelineDescriptorRangeDesc* Shader::FindPipelineBinding(
    const std::string& variableName,
    const DescriptorBindingKind expectedKind) const
{
    return m_BindingSet != nullptr ? m_BindingSet->FindRange(variableName, expectedKind) : nullptr;
}
//Modify End

//Modify Begin:2026-07-27 by BestHui
FRAMEWORK_SUPPRESS_DEPRECATED_WARNINGS_END
//Modify End
