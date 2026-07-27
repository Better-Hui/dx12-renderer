//Modify Begin:2026-07-21 by BestHui
#include <Framework/RayTracingPipelineStateBuilder.h>

#include <DX12Library/Application.h>
#include <DX12Library/Helpers.h>
//Modify Begin:2026-07-27 by BestHui
#include <Framework/PipelineStateCache.h>
//Modify End
#include <Framework/ShaderBlob.h>

#include <d3dx12.h>

using Microsoft::WRL::ComPtr;

namespace
{
    bool IsDescriptorTableBinding(const RayTracingShaderBindingType type)
    {
        switch (type)
        {
        case RayTracingShaderBindingType::OutputTexture:
        case RayTracingShaderBindingType::TextureArray:
        case RayTracingShaderBindingType::VertexBufferArray:
        case RayTracingShaderBindingType::IndexBufferArray:
            return true;
        default:
            return false;
        }
    }

    D3D12_DESCRIPTOR_RANGE_TYPE GetDescriptorRangeType(const RayTracingShaderBindingType type)
    {
        return type == RayTracingShaderBindingType::OutputTexture ?
            D3D12_DESCRIPTOR_RANGE_TYPE_UAV :
            D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    }

    ComPtr<ID3D12Device5> GetDxrDevice()
    {
        ComPtr<ID3D12Device5> device5;
        ThrowIfFailed(Application::Get().GetDevice().As(&device5));
        return device5;
    }

    //Modify Begin:2026-07-27 by BestHui
    PipelineStateCache<RayTracingPipelineStateKey, std::shared_ptr<RayTracingPipelineState>> GPipelineStateCache;
    //Modify End
}

RootSignature& RayTracingPipelineState::GetGlobalRootSignature() const
{
    return *m_GlobalRootSignature;
}

const ComPtr<ID3D12StateObject>& RayTracingPipelineState::GetStateObject() const
{
    return m_StateObject;
}

const void* RayTracingPipelineState::GetShaderIdentifier(const std::wstring& shaderName) const
{
    return m_StateObjectProperties->GetShaderIdentifier(shaderName.c_str());
}

RayTracingPipelineStateBuilder::RayTracingPipelineStateBuilder(const ShaderBlob& shaderLibrary, RayTracingPipelineDesc desc)
    : m_ShaderLibrary(shaderLibrary)
    , m_Desc(std::move(desc))
{}

std::shared_ptr<RayTracingPipelineState> RayTracingPipelineStateBuilder::Build() const
{
    const RayTracingPipelineStateKey cacheKey = CreateKey();
    return GPipelineStateCache.GetOrCreate(
        cacheKey,
        [this]()
        {
            auto state = std::make_shared<RayTracingPipelineState>();
            state->m_GlobalRootSignature = BuildGlobalRootSignature();
            state->m_StateObject = BuildStateObject(state->m_GlobalRootSignature);
            ThrowIfFailed(state->m_StateObject.As(&state->m_StateObjectProperties));
            return state;
        });
}

//Modify Begin:2026-07-27 by BestHui
RayTracingPipelineStateKey RayTracingPipelineStateBuilder::CreateKey() const
{
    RayTracingPipelineStateKey key;
    key.ShaderLibrary = MakePipelineShaderBytecodeKey(m_ShaderLibrary.GetBlob());

    for (const std::wstring& exportName : m_Desc.Exports)
    {
        PipelineHashWideString(key.ExportsHash, exportName);
    }

    for (const RayTracingHitGroupDesc& hitGroup : m_Desc.HitGroups)
    {
        PipelineHashWideString(key.HitGroupsHash, hitGroup.Name);
        PipelineHashWideString(key.HitGroupsHash, hitGroup.ClosestHitShader);
        PipelineHashWideString(key.HitGroupsHash, hitGroup.AnyHitShader);
        PipelineHashWideString(key.HitGroupsHash, hitGroup.IntersectionShader);
        PipelineHashValue(key.HitGroupsHash, hitGroup.Type);
    }

    for (const RayTracingShaderBindingDesc& binding : m_Desc.Bindings)
    {
        PipelineHashString(key.LayoutHash, binding.Name);
        PipelineHashValue(key.LayoutHash, binding.Type);
        PipelineHashValue(key.LayoutHash, binding.ShaderRegister);
        PipelineHashValue(key.LayoutHash, binding.RegisterSpace);
        PipelineHashValue(key.LayoutHash, binding.DescriptorCount);
        PipelineHashValue(key.LayoutHash, binding.HasNullUnorderedAccessViewDesc);
        if (binding.HasNullUnorderedAccessViewDesc)
        {
            PipelineHashBytes(
                key.LayoutHash,
                &binding.NullUnorderedAccessViewDesc,
                sizeof(binding.NullUnorderedAccessViewDesc));
        }
    }

    key.PayloadSizeInBytes = m_Desc.PayloadSizeInBytes;
    key.AttributeSizeInBytes = m_Desc.AttributeSizeInBytes;
    key.MaxTraceRecursionDepth = m_Desc.MaxTraceRecursionDepth;
    key.DescriptorCapacity = m_Desc.MaxDescriptorCount;
    return key;
}
//Modify End

