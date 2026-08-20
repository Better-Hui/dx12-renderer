//Modify Begin:2026-07-30 by Hui
#include "RayTracingShaderInternal.h"

#include <DX12Library/Helpers.h>
#include <Framework/Core/FrameworkDeviceContext.h>
#include <Framework/Rendering/Pipeline/DescriptorLayout.h>
#include <Framework/Rendering/Pipeline/ShaderBlob.h>

#if defined(min)
#undef min
#endif

#if defined(max)
#undef max
#endif

using namespace RayTracingShaderInternal;

namespace
{
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

RayTracingShader::Impl::Impl(
    FrameworkDeviceContext& deviceContext,
    const ShaderBlob& shaderLibrary,
    RayTracingPipelineDesc pipelineDesc)
    : DeviceContext(deviceContext)
    , Desc(std::move(pipelineDesc))
    , PipelineState(RayTracingPipelineStateBuilder(deviceContext, shaderLibrary, Desc).Build())
    , Layout(deviceContext)
    , DispatchTables(deviceContext)
{
    Assert(!Desc.Bindings.empty(), "Ray tracing shader requires at least one binding.");

    PipelineLayoutDesc layoutDesc;
    layoutDesc.RootSamplers = Desc.RootSamplers;
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
        default:
            break;
        }
    }
    Layout.SetRootSignature(PipelineState->GetGlobalRootSignaturePtr());
}

RayTracingShader::RayTracingShader(
    FrameworkDeviceContext& deviceContext,
    const ShaderBlob& shaderLibrary,
    RayTracingPipelineDesc desc)
    : m_Impl(std::make_unique<Impl>(deviceContext, shaderLibrary, std::move(desc)))
{}

RayTracingShader::~RayTracingShader() = default;
RayTracingShader::RayTracingShader(RayTracingShader&&) noexcept = default;
RayTracingShader& RayTracingShader::operator=(RayTracingShader&&) noexcept = default;

bool RayTracingShader::IsSupported(const FrameworkDeviceContext& deviceContext)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
    if (FAILED(deviceContext.GetDevice()->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5))))
    {
        return false;
    }

    return options5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
}

bool RayTracingShader::SupportsIndirectDispatch(const FrameworkDeviceContext& deviceContext)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
    if (FAILED(deviceContext.GetDevice()->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5))))
    {
        return false;
    }

    return options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1;
}

FrameworkDeviceContext& RayTracingShader::GetDeviceContext() const
{
    return m_Impl->DeviceContext;
}

const RayTracingPipelineDesc& RayTracingShader::GetDesc() const
{
    return m_Impl->Desc;
}

const RayTracingPipelineState& RayTracingShader::GetPipelineState() const
{
    return *m_Impl->PipelineState;
}

const PipelineLayout& RayTracingShader::GetPipelineLayout() const
{
    return m_Impl->Layout;
}

D3D12_DISPATCH_RAYS_DESC RayTracingShader::BuildIndirectDispatchArguments(
    const std::string_view passName,
    const uint32_t width,
    const uint32_t height,
    const uint32_t depth) const
{
    return BuildDispatchDesc(passName, width, height, depth);
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

RayTracingBindingSet RayTracingShader::CreateBindingSet() const
{
    return RayTracingBindingSet(*this);
}

bool RayTracingShader::HasBinding(std::string_view name) const
{
    return m_Impl->BindingIndicesByName.find(std::string(name)) != m_Impl->BindingIndicesByName.end();
}
//Modify End
