//Modify Begin:2026-07-21 by BestHui
#include "RayTracingShaderInternal.h"

#include <DX12Library/Application.h>
#include <DX12Library/Helpers.h>
#include <Framework/DescriptorLayout.h>
#include <Framework/ShaderBlob.h>

#if defined(min)
#undef min
#endif

#if defined(max)
#undef max
#endif

//Modify Begin:2026-07-27 by BestHui
using namespace RayTracingShaderInternal;

RayTracingShader::Impl::Impl(const ShaderBlob& shaderLibrary, RayTracingPipelineDesc pipelineDesc)
    : Desc(std::move(pipelineDesc))
    , PipelineState(RayTracingPipelineStateBuilder(shaderLibrary, Desc).Build())
{
    Assert(!Desc.Bindings.empty(), "Ray tracing shader requires at least one binding.");

    PipelineLayoutDesc layoutDesc;
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
        layoutDesc.DescriptorRanges.push_back(std::move(range));
    }

    Layout.Reset(std::move(layoutDesc));
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
