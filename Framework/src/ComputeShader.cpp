#include "ComputeShader.h"
#include <DX12Library/Helpers.h>
#include <DX12Library/ShaderUtils.h>
#include <DX12Library/Application.h>
#include <Framework/CommandContext.h>
#include <Framework/RayTracingAccelerationStructure.h>

#include <algorithm>

//Modify Begin:2026-07-27 by BestHui
FRAMEWORK_SUPPRESS_DEPRECATED_WARNINGS_BEGIN
//Modify End

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

ComputePipelineDesc ComputePipelineDescBuilder::Build() const
{
    return m_Desc;
}

ComputeShader::ComputeShader(
    const std::shared_ptr<CommonRootSignature>& rootSignature,
    const ShaderBlob& shader,
    bool collectMetadata)
    : m_CommonRootSignature(rootSignature)
    , m_RootSignature(rootSignature)
    , m_PipelineStateBuilder(rootSignature)
    , m_Shader(shader.GetBlob())
{
    m_PipelineStateBuilder.WithShader(m_Shader);
    if (collectMetadata)
    {
        CollectShaderMetadata(m_Shader, &m_ShaderMetadata);
    }
}

ComputeShader::ComputeShader(const ShaderBlob& shader, ComputePipelineDesc desc)
    : m_Shader(shader.GetBlob())
    , m_PipelineLayout(std::make_unique<PipelineLayout>())
    , m_UseReflectedRootSignature(true)
{
    CollectShaderMetadata(m_Shader, &m_ShaderMetadata);
    BuildReflectedRootSignature(desc);
    m_PipelineStateBuilder.WithRootSignature(m_RootSignature).WithShader(m_Shader);
}
//Modify End

void ComputeShader::Bind(CommandList& commandList) const
{
    //Modify Begin:2026-07-23 by BestHui
    CommandContext(commandList).BindPipeline(*this);
    //Modify End
}

//Modify Begin:2026-07-23 by BestHui
void ComputeShader::Unbind(CommandList& commandList) const
{
    if (m_CommonRootSignature)
    {
        m_CommonRootSignature->UnbindComputeShaderResourceViews(commandList);
    }
}

//Modify Begin:2026-07-27 by BestHui
void ComputeShader::ApplyBindings(CommandList& commandList) const
{
    if (m_UseReflectedRootSignature)
    {
        CommandContext(commandList).BindDescriptorSet(*m_DescriptorSet, PipelineBindPoint::Compute);
    }
}
//Modify End

void ComputeShader::SetMaterialConstantBuffer(CommandList& commandList, size_t size, const void* data) const
{
    if (m_UseReflectedRootSignature)
    {
        SetComputeConstantBuffer(commandList, size, data);
        return;
    }

    m_CommonRootSignature->SetComputeMaterialConstantBuffer(commandList, size, data);
}

void ComputeShader::SetComputeConstantBuffer(CommandList& commandList, size_t size, const void* data) const
{
    if (m_UseReflectedRootSignature)
    {
        const DescriptorBindingInfo& constantBufferBinding = m_BindingSet->GetFirstBinding(DescriptorBindingKind::ConstantBuffer);
        commandList.SetComputeDynamicConstantBuffer(constantBufferBinding.RootParameterIndex, size, data);
        return;
    }

    m_CommonRootSignature->SetComputeConstantBuffer(commandList, size, data);
}

bool ComputeShader::HasConstantBuffer(const std::string& variableName) const
{
    if (m_UseReflectedRootSignature)
    {
        return m_BindingSet->HasBinding(variableName, DescriptorBindingKind::ConstantBuffer);
    }

    return m_ShaderMetadata.m_ConstantBuffersNameCache.find(variableName) != m_ShaderMetadata.m_ConstantBuffersNameCache.end();
}

bool ComputeShader::HasShaderResourceView(const std::string& variableName) const
{
    if (m_UseReflectedRootSignature)
    {
        return m_BindingSet->HasBinding(variableName, DescriptorBindingKind::ShaderResourceView) ||
            m_BindingSet->HasBinding(variableName, DescriptorBindingKind::AccelerationStructure);
    }

    return m_ShaderMetadata.m_ShaderResourceViewsNameCache.find(variableName) != m_ShaderMetadata.m_ShaderResourceViewsNameCache.end();
}

bool ComputeShader::HasUnorderedAccessView(const std::string& variableName) const
{
    if (m_UseReflectedRootSignature)
    {
        return m_BindingSet->HasBinding(variableName, DescriptorBindingKind::UnorderedAccessView);
    }

    return m_ShaderMetadata.m_UnorderedAccessViewsNameCache.find(variableName) != m_ShaderMetadata.m_UnorderedAccessViewsNameCache.end();
}

