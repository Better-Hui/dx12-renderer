#include <Framework/Rendering/Pipeline/Shader.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/ShaderUtils.h>
#include <DX12Library/Application.h>

//Modify Begin:2026-07-24 by BestHui
#include <algorithm>

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

//Modify Begin:2026-07-27 by BestHui
        AppendUniqueMetadata(vertexShaderMetadata.m_Samplers, merged.m_Samplers, merged.m_SamplersNameCache);
        AppendUniqueMetadata(pixelShaderMetadata.m_Samplers, merged.m_Samplers, merged.m_SamplersNameCache);
//Modify End

        return merged;
    }
}
//Modify End

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

    m_PipelineStateBuilder
        .WithRootSignature(m_RootSignature)
        .WithShaders(vertexShader.GetBlob(), pixelShader.GetBlob());
    buildPipelineState(m_PipelineStateBuilder);
}
//Modify End

void Shader::StageDefaultDescriptorTables(CommandList& commandList) const
{
    if (m_PipelineLayout != nullptr)
    {
        m_PipelineLayout->StageDefaultDescriptorTables(commandList);
    }
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
    (void)commandList;
    m_DescriptorSet->SetConstantBufferData(variableName, data, size);
//Modify End
}

void Shader::SetShaderResourceView(CommandList& commandList, const std::string& variableName, const ShaderResourceView& shaderResourceView)
{
//Modify Begin:2026-07-24 by BestHui
    (void)commandList;
    m_DescriptorSet->SetShaderResourceView(variableName, 0u, shaderResourceView);
//Modify End
}

//Modify Begin:2026-07-30 by BestHui
void Shader::SetShaderResourceViews(CommandList& commandList, const std::string& variableName, std::span<const ShaderResourceView> shaderResourceViews)
{
    (void)commandList;
    m_DescriptorSet->SetShaderResourceViews(variableName, shaderResourceViews);
}
//Modify End

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
    (void)commandList;
    m_DescriptorSet->SetUnorderedAccessView(variableName, unorderedAccessView);
    //Modify End
}
//Modify End

Microsoft::WRL::ComPtr<ID3D12PipelineState> Shader::GetPipelineState(const Microsoft::WRL::ComPtr<ID3D12Device2>& device, const RenderTargetState& renderTargetState)
{
//Modify Begin:2026-07-27 by BestHui
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
//Modify End
}

void Shader::CollectShaderMetadata(const Microsoft::WRL::ComPtr<ID3DBlob>& shader, ShaderMetadata* outMetadata)
{
    *outMetadata = ShaderReflection::CollectShader(shader);
}

//Modify Begin:2026-07-24 by BestHui
void Shader::BuildPipelineLayout()
{
    PipelineLayoutReflectionOptions layoutOptions;
    layoutOptions.MaxDescriptorCount = 4096u;
//Modify Begin:2026-07-27 by BestHui
    layoutOptions.ShaderStages = PipelineShaderStageFlags::AllGraphics;
//Modify End
    const ShaderReflectionMetadata mergedReflection = MergeGraphicsReflection(m_VertexShaderMetadata, m_PixelShaderMetadata);
    m_PipelineLayout = std::make_unique<PipelineLayout>(
        PipelineLayout::CreateDescFromReflection(mergedReflection, layoutOptions));
    m_BindingSet = std::make_unique<PipelineBindingSet>(*m_PipelineLayout);
}

void Shader::BuildReflectedRootSignature()
{
    Assert(m_PipelineLayout != nullptr, "Pipeline layout must be built before creating a reflected root signature.");

    const ShaderReflectionMetadata mergedReflection = MergeGraphicsReflection(m_VertexShaderMetadata, m_PixelShaderMetadata);

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
    rootSignatureBuildDesc.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
//Modify End
    m_RootSignature = m_PipelineLayout->CreateRootSignature(rootSignatureBuildDesc);
//Modify Begin:2026-07-27 by BestHui
    m_PipelineLayout->SetRootSignature(m_RootSignature);
    m_DescriptorSet = m_DescriptorPool.AllocateDescriptorSet(*m_PipelineLayout);
//Modify End
}

const PipelineDescriptorRangeDesc* Shader::FindPipelineBinding(
    const std::string& variableName,
    const DescriptorBindingKind expectedKind) const
{
    return m_BindingSet != nullptr ? m_BindingSet->FindRange(variableName, expectedKind) : nullptr;
}
//Modify End
