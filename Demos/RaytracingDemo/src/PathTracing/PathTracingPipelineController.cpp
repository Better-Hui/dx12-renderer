//Modify Begin:2026-08-19 by Hui
#include <PathTracing/PathTracingPipelineController.h>

#include <DX12Library/CommandList.h>
#include <DX12Library/CommandQueue.h>
#include <DX12Library/Helpers.h>
#include <Framework/Core/FrameworkDeviceContext.h>
#include <Framework/Rendering/Pipeline/CommandContext.h>
#include <Framework/Rendering/RayTracing/RayTracingAccelerationStructure.h>
#include <Framework/Rendering/Pipeline/ShaderBlob.h>
#include <Framework/Rendering/Pipeline/ShaderTargetProfile.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>
#include <Scene/SceneLightManager.h>
#include <Scene/SceneResources.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>

namespace
{
    std::wstring GetEnvironmentProjectionShaderSuffix(const EnvironmentTextureProjection projection)
    {
        switch (projection)
        {
        case EnvironmentTextureProjection::Cubemap:
            return L"";
        case EnvironmentTextureProjection::Equirectangular:
            return L".equirect";
        case EnvironmentTextureProjection::CubemapHorizontalStrip:
            return L".cubestrip";
        default:
            throw std::invalid_argument("Unsupported environment texture projection.");
        }
    }

    void AddEnvironmentProjectionDefine(
        std::vector<ShaderVariantDefine>& defines,
        const EnvironmentTextureProjection projection)
    {
        if (projection != EnvironmentTextureProjection::Cubemap)
        {
            defines.push_back({
                "RAYTRACING_DEMO_ENVIRONMENT_PROJECTION",
            std::to_string(static_cast<uint32_t>(projection))
        });
    }
    }

    void AddMaxPathBouncesDefine(
        std::vector<ShaderVariantDefine>& defines,
        const uint32_t maxPathBounces)
    {
        defines.push_back({ "RAYTRACING_DEMO_MAX_BOUNCES", std::to_string(maxPathBounces) });
    }
    void AddMaterialShadingModelDefine(
        std::vector<ShaderVariantDefine>& defines,
        const MaterialShadingModel shadingModel)
    {
        defines.push_back({
            "FRAMEWORK_MATERIAL_SHADING_MODEL",
            std::to_string(static_cast<uint32_t>(shadingModel))
        });
    }

    std::wstring GetMaterialShadingModelShaderSuffix(const MaterialShadingModel shadingModel)
    {
        switch (shadingModel)
        {
        case MaterialShadingModel::Pbr:
            return L".pbr";
        case MaterialShadingModel::StylizedComic:
            return L".stylizedcomic";
        default:
            throw std::invalid_argument("Unsupported material shading model.");
        }
    }
}

PathTracingPipelineController::PathTracingPipelineController(FrameworkDeviceContext& deviceContext)
    : m_DeviceContext(deviceContext)
{
}

void PathTracingPipelineController::CreateComputeIndirectDispatchResources()
{
    if (m_ComputeIndirectCommandSignature != nullptr)
    {
        return;
    }

    constexpr std::array computeArguments = {
        IndirectArgumentDesc{ .Type = IndirectArgumentType::Dispatch },
    };
    m_ComputeIndirectCommandSignature = std::make_unique<IndirectCommandSignature>(
        m_DeviceContext,
        IndirectCommandSignatureDesc{
            .Arguments = computeArguments,
            .ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS),
        });
    m_ComputeIndirectArguments = std::make_unique<IndirectCommandBuffer>(
        m_DeviceContext,
        sizeof(D3D12_DISPATCH_ARGUMENTS),
        L"Path Tracing Compute Indirect Arguments");
}