void ComputeShader::SetConstantBuffer(CommandList& commandList, const std::string& variableName, size_t size, const void* data) const
{
    if (m_UseReflectedRootSignature)
    {
        (void)commandList;
        m_DescriptorSet->SetConstantBufferData(variableName, data, size);
        return;
    }

    const auto findResult = m_ShaderMetadata.m_ConstantBuffersNameCache.find(variableName);
    if (findResult == m_ShaderMetadata.m_ConstantBuffersNameCache.end())
    {
        throw std::exception("Shader variable not found.");
    }

    const ShaderUtils::ConstantBufferMetadata& cbufferMetadata = m_ShaderMetadata.m_ConstantBuffers[findResult->second];
    switch (cbufferMetadata.Space)
    {
    case CommonRootSignature::MATERIAL_REGISTER_SPACE:
        m_CommonRootSignature->SetComputeMaterialConstantBuffer(commandList, size, data);
        break;
    case CommonRootSignature::MODEL_REGISTER_SPACE:
        m_CommonRootSignature->SetComputeModelConstantBuffer(commandList, size, data);
        break;
    case CommonRootSignature::PIPELINE_REGISTER_SPACE:
        m_CommonRootSignature->SetComputePipelineConstantBuffer(commandList, size, data);
        break;
    default:
        throw std::exception("Invalid space index for a constant buffer.");
    }
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
    if (m_UseReflectedRootSignature)
    {
        (void)commandList;
        m_DescriptorSet->SetShaderResourceView(variableName, arrayIndex, shaderResourceView);
        return;
    }

    const auto findResult = m_ShaderMetadata.m_ShaderResourceViewsNameCache.find(variableName);
    if (findResult == m_ShaderMetadata.m_ShaderResourceViewsNameCache.end())
    {
        throw std::exception("Shader variable not found.");
    }

    const auto index = findResult->second;
    const auto& srvMetadata = m_ShaderMetadata.m_ShaderResourceViews[index];
    if (!DescriptorLayout::IsArrayIndexInBounds(srvMetadata.BindCount, arrayIndex))
    {
        throw std::exception("SRV array index is out of bounds.");
    }

    const UINT registerIndex = srvMetadata.RegisterIndex + arrayIndex;
    switch (srvMetadata.Space)
    {
    case CommonRootSignature::MATERIAL_REGISTER_SPACE:
        m_CommonRootSignature->SetComputeShaderResourceView(commandList, registerIndex, shaderResourceView);
        break;
    case CommonRootSignature::PIPELINE_REGISTER_SPACE:
        m_CommonRootSignature->SetPipelineShaderResourceView(commandList, registerIndex, shaderResourceView);
        break;
    default:
        throw std::exception("Invalid space index for an SRV.");
    }
}

void ComputeShader::SetShaderResourceView(CommandList& commandList, const std::string& variableName, UINT arrayIndex, const Resource& resource, D3D12_RESOURCE_STATES stateAfter) const
{
    if (m_UseReflectedRootSignature)
    {
        (void)commandList;
        m_DescriptorSet->SetShaderResource(variableName, arrayIndex, resource, stateAfter);
        return;
    }

    const auto findResult = m_ShaderMetadata.m_ShaderResourceViewsNameCache.find(variableName);
    if (findResult == m_ShaderMetadata.m_ShaderResourceViewsNameCache.end())
    {
        throw std::exception("Shader variable not found.");
    }

    const auto index = findResult->second;
    const auto& srvMetadata = m_ShaderMetadata.m_ShaderResourceViews[index];
    if (!DescriptorLayout::IsArrayIndexInBounds(srvMetadata.BindCount, arrayIndex))
    {
        throw std::exception("SRV array index is out of bounds.");
    }

    const UINT registerIndex = srvMetadata.RegisterIndex + arrayIndex;
    switch (srvMetadata.Space)
    {
    case CommonRootSignature::MATERIAL_REGISTER_SPACE:
        commandList.SetShaderResourceView(CommonRootSignature::RootParameters::MaterialSRVs, registerIndex, resource, stateAfter);
        break;
    case CommonRootSignature::PIPELINE_REGISTER_SPACE:
        commandList.SetShaderResourceView(CommonRootSignature::RootParameters::PipelineSRVs, registerIndex, resource, stateAfter);
        break;
    default:
        throw std::exception("Invalid space index for an SRV.");
    }
}

void ComputeShader::SetPipelineShaderResourceView(CommandList& commandList, UINT index, const ShaderResourceView& shaderResourceView) const
{
    Assert(m_CommonRootSignature != nullptr, "Pipeline SRV index binding requires the common root signature.");
    m_CommonRootSignature->SetPipelineShaderResourceView(commandList, index, shaderResourceView);
}

