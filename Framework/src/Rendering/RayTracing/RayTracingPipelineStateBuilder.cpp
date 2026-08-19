//Modify Begin:2026-07-30 by Hui
#include <Framework/Rendering/RayTracing/RayTracingPipelineStateBuilder.h>

#include <DX12Library/Helpers.h>
#include <Framework/Core/FrameworkDeviceContext.h>
#include <Framework/Rendering/Pipeline/PipelineLayout.h>
#include <Framework/Rendering/Pipeline/PipelineStateCache.h>
#include <Framework/Rendering/Pipeline/ShaderBlob.h>

#include <d3dx12/d3dx12.h>

using Microsoft::WRL::ComPtr;

#if defined(min)
#undef min
#endif

#if defined(max)
#undef max
#endif

namespace
{
    bool IsDescriptorTableBinding(const RayTracingShaderBindingType type)
    {
        switch (type)
        {
        case RayTracingShaderBindingType::OutputTexture:
        case RayTracingShaderBindingType::TextureArray:
            return true;
        default:
            return false;
        }
    }

    DescriptorBindingKind GetDescriptorBindingKind(const RayTracingShaderBindingType type)
    {
        switch (type)
        {
        case RayTracingShaderBindingType::OutputTexture:
            return DescriptorBindingKind::UnorderedAccessView;
        case RayTracingShaderBindingType::AccelerationStructure:
            return DescriptorBindingKind::AccelerationStructure;
        case RayTracingShaderBindingType::ConstantBuffer:
            return DescriptorBindingKind::ConstantBuffer;
        default:
            return DescriptorBindingKind::ShaderResourceView;
        }
    }

    ComPtr<ID3D12Device5> GetDxrDevice(const FrameworkDeviceContext& deviceContext)
    {
        ComPtr<ID3D12Device5> device5;
        ThrowIfFailed(deviceContext.GetDevice().As(&device5));
        return device5;
    }

    PipelineStateCache<RayTracingPipelineStateKey, std::shared_ptr<RayTracingPipelineState>> GPipelineStateCache;
}

RootSignature& RayTracingPipelineState::GetGlobalRootSignature() const
{
    return *m_GlobalRootSignature;
}

const std::shared_ptr<RootSignature>& RayTracingPipelineState::GetGlobalRootSignaturePtr() const
{
    return m_GlobalRootSignature;
}

const ComPtr<ID3D12StateObject>& RayTracingPipelineState::GetStateObject() const
{
    return m_StateObject;
}

const void* RayTracingPipelineState::GetShaderIdentifier(const std::wstring& shaderName) const
{
    return m_StateObjectProperties->GetShaderIdentifier(shaderName.c_str());
}

RayTracingPipelineStateBuilder::RayTracingPipelineStateBuilder(
    FrameworkDeviceContext& deviceContext,
    const ShaderBlob& shaderLibrary,
    RayTracingPipelineDesc desc)
    : m_ShaderLibrary(shaderLibrary)
    , m_DeviceContext(deviceContext)
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
    for (const PipelineRootSamplerDesc& sampler : m_Desc.RootSamplers)
    {
        PipelineHashString(key.LayoutHash, sampler.Name);
        PipelineHashValue(key.LayoutHash, sampler.ShaderRegister);
        PipelineHashValue(key.LayoutHash, sampler.RegisterSpace);
        PipelineHashValue(key.LayoutHash, sampler.ShaderStages);
        PipelineHashBytes(key.LayoutHash, &sampler.Desc, sizeof(sampler.Desc));
    }

    key.PayloadSizeInBytes = m_Desc.PayloadSizeInBytes;
    key.AttributeSizeInBytes = m_Desc.AttributeSizeInBytes;
    key.MaxTraceRecursionDepth = m_Desc.MaxTraceRecursionDepth;
    key.DescriptorCapacity = m_Desc.MaxDescriptorCount;
    return key;
}

std::shared_ptr<RootSignature> RayTracingPipelineStateBuilder::BuildGlobalRootSignature() const
{
    PipelineLayoutDesc layoutDesc;
    layoutDesc.ShaderStages = PipelineShaderStageFlags::RayTracing;
    layoutDesc.RootSamplers = m_Desc.RootSamplers;
    layoutDesc.DescriptorRanges.reserve(m_Desc.Bindings.size());
    for (uint32_t bindingIndex = 0; bindingIndex < m_Desc.Bindings.size(); ++bindingIndex)
    {
        const RayTracingShaderBindingDesc& binding = m_Desc.Bindings[bindingIndex];
        PipelineDescriptorRangeDesc range;
        range.Name = binding.Name;
        range.Kind = GetDescriptorBindingKind(binding.Type);
        range.ShaderRegister = binding.ShaderRegister;
        range.RegisterSpace = binding.RegisterSpace;
        range.DescriptorCount = std::max(1u, binding.DescriptorCount);
        range.RootParameterIndex = bindingIndex;
        range.BindingMode = IsDescriptorTableBinding(binding.Type) ?
            PipelineDescriptorBindingMode::DescriptorTable :
            PipelineDescriptorBindingMode::RootDescriptor;
        range.ShaderStages = PipelineShaderStageFlags::RayTracing;
        layoutDesc.DescriptorRanges.push_back(std::move(range));
    }

    PipelineLayout layout(m_DeviceContext, std::move(layoutDesc));

    PipelineRootSignatureBuildDesc rootSignatureBuildDesc;
    rootSignatureBuildDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
    return layout.CreateRootSignature(rootSignatureBuildDesc);
}

ComPtr<ID3D12StateObject> RayTracingPipelineStateBuilder::BuildStateObject(const std::shared_ptr<RootSignature>& globalRootSignature) const
{
    const auto device = GetDxrDevice(m_DeviceContext);

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