void PathTracingPipelineController::EnsureRayTracingIndirectDispatchResources()
{
    if (m_RayTracingIndirectCommandSignature != nullptr)
    {
        return;
    }

    Assert(
        RayTracingShader::SupportsIndirectDispatch(m_DeviceContext),
        "DispatchRaysIndirect requires DirectX Raytracing Tier 1.1 support.");
    static_assert(offsetof(D3D12_DISPATCH_RAYS_DESC, Width) == 88u);
    static_assert(offsetof(D3D12_DISPATCH_RAYS_DESC, Height) == 92u);
    static_assert(offsetof(D3D12_DISPATCH_RAYS_DESC, Depth) == 96u);

    constexpr std::array rayTracingArguments = {
        IndirectArgumentDesc{ .Type = IndirectArgumentType::DispatchRays },
    };
    m_RayTracingIndirectCommandSignature = std::make_unique<IndirectCommandSignature>(
        m_DeviceContext,
        IndirectCommandSignatureDesc{
            .Arguments = rayTracingArguments,
            .ByteStride = sizeof(D3D12_DISPATCH_RAYS_DESC),
        });
    m_DirectRayTracingIndirectArguments = std::make_unique<IndirectCommandBuffer>(
        m_DeviceContext,
        sizeof(D3D12_DISPATCH_RAYS_DESC),
        L"Path Tracing Direct Ray Tracing Indirect Arguments");
    m_IndirectRayTracingIndirectArguments = std::make_unique<IndirectCommandBuffer>(
        m_DeviceContext,
        sizeof(D3D12_DISPATCH_RAYS_DESC),
        L"Path Tracing Indirect Ray Tracing Indirect Arguments");
}

void PathTracingPipelineController::PrepareDxrCompactedDispatchTemplate(
    CommandList& commandList,
    const uint32_t width,
    const uint32_t height,
    const bool directLighting)
{
    Assert(m_DispatchMode == PathTracingDispatchMode::CompactedIndirect, "DXR compact-dispatch template requires compacted indirect mode.");
    Assert(m_Backend == PathTracingBackend::ShaderTableDxr, "DXR compact-dispatch template requires the shader-table backend.");
    Assert(m_RayTracingShader != nullptr, "DXR indirect dispatch requires a ray tracing pipeline.");
    const D3D12_DISPATCH_RAYS_DESC dispatchArguments = m_RayTracingShader->BuildIndirectDispatchArguments(
        directLighting ? "DirectLightingRayGen" : "IndirectLightingRayGen",
        width,
        height,
        1u);
    IndirectCommandBuffer* argumentBuffer = directLighting
        ? m_DirectRayTracingIndirectArguments.get()
        : m_IndirectRayTracingIndirectArguments.get();
    Assert(argumentBuffer != nullptr, "DXR compact-dispatch argument buffer is not initialized.");
    argumentBuffer->Upload(commandList, std::as_bytes(std::span{ &dispatchArguments, 1u }));
}

PathTracingIndirectDispatch PathTracingPipelineController::GetCompactedIndirectDispatch(
    const PathTracingBackend backend,
    const bool directLighting) const
{
    Assert(m_DispatchMode == PathTracingDispatchMode::CompactedIndirect, "Compacted indirect dispatch was requested for the full-resolution pipeline.");
    Assert(backend == m_Backend, "Compacted indirect dispatch backend does not match the active path-tracing pipeline.");
    if (backend == PathTracingBackend::InlineRayQuery)
    {
        Assert(
            m_ComputeIndirectCommandSignature != nullptr && m_ComputeIndirectArguments != nullptr,
            "Compute indirect dispatch resources are not initialized.");
        return { *m_ComputeIndirectCommandSignature, m_ComputeIndirectArguments->GetResource() };
    }

    Assert(m_RayTracingIndirectCommandSignature != nullptr, "DXR indirect command signature is not initialized.");
    IndirectCommandBuffer* arguments = directLighting
        ? m_DirectRayTracingIndirectArguments.get()
        : m_IndirectRayTracingIndirectArguments.get();
    Assert(arguments != nullptr, "DXR indirect dispatch arguments are not initialized.");
    return { *m_RayTracingIndirectCommandSignature, arguments->GetResource() };
}

