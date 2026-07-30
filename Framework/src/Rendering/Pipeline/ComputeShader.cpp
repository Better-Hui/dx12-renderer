#include <Framework/Rendering/Pipeline/ComputeShader.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/ShaderUtils.h>
#include <DX12Library/Application.h>
#include <DX12Library/StructuredBuffer.h>
#include <Framework/Rendering/RayTracing/RayTracingAccelerationStructure.h>

#include <algorithm>

//Modify Begin:2026-07-23 by BestHui
ComputePipelineDescBuilder::ComputePipelineDescBuilder(ComputePipelineDesc desc)
    : m_Desc(std::move(desc))
{
}

ComputePipelineDescBuilder ComputePipelineDescBuilder::ReflectedDefault(const ShaderBlob&)
{
    return ComputePipelineDescBuilder(ComputePipelineDesc{});
}

ComputePipelineDescBuilder& ComputePipelineDescBuilder::WithDescriptorArrayCount(std::string name, UINT descriptorCount)
{
    const auto existingOverride = std::find_if(
        m_Desc.BindingOverrides.begin(),
        m_Desc.BindingOverrides.end(),
        [&name](const ComputePipelineDesc::BindingOverride& bindingOverride)
        {
            return bindingOverride.Name == name;
        });

    if (existingOverride != m_Desc.BindingOverrides.end())
    {
        existingOverride->DescriptorCount = descriptorCount;
        return *this;
    }

    m_Desc.BindingOverrides.push_back({ std::move(name), descriptorCount });
    return *this;
}

ComputePipelineDescBuilder& ComputePipelineDescBuilder::WithMaxDescriptorCount(UINT maxDescriptorCount)
{
    m_Desc.MaxDescriptorCount = maxDescriptorCount;
    return *this;
}

//Modify Begin:2026-07-30 by BestHui
ComputePipelineDescBuilder& ComputePipelineDescBuilder::WithRootSignatureFlags(D3D12_ROOT_SIGNATURE_FLAGS flags)
{
    m_Desc.RootSignatureFlags = flags;
    return *this;
}

ComputePipelineDescBuilder& ComputePipelineDescBuilder::WithDirectlyIndexedResourceHeap()
{
    m_Desc.RootSignatureFlags = static_cast<D3D12_ROOT_SIGNATURE_FLAGS>(
        m_Desc.RootSignatureFlags | D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);
    return *this;
}
//Modify End

ComputePipelineDesc ComputePipelineDescBuilder::Build() const
{
    return m_Desc;
}

ComputeShader::ComputeShader(const ShaderBlob& shader, ComputePipelineDesc desc)
    : m_Shader(shader.GetBlob())
    , m_PipelineLayout(std::make_unique<PipelineLayout>())
{
    CollectShaderMetadata(m_Shader, &m_ShaderMetadata);
    BuildReflectedRootSignature(desc);
    m_PipelineStateBuilder.WithRootSignature(m_RootSignature).WithShader(m_Shader);
}
//Modify End

void ComputeShader::StageDefaultDescriptorTables(CommandList& commandList) const
{
    if (m_PipelineLayout != nullptr)
    {
        m_PipelineLayout->StageDefaultDescriptorTables(commandList);
    }
}

bool ComputeShader::HasConstantBuffer(const std::string& variableName) const
{
    return m_BindingSet->HasBinding(variableName, DescriptorBindingKind::ConstantBuffer);
}

bool ComputeShader::HasShaderResourceView(const std::string& variableName) const
{
    return m_BindingSet->HasBinding(variableName, DescriptorBindingKind::ShaderResourceView) ||
        m_BindingSet->HasBinding(variableName, DescriptorBindingKind::AccelerationStructure);
}

bool ComputeShader::HasUnorderedAccessView(const std::string& variableName) const
{
    return m_BindingSet->HasBinding(variableName, DescriptorBindingKind::UnorderedAccessView);
}

void ComputeShader::SetConstantBuffer(CommandList& commandList, const std::string& variableName, size_t size, const void* data) const
{
    (void)commandList;
    m_DescriptorSet->SetConstantBufferData(variableName, data, size);
}

void ComputeShader::SetShaderResourceView(CommandList& commandList, const std::string& variableName, const ShaderResourceView& shaderResourceView) const
{
    SetShaderResourceView(commandList, variableName, 0u, shaderResourceView);
}

void ComputeShader::SetTexture(CommandList& commandList, const std::string& variableName, const ShaderResourceView& shaderResourceView) const
{
    SetShaderResourceView(commandList, variableName, shaderResourceView);
}

void ComputeShader::SetTexture(CommandList& commandList, const std::string& variableName, const std::shared_ptr<Resource>& texture) const
{
    SetTexture(commandList, variableName, ShaderResourceView(texture));
}

void ComputeShader::SetShaderResourceView(CommandList& commandList, const std::string& variableName, UINT arrayIndex, const ShaderResourceView& shaderResourceView) const
{
    (void)commandList;
    m_DescriptorSet->SetShaderResourceView(variableName, arrayIndex, shaderResourceView);
}

void ComputeShader::SetShaderResourceView(CommandList& commandList, const std::string& variableName, UINT arrayIndex, const Resource& resource, D3D12_RESOURCE_STATES stateAfter) const
{
    (void)commandList;
    m_DescriptorSet->SetShaderResource(variableName, arrayIndex, resource, stateAfter);
}

