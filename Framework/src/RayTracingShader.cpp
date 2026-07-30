//Modify Begin:2026-07-21 by BestHui
#include "RayTracingShaderInternal.h"

#include <DX12Library/Application.h>
#include <DX12Library/Helpers.h>
#include <Framework/DescriptorLayout.h>
#include <Framework/Mesh.h>
#include <Framework/ShaderBlob.h>

#if defined(min)
#undef min
#endif

#if defined(max)
#undef max
#endif

//Modify Begin:2026-07-27 by BestHui
using namespace RayTracingShaderInternal;

namespace
{
    D3D12_SHADER_RESOURCE_VIEW_DESC CreateNullRayTracingVertexBufferSrvDesc()
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
        desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.Buffer.NumElements = 1;
        desc.Buffer.StructureByteStride = sizeof(VertexAttributes);
        desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        return desc;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC CreateNullRayTracingIndexBufferSrvDesc()
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
        desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        desc.Format = DXGI_FORMAT_R32_UINT;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.Buffer.NumElements = 1;
        desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        return desc;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC CreateNullRayTracingTextureSrvDesc()
    {
        ShaderUtils::ShaderResourceViewMetadata metadata{};
        metadata.InputType = D3D_SIT_TEXTURE;
        metadata.Dimension = D3D_SRV_DIMENSION_TEXTURE2D;
        return DescriptorLayout::CreateNullShaderResourceViewDesc(metadata);
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC CreateNullRayTracingOutputUavDesc(const RayTracingShaderBindingDesc& binding)
    {
        if (binding.HasNullUnorderedAccessViewDesc)
        {
            return binding.NullUnorderedAccessViewDesc;
        }

        D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
        desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        return desc;
    }
}

RayTracingShader::Impl::Impl(const ShaderBlob& shaderLibrary, RayTracingPipelineDesc pipelineDesc)
    : Desc(std::move(pipelineDesc))
    , PipelineState(RayTracingPipelineStateBuilder(shaderLibrary, Desc).Build())
{
    Assert(!Desc.Bindings.empty(), "Ray tracing shader requires at least one binding.");

    PipelineLayoutDesc layoutDesc;
//Modify Begin:2026-07-30 by BestHui
    layoutDesc.RootSamplers = Desc.RootSamplers;
//Modify End
    layoutDesc.DescriptorRanges.reserve(Desc.Bindings.size());
    for (uint32_t i = 0; i < Desc.Bindings.size(); ++i)
    {
        const RayTracingShaderBindingDesc& binding = Desc.Bindings[i];
        Assert(!binding.Name.empty(), "Ray tracing binding name must not be empty.");
        BindingIndicesByName.emplace(binding.Name, i);
        BindingIndicesByName.emplace(DescriptorLayout::GetBaseResourceName(binding.Name), i);

        PipelineDescriptorRangeDesc range;
        range.Name = binding.Name;
        range.Kind = GetDescriptorBindingKind(binding.Type);
        range.ShaderRegister = binding.ShaderRegister;
        range.RegisterSpace = binding.RegisterSpace;
        range.DescriptorCount = std::max(1u, binding.DescriptorCount);
        range.RootParameterIndex = i;
        range.BindingMode = IsDescriptorTableBinding(binding.Type) ?
            PipelineDescriptorBindingMode::DescriptorTable :
            PipelineDescriptorBindingMode::RootDescriptor;
        range.ShaderStages = PipelineShaderStageFlags::RayTracing;
        layoutDesc.DescriptorRanges.push_back(std::move(range));
    }

    Layout.Reset(std::move(layoutDesc));
//Modify Begin:2026-07-27 by BestHui
    for (const RayTracingShaderBindingDesc& binding : Desc.Bindings)
    {
        if (!IsDescriptorTableBinding(binding.Type))
        {
            continue;
        }

        const uint32_t bindingIndex = BindingIndicesByName.at(binding.Name);
        switch (binding.Type)
        {
        case RayTracingShaderBindingType::OutputTexture:
            Layout.AddDefaultUnorderedAccessViewTable(
                bindingIndex,
                std::max(1u, binding.DescriptorCount),
                CreateNullRayTracingOutputUavDesc(binding));
            break;
        case RayTracingShaderBindingType::TextureArray:
            Layout.AddDefaultShaderResourceViewTable(
                bindingIndex,
                std::max(1u, binding.DescriptorCount),
                CreateNullRayTracingTextureSrvDesc());
            break;
        case RayTracingShaderBindingType::VertexBufferArray:
            Layout.AddDefaultShaderResourceViewTable(
                bindingIndex,
                std::max(1u, binding.DescriptorCount),
                CreateNullRayTracingVertexBufferSrvDesc());
            break;
        case RayTracingShaderBindingType::IndexBufferArray:
            Layout.AddDefaultShaderResourceViewTable(
                bindingIndex,
                std::max(1u, binding.DescriptorCount),
                CreateNullRayTracingIndexBufferSrvDesc());
            break;
        default:
            break;
        }
    }
//Modify End
    Layout.SetRootSignature(PipelineState->GetGlobalRootSignaturePtr());
}
//Modify End

RayTracingPipelineDesc RayTracingShader::CreateDefaultPipelineDesc()
{
    RayTracingPipelineDesc desc;
    desc.Exports = {
        DefaultRayGenerationShaderName,
        DefaultMissShaderName,
        DefaultClosestHitShaderName
    };
    desc.HitGroups = {
        {
            DefaultHitGroupName,
            DefaultClosestHitShaderName,
            L"",
            L"",
            D3D12_HIT_GROUP_TYPE_TRIANGLES
        }
    };
    desc.Bindings = {
        { "Output", RayTracingShaderBindingType::OutputTexture, 0, 0, 1 },
        { "Scene", RayTracingShaderBindingType::AccelerationStructure, 0, 0, 1 },
        { "CameraConstants", RayTracingShaderBindingType::ConstantBuffer, 0, 0, 1 },
        { "Materials", RayTracingShaderBindingType::StructuredBuffer, 1, 0, 1 },
        { "Geometries", RayTracingShaderBindingType::StructuredBuffer, 2, 0, 1 },
        { "VertexBuffers", RayTracingShaderBindingType::VertexBufferArray, 0, 1, desc.MaxDescriptorCount },
        { "IndexBuffers", RayTracingShaderBindingType::IndexBufferArray, 0, 2, desc.MaxDescriptorCount },
        { "Textures", RayTracingShaderBindingType::TextureArray, 0, 3, desc.MaxDescriptorCount }
    };
    desc.Passes = {
        {
            "RayGen",
            DefaultRayGenerationShaderName,
            { { DefaultMissShaderName, {} } },
            { { DefaultHitGroupName, {} } }
        }
    };
    return desc;
}

RayTracingShader::RayTracingShader(const ShaderBlob& shaderLibrary)
    : RayTracingShader(shaderLibrary, CreateDefaultPipelineDesc())
{}

RayTracingShader::RayTracingShader(const ShaderBlob& shaderLibrary, RayTracingPipelineDesc desc)
    : m_Impl(std::make_unique<Impl>(shaderLibrary, std::move(desc)))
{}

RayTracingShader::~RayTracingShader() = default;
RayTracingShader::RayTracingShader(RayTracingShader&&) noexcept = default;
RayTracingShader& RayTracingShader::operator=(RayTracingShader&&) noexcept = default;

bool RayTracingShader::IsSupported()
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
    if (FAILED(Application::Get().GetDevice()->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5))))
    {
        return false;
    }

    return options5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
}