std::shared_ptr<ShaderBlob> PathTracingPipelineController::LoadShader(
    std::wstring compiledFileName,
    std::wstring sourceFileName,
    std::string targetProfile,
    std::vector<ShaderVariantDefine> defines,
    std::string entryPoint)
{
    ShaderVariantDesc desc;
    desc.CompiledFileName = std::move(compiledFileName);
    desc.SourceFileName = std::move(sourceFileName);
    desc.EntryPoint = std::move(entryPoint);
    desc.TargetProfile = std::move(targetProfile);
    desc.Defines = std::move(defines);
    desc.DebugName = "RaytracingDemo";
    return m_ShaderVariants.GetOrCompile(desc);
}

void PathTracingPipelineController::RetireCurrentPipelines()
{
    if (m_RayTracingShader == nullptr &&
        m_DirectRayTracingBindingSet == nullptr &&
        m_IndirectRayTracingBindingSet == nullptr &&
        m_InlineDirectLightingShader == nullptr &&
        m_InlineIndirectLightingShader == nullptr &&
        m_InlineCompactedDispatchFinalizeShader == nullptr &&
        m_DxrCompactedDispatchFinalizeShader == nullptr &&
        m_LightingCompositeShaders.empty())
    {
        return;
    }

    RetiredPipelines retired;
    const std::shared_ptr<CommandQueue> directQueue =
        m_DeviceContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
    const std::shared_ptr<CommandQueue> computeQueue =
        m_DeviceContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE);
    retired.DirectFenceValue = directQueue->Signal();
    retired.ComputeFenceValue = computeQueue->Signal();
    retired.RayTracingShader = std::move(m_RayTracingShader);
    retired.DirectRayTracingBindingSet = std::move(m_DirectRayTracingBindingSet);
    retired.IndirectRayTracingBindingSet = std::move(m_IndirectRayTracingBindingSet);
    retired.InlineDirectLightingShader = std::move(m_InlineDirectLightingShader);
    retired.InlineIndirectLightingShader = std::move(m_InlineIndirectLightingShader);
    retired.InlineCompactedDispatchFinalizeShader = std::move(m_InlineCompactedDispatchFinalizeShader);
    retired.DxrCompactedDispatchFinalizeShader = std::move(m_DxrCompactedDispatchFinalizeShader);
    retired.LightingCompositeShaders = std::move(m_LightingCompositeShaders);
    m_RetiredPipelines.push_back(std::move(retired));
}

void PathTracingPipelineController::ReleaseExpiredRetiredPipelines()
{
    const std::shared_ptr<CommandQueue> directQueue =
        m_DeviceContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
    const std::shared_ptr<CommandQueue> computeQueue =
        m_DeviceContext.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE);
    while (!m_RetiredPipelines.empty())
    {
        const RetiredPipelines& retired = m_RetiredPipelines.front();
        if (!directQueue->IsFenceComplete(retired.DirectFenceValue) ||
            !computeQueue->IsFenceComplete(retired.ComputeFenceValue))
        {
            break;
        }
        m_RetiredPipelines.pop_front();
    }
}

void PathTracingPipelineController::Reset()
{
    RetireCurrentPipelines();
    m_RetiredPipelines.clear();
    m_LightingCompositeShaders.clear();
    m_DxrCompactedDispatchFinalizeShader.reset();
    m_InlineCompactedDispatchFinalizeShader.reset();
    m_InlineIndirectLightingShader.reset();
    m_InlineDirectLightingShader.reset();
    m_IndirectRayTracingBindingSet.reset();
    m_DirectRayTracingBindingSet.reset();
    m_RayTracingShader.reset();
    m_ShaderVariants.Clear();
    m_ShadowMode = PathTracingShadowMode::HardShadows;
    m_DispatchMode = PathTracingDispatchMode::FullResolution;
    m_MaterialShadingModel = MaterialShadingModel::Pbr;
    m_MaxPathBounces = 3u;
    m_Layout = {};
}

