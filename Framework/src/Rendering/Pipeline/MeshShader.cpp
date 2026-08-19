#include <Framework/Rendering/Pipeline/MeshShader.h>

#include <DX12Library/Helpers.h>
//Modify Begin:2026-07-30 by Hui
#include <Framework/Core/FrameworkDeviceContext.h>
//Modify End

#include <algorithm>
#include <cstring>

//Modify Begin:2026-07-31 by Hui
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
        const ShaderReflectionMetadata* amplificationShaderMetadata,
        const ShaderReflectionMetadata& meshShaderMetadata,
        const ShaderReflectionMetadata& pixelShaderMetadata)
    {
        ShaderReflectionMetadata merged;
        if (amplificationShaderMetadata != nullptr)
        {
            AppendUniqueMetadata(amplificationShaderMetadata->m_ConstantBuffers, merged.m_ConstantBuffers, merged.m_ConstantBuffersNameCache);
            AppendUniqueMetadata(amplificationShaderMetadata->m_ShaderResourceViews, merged.m_ShaderResourceViews, merged.m_ShaderResourceViewsNameCache);
            AppendUniqueMetadata(amplificationShaderMetadata->m_UnorderedAccessViews, merged.m_UnorderedAccessViews, merged.m_UnorderedAccessViewsNameCache);
            AppendUniqueMetadata(amplificationShaderMetadata->m_Samplers, merged.m_Samplers, merged.m_SamplersNameCache);
        }
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
    FrameworkDeviceContext& deviceContext,
    const ShaderBlob& meshShader,
    const ShaderBlob& pixelShader,
    PipelineLayoutReflectionOptions layoutOptions,
    const std::function<void(RasterPipelineStateBuilder&)> buildPipelineState)
    : m_DeviceContext(deviceContext)
    , m_PipelineLayoutOptions(std::move(layoutOptions))
    , m_DescriptorPool(deviceContext)
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

MeshShader::MeshShader(
    FrameworkDeviceContext& deviceContext,
    const ShaderBlob& amplificationShader,
    const ShaderBlob& meshShader,
    const ShaderBlob& pixelShader,
    PipelineLayoutReflectionOptions layoutOptions,
    const std::function<void(RasterPipelineStateBuilder&)> buildPipelineState)
    : m_DeviceContext(deviceContext)
    , m_PipelineLayoutOptions(std::move(layoutOptions))
    , m_DescriptorPool(deviceContext)
{
    CollectShaderMetadata(amplificationShader.GetBlob(), &m_AmplificationShaderMetadata);
    CollectShaderMetadata(meshShader.GetBlob(), &m_MeshShaderMetadata);
    CollectShaderMetadata(pixelShader.GetBlob(), &m_PixelShaderMetadata);
    BuildPipelineLayout();
    BuildReflectedRootSignature();

    m_PipelineStateBuilder
        .WithRootSignature(m_RootSignature)
        .WithMeshShaders(amplificationShader.GetBlob(), meshShader.GetBlob(), pixelShader.GetBlob());
    buildPipelineState(m_PipelineStateBuilder);
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
    if (m_PipelineLayoutOptions.MaxDescriptorCount == 1024u)
    {
        m_PipelineLayoutOptions.MaxDescriptorCount = 4096u;
    }
    if (m_PipelineLayoutOptions.ShaderStages == PipelineShaderStageFlags::All)
    {
        m_PipelineLayoutOptions.ShaderStages = PipelineShaderStageFlags::AllGraphics;
    }
    const ShaderReflectionMetadata* amplificationReflection = m_AmplificationShaderMetadata.m_ConstantBuffers.empty() &&
        m_AmplificationShaderMetadata.m_ShaderResourceViews.empty() &&
        m_AmplificationShaderMetadata.m_UnorderedAccessViews.empty() &&
        m_AmplificationShaderMetadata.m_Samplers.empty() ?
        nullptr :
        &m_AmplificationShaderMetadata;
    const ShaderReflectionMetadata mergedReflection = MergeMeshGraphicsReflection(amplificationReflection, m_MeshShaderMetadata, m_PixelShaderMetadata);
    m_PipelineLayout = std::make_unique<PipelineLayout>(
        m_DeviceContext,
        PipelineLayout::CreateDescFromReflection(mergedReflection, m_PipelineLayoutOptions));
    m_BindingSet = std::make_unique<PipelineBindingSet>(*m_PipelineLayout);
}

void MeshShader::BuildReflectedRootSignature()
{
    Assert(m_PipelineLayout != nullptr, "Pipeline layout must be built before creating a reflected root signature.");

    const ShaderReflectionMetadata* amplificationReflection = m_AmplificationShaderMetadata.m_ConstantBuffers.empty() &&
        m_AmplificationShaderMetadata.m_ShaderResourceViews.empty() &&
        m_AmplificationShaderMetadata.m_UnorderedAccessViews.empty() &&
        m_AmplificationShaderMetadata.m_Samplers.empty() ?
        nullptr :
        &m_AmplificationShaderMetadata;
    const ShaderReflectionMetadata mergedReflection = MergeMeshGraphicsReflection(amplificationReflection, m_MeshShaderMetadata, m_PixelShaderMetadata);
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
    rootSignatureBuildDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
    m_RootSignature = m_PipelineLayout->CreateRootSignature(rootSignatureBuildDesc);
    m_PipelineLayout->SetRootSignature(m_RootSignature);
    m_DescriptorSet = m_DescriptorPool.AllocateDescriptorSet(*m_PipelineLayout);
}
//Modify End