const RayTracingPipelineDesc& RayTracingShader::GetDesc() const
{
    return m_Impl->Desc;
}

//Modify Begin:2026-07-27 by BestHui
const RayTracingPipelineState& RayTracingShader::GetPipelineState() const
{
    return *m_Impl->PipelineState;
}

const PipelineLayout& RayTracingShader::GetPipelineLayout() const
{
    return m_Impl->Layout;
}

void RayTracingShader::PrepareDispatch(const std::string_view passName) const
{
    const RayTracingShaderPassDesc& pass = m_Impl->DispatchTables.ResolvePass(m_Impl->Desc, passName);
    Assert(!pass.RayGenerationShader.empty(), "Ray tracing pass requires a ray generation shader.");
    m_Impl->DispatchTables.EnsureBuilt(*m_Impl->PipelineState, pass);
}

D3D12_DISPATCH_RAYS_DESC RayTracingShader::BuildDispatchDesc(
    const std::string_view passName,
    const uint32_t width,
    const uint32_t height,
    const uint32_t depth) const
{
    PrepareDispatch(passName);
    return BuildDispatchDesc(width, height, depth);
}

D3D12_DISPATCH_RAYS_DESC RayTracingShader::BuildDispatchDesc(
    const uint32_t width,
    const uint32_t height,
    const uint32_t depth) const
{
    return m_Impl->DispatchTables.BuildDispatchDesc(width, height, depth);
}
//Modify End

//Modify Begin:2026-07-24 by BestHui
RayTracingBindingSet RayTracingShader::CreateBindingSet() const
{
    return RayTracingBindingSet(*this);
}
//Modify End

bool RayTracingShader::HasBinding(std::string_view name) const
{
    return m_Impl->BindingIndicesByName.find(std::string(name)) != m_Impl->BindingIndicesByName.end();
}
//Modify End