void PathTracingPipelineController::EnsurePipelines(
    const PathTracingBackend backend,
    const PathTracingShadowMode shadowMode,
    const MaterialShadingModel shadingModel,
    const RayTracingSceneResourceLayout& layout,
    const uint32_t maxPathBounces,
    const PathTracingDispatchMode dispatchMode)
{
    ReleaseExpiredRetiredPipelines();
    const uint32_t clampedMaxPathBounces = std::clamp(maxPathBounces, 1u, 5u);
    const bool layoutChanged = m_Layout != layout;
    const bool backendChanged = m_Backend != backend;
    const bool shadowModeChanged = m_ShadowMode != shadowMode;
    const bool shadingModelChanged = m_MaterialShadingModel != shadingModel;
    const bool maxPathBouncesChanged = m_MaxPathBounces != clampedMaxPathBounces;
    const bool dispatchModeChanged = m_DispatchMode != dispatchMode;
    const bool needsDxrPipeline = backend == PathTracingBackend::ShaderTableDxr;
    const bool needsCompactedDispatch = dispatchMode == PathTracingDispatchMode::CompactedIndirect;

    if (needsCompactedDispatch)
    {
        CreateComputeIndirectDispatchResources();
        if (needsDxrPipeline)
        {
            EnsureRayTracingIndirectDispatchResources();
        }
    }

    if (!layoutChanged &&
        !backendChanged &&
        !shadowModeChanged &&
        !shadingModelChanged &&
        !maxPathBouncesChanged &&
        !dispatchModeChanged &&
        m_InlineDirectLightingShader != nullptr &&
        m_InlineIndirectLightingShader != nullptr &&
        (!needsCompactedDispatch ||
            (m_InlineCompactedDispatchFinalizeShader != nullptr &&
                (!needsDxrPipeline || m_DxrCompactedDispatchFinalizeShader != nullptr))) &&
        (!needsDxrPipeline ||
            (m_RayTracingShader != nullptr &&
                m_DirectRayTracingBindingSet != nullptr &&
                m_IndirectRayTracingBindingSet != nullptr)))
    {
        return;
    }

    m_Backend = backend;
    m_ShadowMode = shadowMode;
    m_MaterialShadingModel = shadingModel;
    m_MaxPathBounces = clampedMaxPathBounces;
    m_DispatchMode = dispatchMode;
    m_Layout = layout;

    if (layoutChanged || backendChanged
        || shadowModeChanged || shadingModelChanged || maxPathBouncesChanged || dispatchModeChanged
        )
    {
        RetireCurrentPipelines();
    }

    if (needsDxrPipeline)
    {
        CreateDxrPipeline(layout);
    }
    else
    {
        m_IndirectRayTracingBindingSet.reset();
        m_DirectRayTracingBindingSet.reset();
        m_RayTracingShader.reset();
    }

    CreateInlinePipelines(layout);
    if (needsCompactedDispatch)
    {
        CreateCompactedDispatchPipelines();
    }
}

void PathTracingPipelineController::CreateDxrPipeline(const RayTracingSceneResourceLayout& layout)
{
    const bool useSoftShadows = m_ShadowMode == PathTracingShadowMode::SoftShadows;
    std::wstring shaderFileName = L"PathTracing" + GetEnvironmentProjectionShaderSuffix(layout.EnvironmentProjection);
    if (useSoftShadows)
    {
        shaderFileName += L".softshadow";
    }
    shaderFileName += L".bounces" + std::to_wstring(m_MaxPathBounces);
    shaderFileName += GetMaterialShadingModelShaderSuffix(m_MaterialShadingModel);
    if (m_DispatchMode == PathTracingDispatchMode::CompactedIndirect)
    {
        shaderFileName += L".compact";
    }
    shaderFileName += L".rt.cso";
    std::vector<ShaderVariantDefine> defines;
    AddEnvironmentProjectionDefine(defines, layout.EnvironmentProjection);
    if (useSoftShadows)
    {
        defines.push_back({ "RAYTRACING_DEMO_SOFT_SHADOWS", "1" });
    }
    AddMaxPathBouncesDefine(defines, m_MaxPathBounces);
    AddMaterialShadingModelDefine(defines, m_MaterialShadingModel);
    if (m_DispatchMode == PathTracingDispatchMode::CompactedIndirect)
    {
        defines.push_back({ "RAYTRACING_DEMO_COMPACTED_DISPATCH", "1" });
    }
    constexpr const char* targetProfile = ShaderTargetProfile::RayTracingLibrary();
    const std::shared_ptr<ShaderBlob> pathTracingShader = LoadShader(
        shaderFileName,
        L"Demos/RaytracingDemo/shaders/PathTracing/PathTracing.rt.hlsl",
        targetProfile,
        defines,
        "");
    const RayTracingPipelineDesc rayTracingDesc = RayTracingPipelineDescBuilder::ReflectedDefault(*pathTracingShader)
        .WithStaticSamplerContract(PipelineStaticSamplers::LinearWrap(0u))
        .WithTriangleHitGroup(L"HitGroup", L"ClosestHit")
        .WithTriangleHitGroup(L"VisibilityHitGroup", L"VisibilityClosestHit")
        .WithRayGenerationPass("DirectLightingRayGen", L"DirectLightingRayGen", { L"Miss" }, { L"HitGroup", L"VisibilityHitGroup" })
        .WithRayGenerationPass("IndirectLightingRayGen", L"IndirectLightingRayGen", { L"Miss" }, { L"HitGroup", L"VisibilityHitGroup" })
        .WithPayloadSize(80)
        .Build();
    m_RayTracingShader = std::make_unique<RayTracingShader>(m_DeviceContext, *pathTracingShader, rayTracingDesc);
    m_DirectRayTracingBindingSet = std::make_unique<RayTracingBindingSet>(m_RayTracingShader->CreateBindingSet());
    m_IndirectRayTracingBindingSet = std::make_unique<RayTracingBindingSet>(m_RayTracingShader->CreateBindingSet());
}