//Modify Begin:2026-07-30 by BestHui
void ComputeShader::SetShaderResourceViews(CommandList& commandList, const std::string& variableName, std::span<const ShaderResourceView> shaderResourceViews) const
{
    (void)commandList;
    m_DescriptorSet->SetShaderResourceViews(variableName, shaderResourceViews);
}

void ComputeShader::SetStructuredBuffer(CommandList& commandList, const std::string& variableName, const StructuredBuffer& buffer) const
{
    (void)commandList;
    m_DescriptorSet->SetStructuredBuffer(variableName, buffer);
}
//Modify End

void ComputeShader::SetUnorderedAccessView(CommandList& commandList, const std::string& variableName, const UnorderedAccessView& unorderedAccessView) const
{
    (void)commandList;
    m_DescriptorSet->SetUnorderedAccessView(variableName, unorderedAccessView);
}

void ComputeShader::SetAccelerationStructure(CommandList& commandList, const RayTracingAccelerationStructure& accelerationStructure) const
{
    const PipelineDescriptorRangeDesc& accelerationStructureBinding = m_BindingSet->GetFirstRange(DescriptorBindingKind::AccelerationStructure);
    (void)commandList;
    m_DescriptorSet->SetAccelerationStructure(accelerationStructureBinding.Name, accelerationStructure);
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> ComputeShader::GetPipelineState(const Microsoft::WRL::ComPtr<ID3D12Device2>& device) const
{
//Modify Begin:2026-07-27 by BestHui
    const ComputePipelineStateKey pipelineStateKey = m_PipelineStateBuilder.CreateKey();
    return m_PipelineStateCache.GetOrCreate(
        pipelineStateKey,
        [this, &device]()
        {
            return m_PipelineStateBuilder.Build(device);
        });
//Modify End
}

void ComputeShader::CollectShaderMetadata(const Microsoft::WRL::ComPtr<ID3DBlob>& shader, ShaderMetadata* outMetadata)
{
    *outMetadata = ShaderReflection::CollectShader(shader);
}

void ComputeShader::BuildReflectedRootSignature(const ComputePipelineDesc& desc)
{
    //Modify Begin:2026-07-24 by BestHui
    PipelineLayoutReflectionOptions layoutOptions;
    layoutOptions.MaxDescriptorCount = desc.MaxDescriptorCount;
    layoutOptions.AccelerationStructureFallbackName = "g_InlineRayTracingScene";
    layoutOptions.ShaderStages = PipelineShaderStageFlags::Compute;
    layoutOptions.BindingOverrides.reserve(desc.BindingOverrides.size());
    for (const ComputePipelineDesc::BindingOverride& bindingOverride : desc.BindingOverrides)
    {
        layoutOptions.BindingOverrides.push_back({ bindingOverride.Name, bindingOverride.DescriptorCount });
    }

    m_PipelineLayout->Reset(PipelineLayout::CreateDescFromReflection(m_ShaderMetadata, layoutOptions));
    m_BindingSet = std::make_unique<PipelineBindingSet>(*m_PipelineLayout);

    for (const PipelineDescriptorRangeDesc& range : m_PipelineLayout->GetDesc().DescriptorRanges)
    {
        if (range.Kind == DescriptorBindingKind::ShaderResourceView)
        {
            const auto srvFindResult = m_ShaderMetadata.m_ShaderResourceViewsNameCache.find(range.Name);
            Assert(srvFindResult != m_ShaderMetadata.m_ShaderResourceViewsNameCache.end(), "SRV reflection metadata was not found.");
            m_PipelineLayout->AddDefaultShaderResourceViewTable(
                range.RootParameterIndex,
                range.DescriptorCount,
                m_ShaderMetadata.m_ShaderResourceViews[srvFindResult->second]);
        }
        else if (range.Kind == DescriptorBindingKind::UnorderedAccessView)
        {
            const auto uavFindResult = m_ShaderMetadata.m_UnorderedAccessViewsNameCache.find(range.Name);
            Assert(uavFindResult != m_ShaderMetadata.m_UnorderedAccessViewsNameCache.end(), "UAV reflection metadata was not found.");
            m_PipelineLayout->AddDefaultUnorderedAccessViewTable(
                range.RootParameterIndex,
                range.DescriptorCount,
                m_ShaderMetadata.m_UnorderedAccessViews[uavFindResult->second]);
        }
    }
    //Modify End

    PipelineRootSignatureBuildDesc rootSignatureBuildDesc;
//Modify Begin:2026-07-30 by BestHui
    rootSignatureBuildDesc.Flags = desc.RootSignatureFlags;
//Modify End
    m_RootSignature = m_PipelineLayout->CreateRootSignature(rootSignatureBuildDesc);
    m_PipelineLayout->SetRootSignature(m_RootSignature);
//Modify Begin:2026-07-27 by BestHui
    m_DescriptorSet = m_DescriptorPool.AllocateDescriptorSet(*m_PipelineLayout);
//Modify End
}

const DescriptorBindingInfo& ComputeShader::GetReflectedBinding(const std::string& variableName, DescriptorBindingKind expectedKind) const
{
    return m_BindingSet->GetBinding(variableName, expectedKind);
}
//Modify End
