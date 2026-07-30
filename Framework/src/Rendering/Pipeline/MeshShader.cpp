#include <Framework/Rendering/Pipeline/MeshShader.h>

#include <DX12Library/Application.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/StructuredBuffer.h>

#include <algorithm>
#include <cstring>

//Modify Begin:2026-07-30 by BestHui
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

    ShaderReflectionMetadata MergeMeshGraphicsReflection(
        const ShaderReflectionMetadata& meshShaderMetadata,
        const ShaderReflectionMetadata& pixelShaderMetadata)
    {
        ShaderReflectionMetadata merged;
        AppendUniqueMetadata(meshShaderMetadata.m_ConstantBuffers, merged.m_ConstantBuffers, merged.m_ConstantBuffersNameCache);
        AppendUniqueMetadata(pixelShaderMetadata.m_ConstantBuffers, merged.m_ConstantBuffers, merged.m_ConstantBuffersNameCache);
        AppendUniqueMetadata(meshShaderMetadata.m_ShaderResourceViews, merged.m_ShaderResourceViews, merged.m_ShaderResourceViewsNameCache);
        AppendUniqueMetadata(pixelShaderMetadata.m_ShaderResourceViews, merged.m_ShaderResourceViews, merged.m_ShaderResourceViewsNameCache);
        AppendUniqueMetadata(meshShaderMetadata.m_UnorderedAccessViews, merged.m_UnorderedAccessViews, merged.m_UnorderedAccessViewsNameCache);
        AppendUniqueMetadata(pixelShaderMetadata.m_UnorderedAccessViews, merged.m_UnorderedAccessViews, merged.m_UnorderedAccessViewsNameCache);
        AppendUniqueMetadata(meshShaderMetadata.m_Samplers, merged.m_Samplers, merged.m_SamplersNameCache);
        AppendUniqueMetadata(pixelShaderMetadata.m_Samplers, merged.m_Samplers, merged.m_SamplersNameCache);
        return merged;
    }
}

MeshShader::MeshShader(
    const ShaderBlob& meshShader,
    const ShaderBlob& pixelShader,
    const std::function<void(RasterPipelineStateBuilder&)> buildPipelineState)
{
    CollectShaderMetadata(meshShader.GetBlob(), &m_MeshShaderMetadata);
    CollectShaderMetadata(pixelShader.GetBlob(), &m_PixelShaderMetadata);
    BuildPipelineLayout();
    BuildReflectedRootSignature();

    m_PipelineStateBuilder
        .WithRootSignature(m_RootSignature)
        .WithMeshShaders(meshShader.GetBlob(), pixelShader.GetBlob());
    buildPipelineState(m_PipelineStateBuilder);
}

void MeshShader::SetConstantBuffer(CommandList& commandList, const std::string& variableName, const size_t size, const void* data)
{
    (void)commandList;
    m_DescriptorSet->SetConstantBufferData(variableName, data, size);
}

void MeshShader::SetShaderResourceView(CommandList& commandList, const std::string& variableName, const ShaderResourceView& shaderResourceView)
{
    (void)commandList;
    m_DescriptorSet->SetShaderResourceView(variableName, 0u, shaderResourceView);
}

void MeshShader::SetShaderResource(CommandList& commandList, const std::string& variableName, const Resource& resource)
{
    (void)commandList;
    m_DescriptorSet->SetShaderResource(variableName, 0u, resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}

void MeshShader::SetStructuredBuffer(CommandList& commandList, const std::string& variableName, const StructuredBuffer& buffer)
{
    (void)commandList;
    m_DescriptorSet->SetStructuredBuffer(variableName, buffer);
}

void MeshShader::SetTexture(CommandList& commandList, const std::string& variableName, const ShaderResourceView& shaderResourceView)
{
    SetShaderResourceView(commandList, variableName, shaderResourceView);
}

void MeshShader::SetTexture(CommandList& commandList, const std::string& variableName, const std::shared_ptr<Resource>& texture)
{
    SetTexture(commandList, variableName, ShaderResourceView(texture));
}

void MeshShader::SetUnorderedAccessView(CommandList& commandList, const std::string& variableName, const UnorderedAccessView& unorderedAccessView)
{
    (void)commandList;
    m_DescriptorSet->SetUnorderedAccessView(variableName, unorderedAccessView);
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> MeshShader::GetPipelineState(
    const Microsoft::WRL::ComPtr<ID3D12Device2>& device,
    const RenderTargetState& renderTargetState)
{
    const auto& formats = renderTargetState.GetFormats();
    std::vector<DXGI_FORMAT> renderTargetFormats(formats.GetCount());
    memcpy(renderTargetFormats.data(), formats.GetFormats(), sizeof(DXGI_FORMAT) * renderTargetFormats.size());
    m_PipelineStateBuilder.WithRenderTargetFormats(renderTargetFormats, formats.GetDepthStencilFormat());
    m_PipelineStateBuilder.WithSampleDesc(renderTargetState.GetSampleDesc());

    const RasterPipelineStateKey pipelineStateKey = m_PipelineStateBuilder.CreateKey(renderTargetState);
    return m_PipelineStateObjects.GetOrCreate(
        pipelineStateKey,
        [this, &device]()
        {
            return m_PipelineStateBuilder.Build(device);
        });
}

void MeshShader::CollectShaderMetadata(const Microsoft::WRL::ComPtr<ID3DBlob>& shader, ShaderReflectionMetadata* outMetadata)
{
    *outMetadata = ShaderReflection::CollectShader(shader);
}

void MeshShader::BuildPipelineLayout()
{
    PipelineLayoutReflectionOptions layoutOptions;
    layoutOptions.MaxDescriptorCount = 4096u;
    layoutOptions.ShaderStages = PipelineShaderStageFlags::Mesh | PipelineShaderStageFlags::Pixel;
    const ShaderReflectionMetadata mergedReflection = MergeMeshGraphicsReflection(m_MeshShaderMetadata, m_PixelShaderMetadata);
    m_PipelineLayout = std::make_unique<PipelineLayout>(
        PipelineLayout::CreateDescFromReflection(mergedReflection, layoutOptions));
    m_BindingSet = std::make_unique<PipelineBindingSet>(*m_PipelineLayout);
}

void MeshShader::BuildReflectedRootSignature()
{
    Assert(m_PipelineLayout != nullptr, "Pipeline layout must be built before creating a reflected root signature.");

    const ShaderReflectionMetadata mergedReflection = MergeMeshGraphicsReflection(m_MeshShaderMetadata, m_PixelShaderMetadata);
    for (const PipelineDescriptorRangeDesc& range : m_PipelineLayout->GetDesc().DescriptorRanges)
    {
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

    PipelineRootSignatureBuildDesc rootSignatureBuildDesc;
//Modify Begin:2026-07-30 by BestHui
    rootSignatureBuildDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
//Modify End
    m_RootSignature = m_PipelineLayout->CreateRootSignature(rootSignatureBuildDesc);
    m_PipelineLayout->SetRootSignature(m_RootSignature);
    m_DescriptorSet = m_DescriptorPool.AllocateDescriptorSet(*m_PipelineLayout);
}
//Modify End