void PathTracingPipelineController::CreateInlinePipelines(const RayTracingSceneResourceLayout& layout)
{
    const bool useSoftShadows = m_ShadowMode == PathTracingShadowMode::SoftShadows;
    std::vector<ShaderVariantDefine> directShaderDefines;
    AddEnvironmentProjectionDefine(directShaderDefines, layout.EnvironmentProjection);
    if (useSoftShadows)
    {
        directShaderDefines.push_back({ "RAYTRACING_DEMO_SOFT_SHADOWS", "1" });
    }
    std::vector<ShaderVariantDefine> indirectShaderDefines = directShaderDefines;
    AddMaxPathBouncesDefine(indirectShaderDefines, m_MaxPathBounces);
    AddMaterialShadingModelDefine(directShaderDefines, m_MaterialShadingModel);
    AddMaterialShadingModelDefine(indirectShaderDefines, m_MaterialShadingModel);
    if (m_DispatchMode == PathTracingDispatchMode::CompactedIndirect)
    {
        directShaderDefines.push_back({ "RAYTRACING_DEMO_COMPACTED_DISPATCH", "1" });
        indirectShaderDefines.push_back({ "RAYTRACING_DEMO_COMPACTED_DISPATCH", "1" });
    }
    std::wstring directLightingShaderFileName = L"DirectLighting";
    std::wstring indirectLightingShaderFileName = L"IndirectLighting";
    const std::wstring environmentProjectionSuffix = GetEnvironmentProjectionShaderSuffix(layout.EnvironmentProjection);
    directLightingShaderFileName += environmentProjectionSuffix;
    indirectLightingShaderFileName += environmentProjectionSuffix;
    if (useSoftShadows)
    {
        directLightingShaderFileName += L".softshadow";
        indirectLightingShaderFileName += L".softshadow";
    }
    indirectLightingShaderFileName += L".bounces" + std::to_wstring(m_MaxPathBounces);
    const std::wstring materialShadingModelSuffix = GetMaterialShadingModelShaderSuffix(m_MaterialShadingModel);
    directLightingShaderFileName += materialShadingModelSuffix;
    indirectLightingShaderFileName += materialShadingModelSuffix;
    if (m_DispatchMode == PathTracingDispatchMode::CompactedIndirect)
    {
        directLightingShaderFileName += L".compact";
        indirectLightingShaderFileName += L".compact";
    }
    directLightingShaderFileName += L".cs.cso";
    indirectLightingShaderFileName += L".cs.cso";
    const std::shared_ptr<ShaderBlob> inlineDirectLightingShader = LoadShader(
        std::move(directLightingShaderFileName),
        L"Demos/RaytracingDemo/shaders/PathTracing/DirectLighting.cs.hlsl",
        ShaderTargetProfile::Compute(),
        directShaderDefines);
    const ComputePipelineDesc inlineDirectLightingDesc = ComputePipelineDescBuilder::ReflectedDefault(*inlineDirectLightingShader)
        .WithDirectlyIndexedResourceHeap()
        .WithStaticSamplerContract(PipelineStaticSamplers::LinearWrap(1u))
        .Build();
    m_InlineDirectLightingShader = std::make_unique<ComputeShader>(m_DeviceContext, *inlineDirectLightingShader, inlineDirectLightingDesc);

    const std::shared_ptr<ShaderBlob> inlineIndirectLightingShader = LoadShader(
        std::move(indirectLightingShaderFileName),
        L"Demos/RaytracingDemo/shaders/PathTracing/IndirectLighting.cs.hlsl",
        ShaderTargetProfile::Compute(),
        indirectShaderDefines);
    const ComputePipelineDesc inlineIndirectLightingDesc = ComputePipelineDescBuilder::ReflectedDefault(*inlineIndirectLightingShader)
        .WithDirectlyIndexedResourceHeap()
        .WithStaticSamplerContract(PipelineStaticSamplers::LinearWrap(1u))
        .Build();
    m_InlineIndirectLightingShader = std::make_unique<ComputeShader>(m_DeviceContext, *inlineIndirectLightingShader, inlineIndirectLightingDesc);

}