std::shared_ptr<RootSignature> RayTracingPipelineStateBuilder::BuildGlobalRootSignature() const
{
    using RootParameter = CD3DX12_ROOT_PARAMETER1;
    using DescriptorRange = CD3DX12_DESCRIPTOR_RANGE1;
    using StaticSampler = CD3DX12_STATIC_SAMPLER_DESC;

    std::vector<DescriptorRange> descriptorRanges;
    descriptorRanges.reserve(m_Desc.Bindings.size());

    std::vector<RootParameter> rootParameters;
    rootParameters.reserve(m_Desc.Bindings.size());

    for (const RayTracingShaderBindingDesc& binding : m_Desc.Bindings)
    {
        RootParameter rootParameter;
        if (IsDescriptorTableBinding(binding.Type))
        {
            descriptorRanges.emplace_back();
            descriptorRanges.back().Init(
                GetDescriptorRangeType(binding.Type),
                binding.DescriptorCount,
                binding.ShaderRegister,
                binding.RegisterSpace);
            rootParameter.InitAsDescriptorTable(1, &descriptorRanges.back());
        }
        else if (binding.Type == RayTracingShaderBindingType::ConstantBuffer)
        {
            rootParameter.InitAsConstantBufferView(binding.ShaderRegister, binding.RegisterSpace);
        }
        else
        {
            rootParameter.InitAsShaderResourceView(binding.ShaderRegister, binding.RegisterSpace);
        }

        rootParameters.push_back(rootParameter);
    }

    StaticSampler staticSampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);
    staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;

    D3D12_ROOT_SIGNATURE_DESC1 desc1 = {};
    desc1.NumParameters = static_cast<UINT>(rootParameters.size());
    desc1.pParameters = rootParameters.data();
    desc1.NumStaticSamplers = 1;
    desc1.pStaticSamplers = &staticSampler;
    desc1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    return std::make_shared<RootSignature>(desc1, D3D_ROOT_SIGNATURE_VERSION_1_1);
}

ComPtr<ID3D12StateObject> RayTracingPipelineStateBuilder::BuildStateObject(const std::shared_ptr<RootSignature>& globalRootSignature) const
{
    const auto device = GetDxrDevice();

    CD3DX12_STATE_OBJECT_DESC stateObjectDesc(D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);

    auto dxilLibrary = stateObjectDesc.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    const auto& blob = m_ShaderLibrary.GetBlob();
    D3D12_SHADER_BYTECODE libraryBytecode{ blob->GetBufferPointer(), blob->GetBufferSize() };
    dxilLibrary->SetDXILLibrary(&libraryBytecode);
    for (const std::wstring& exportName : m_Desc.Exports)
    {
        dxilLibrary->DefineExport(exportName.c_str());
    }

    for (const RayTracingHitGroupDesc& hitGroupDesc : m_Desc.HitGroups)
    {
        auto hitGroup = stateObjectDesc.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
        hitGroup->SetHitGroupExport(hitGroupDesc.Name.c_str());
        hitGroup->SetHitGroupType(hitGroupDesc.Type);
        if (!hitGroupDesc.ClosestHitShader.empty())
        {
            hitGroup->SetClosestHitShaderImport(hitGroupDesc.ClosestHitShader.c_str());
        }
        if (!hitGroupDesc.AnyHitShader.empty())
        {
            hitGroup->SetAnyHitShaderImport(hitGroupDesc.AnyHitShader.c_str());
        }
        if (!hitGroupDesc.IntersectionShader.empty())
        {
            hitGroup->SetIntersectionShaderImport(hitGroupDesc.IntersectionShader.c_str());
        }
    }

    auto shaderConfig = stateObjectDesc.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
    shaderConfig->Config(m_Desc.PayloadSizeInBytes, m_Desc.AttributeSizeInBytes);

    auto globalRootSignatureSubobject = stateObjectDesc.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
    globalRootSignatureSubobject->SetRootSignature(globalRootSignature->GetRootSignature().Get());

    auto pipelineConfig = stateObjectDesc.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
    pipelineConfig->Config(m_Desc.MaxTraceRecursionDepth);

    ComPtr<ID3D12StateObject> stateObject;
    ThrowIfFailed(device->CreateStateObject(stateObjectDesc, IID_PPV_ARGS(&stateObject)));
    return stateObject;
}

//Modify End