void ComputeShader::SetPipelineShaderResourceView(CommandList& commandList, UINT index, const Resource& resource, D3D12_RESOURCE_STATES stateAfter) const
{
    Assert(m_CommonRootSignature != nullptr, "Pipeline SRV index binding requires the common root signature.");
    commandList.SetShaderResourceView(CommonRootSignature::RootParameters::PipelineSRVs, index, resource, stateAfter);
}

void ComputeShader::SetComputeShaderResourceView(CommandList& commandList, UINT index, const ShaderResourceView& shaderResourceView) const
{
    Assert(m_CommonRootSignature != nullptr, "Compute SRV index binding requires the common root signature.");
    m_CommonRootSignature->SetComputeShaderResourceView(commandList, index, shaderResourceView);
}

void ComputeShader::SetUnorderedAccessView(CommandList& commandList, UINT index, const UnorderedAccessView& unorderedAccessView) const
{
    Assert(m_CommonRootSignature != nullptr, "UAV index binding requires the common root signature.");
    m_CommonRootSignature->SetUnorderedAccessView(commandList, index, unorderedAccessView);
}

void ComputeShader::SetUnorderedAccessView(CommandList& commandList, const std::string& variableName, const UnorderedAccessView& unorderedAccessView) const
{
    if (m_UseReflectedRootSignature)
    {
        (void)commandList;
        m_DescriptorSet->SetUnorderedAccessView(variableName, unorderedAccessView);
        return;
    }

    const auto findResult = m_ShaderMetadata.m_UnorderedAccessViewsNameCache.find(variableName);
    if (findResult == m_ShaderMetadata.m_UnorderedAccessViewsNameCache.end())
    {
        throw std::exception("Shader variable not found.");
    }

    const auto index = findResult->second;
    const auto& uavMetadata = m_ShaderMetadata.m_UnorderedAccessViews[index];
    if (uavMetadata.Space != CommonRootSignature::MATERIAL_REGISTER_SPACE)
    {
        throw std::exception("Invalid space index for a UAV.");
    }

    m_CommonRootSignature->SetUnorderedAccessView(commandList, uavMetadata.RegisterIndex, unorderedAccessView);
}

void ComputeShader::SetAccelerationStructure(CommandList& commandList, const RayTracingAccelerationStructure& accelerationStructure) const
{
    if (m_UseReflectedRootSignature)
    {
        const PipelineDescriptorRangeDesc& accelerationStructureBinding = m_BindingSet->GetFirstRange(DescriptorBindingKind::AccelerationStructure);
        (void)commandList;
        m_DescriptorSet->SetAccelerationStructure(accelerationStructureBinding.Name, accelerationStructure);
        return;
    }

    m_CommonRootSignature->SetComputeAccelerationStructure(commandList, accelerationStructure);
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
    using RootParameter = CD3DX12_ROOT_PARAMETER1;
    using DescriptorRange = CD3DX12_DESCRIPTOR_RANGE1;
    using StaticSampler = CD3DX12_STATIC_SAMPLER_DESC;

    //Modify Begin:2026-07-24 by BestHui
    PipelineLayoutReflectionOptions layoutOptions;
    layoutOptions.MaxDescriptorCount = desc.MaxDescriptorCount;
    layoutOptions.AccelerationStructureFallbackName = "g_InlineRayTracingScene";
    layoutOptions.BindingOverrides.reserve(desc.BindingOverrides.size());
    for (const ComputePipelineDesc::BindingOverride& bindingOverride : desc.BindingOverrides)
    {
        layoutOptions.BindingOverrides.push_back({ bindingOverride.Name, bindingOverride.DescriptorCount });
    }

    m_PipelineLayout->Reset(PipelineLayout::CreateDescFromReflection(m_ShaderMetadata, layoutOptions));
    m_BindingSet = std::make_unique<PipelineBindingSet>(*m_PipelineLayout);
    m_DescriptorSet = std::make_unique<PipelineDescriptorSet>(*m_PipelineLayout);

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
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    m_RootSignature = std::make_shared<RootSignature>(rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1_1);
}

const DescriptorBindingInfo& ComputeShader::GetReflectedBinding(const std::string& variableName, DescriptorBindingKind expectedKind) const
{
    return m_BindingSet->GetBinding(variableName, expectedKind);
}
//Modify End

//Modify Begin:2026-07-27 by BestHui
FRAMEWORK_SUPPRESS_DEPRECATED_WARNINGS_END
//Modify End