void PathTracingPipelineController::CreateCompactedDispatchPipelines()
{
    const std::shared_ptr<ShaderBlob> inlineFinalizeShader = LoadShader(
        L"PathTracing.CompactInlineDispatchFinalize.cs.cso",
        L"Demos/RaytracingDemo/shaders/PathTracing/CompactInlineDispatchFinalize.cs.hlsl",
        ShaderTargetProfile::Compute());
    m_InlineCompactedDispatchFinalizeShader = std::make_unique<ComputeShader>(
        m_DeviceContext,
        *inlineFinalizeShader,
        ComputePipelineDescBuilder::ReflectedDefault(*inlineFinalizeShader).Build());

    if (m_Backend == PathTracingBackend::ShaderTableDxr)
    {
        const std::shared_ptr<ShaderBlob> dxrFinalizeShader = LoadShader(
            L"PathTracing.CompactDxrDispatchFinalize.cs.cso",
            L"Demos/RaytracingDemo/shaders/PathTracing/CompactDxrDispatchFinalize.cs.hlsl",
            ShaderTargetProfile::Compute());
        m_DxrCompactedDispatchFinalizeShader = std::make_unique<ComputeShader>(
            m_DeviceContext,
            *dxrFinalizeShader,
            ComputePipelineDescBuilder::ReflectedDefault(*dxrFinalizeShader).Build());
    }
}

void PathTracingPipelineController::BindRayTracingResources(
    const RayTracingAccelerationStructure& accelerationStructure,
    const RaytracingDemoSceneResources& sceneResources,
    SceneLightManager& lights,
    const std::shared_ptr<Texture>& skyboxTexture)
{
    if (!HasDxrBindingSets())
    {
        return;
    }

    const auto bindBindingSet = [&] (RayTracingBindingSet& bindingSet)
    {
        if (bindingSet.HasBinding("Scene"))
        {
            bindingSet.SetAccelerationStructure("Scene", accelerationStructure);
        }
        if (bindingSet.HasBinding("Materials"))
        {
            bindingSet.SetBuffer("Materials", sceneResources.GetMaterialBuffer());
        }
        if (bindingSet.HasBinding("Geometries"))
        {
            bindingSet.SetBuffer("Geometries", sceneResources.GetGeometryBuffer());
        }
        lights.BindRayTracingResources(bindingSet);
        if (bindingSet.HasBinding("Skybox"))
        {
            bindingSet.SetTexture("Skybox", ShaderResourceView::EnvironmentTexture(skyboxTexture));
        }
    };

    bindBindingSet(*m_DirectRayTracingBindingSet);
    bindBindingSet(*m_IndirectRayTracingBindingSet);
}

ComputeShader& PathTracingPipelineController::GetInlineDirectLightingShader() const
{
    return *m_InlineDirectLightingShader;
}

ComputeShader& PathTracingPipelineController::GetInlineIndirectLightingShader() const
{
    return *m_InlineIndirectLightingShader;
}

ComputeShader& PathTracingPipelineController::GetInlineCompactedDispatchFinalizeShader() const
{
    Assert(m_InlineCompactedDispatchFinalizeShader != nullptr, "Inline compact-dispatch finalizer is unavailable for the full-resolution pipeline.");
    return *m_InlineCompactedDispatchFinalizeShader;
}

ComputeShader& PathTracingPipelineController::GetDxrCompactedDispatchFinalizeShader() const
{
    Assert(m_DxrCompactedDispatchFinalizeShader != nullptr, "DXR compact-dispatch finalizer is unavailable for the current pipeline.");
    return *m_DxrCompactedDispatchFinalizeShader;
}

uint32_t PathTracingPipelineController::GetLightingCompositeVariantKey(
    const PathTracingCompositeFeatures& features)
{
    Assert(features.DenoiserMode <= 2u, "Unsupported lighting composite denoiser mode.");
    return (features.DirectLightingEnabled ? 1u : 0u) |
        (features.IndirectLightingEnabled ? 1u : 0u) << 1u |
        (features.AccumulationEnabled ? 1u : 0u) << 2u |
        (features.DenoiserMode << 3u) |
        ((features.DenoiserMode == 1u && features.UseNrdReblur ? 1u : 0u) << 5u);
}

ComputeShader& PathTracingPipelineController::GetLightingCompositeShader(
    const PathTracingCompositeFeatures& features)
{
    const uint32_t variantKey = GetLightingCompositeVariantKey(features);
    auto [shaderIt, inserted] = m_LightingCompositeShaders.try_emplace(variantKey);
    if (inserted)
    {
        std::vector<ShaderVariantDefine> defines = {
            { "RAYTRACING_DEMO_COMPOSITE_DIRECT_LIGHTING", features.DirectLightingEnabled ? "1" : "0" },
            { "RAYTRACING_DEMO_COMPOSITE_INDIRECT_LIGHTING", features.IndirectLightingEnabled ? "1" : "0" },
            { "RAYTRACING_DEMO_COMPOSITE_ACCUMULATION", features.AccumulationEnabled ? "1" : "0" },
            { "RAYTRACING_DEMO_COMPOSITE_DENOISER_MODE", std::to_string(features.DenoiserMode) },
            { "RAYTRACING_DEMO_COMPOSITE_NRD_REBLUR", features.UseNrdReblur ? "1" : "0" },
        };
        const std::shared_ptr<ShaderBlob> shaderBlob = LoadShader(
            L"LightingComposite.cs.cso.variant" + std::to_wstring(variantKey),
            L"Demos/RaytracingDemo/shaders/PathTracing/LightingComposite.cs.hlsl",
            ShaderTargetProfile::Compute(),
            std::move(defines));
        shaderIt->second = std::make_unique<ComputeShader>(
            m_DeviceContext,
            *shaderBlob,
            ComputePipelineDescBuilder::ReflectedDefault(*shaderBlob).Build());
    }

    Assert(shaderIt->second != nullptr, "Lighting composite shader creation failed.");
    return *shaderIt->second;
}

RayTracingShader& PathTracingPipelineController::GetRayTracingShader() const
{
    return *m_RayTracingShader;
}

RayTracingBindingSet& PathTracingPipelineController::GetDirectRayTracingBindingSet() const
{
    return *m_DirectRayTracingBindingSet;
}

RayTracingBindingSet& PathTracingPipelineController::GetIndirectRayTracingBindingSet() const
{
    return *m_IndirectRayTracingBindingSet;
}

bool PathTracingPipelineController::HasDxrBindingSets() const
{
    return m_DirectRayTracingBindingSet != nullptr && m_IndirectRayTracingBindingSet != nullptr;
}
//Modify End
