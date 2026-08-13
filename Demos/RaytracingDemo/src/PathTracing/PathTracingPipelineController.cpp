//Modify Begin:2026-07-27 by BestHui
#include <PathTracing/PathTracingPipelineController.h>

#include <DX12Library/Application.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/Window.h>
//Modify Begin:2026-07-30 by BestHui
#include <Framework/Core/FrameworkDeviceContext.h>
//Modify End
#include <Framework/Rendering/RayTracing/RayTracingAccelerationStructure.h>
#include <Framework/Rendering/Pipeline/ShaderBlob.h>
//Modify Begin:2026-07-30 by BestHui
#include <Framework/Rendering/Pipeline/ShaderTargetProfile.h>
//Modify End
#include <Framework/Rendering/Texture/ShaderResourceView.h>
#include <Scene/SceneLightManager.h>
#include <Scene/SceneResources.h>

#include <algorithm>

namespace
{
//Modify Begin:2026-08-06 by BestHui
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

//Modify Begin:2026-08-11 by BestHui
    void AddMaxPathBouncesDefine(
        std::vector<ShaderVariantDefine>& defines,
        const uint32_t maxPathBounces)
    {
        defines.push_back({ "RAYTRACING_DEMO_MAX_BOUNCES", std::to_string(maxPathBounces) });
    }
//Modify End
//Modify Begin:2026-07-30 by BestHui
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
//Modify End
//Modify End
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

//Modify Begin:2026-07-27 by BestHui
void PathTracingPipelineController::RetireCurrentPipelines()
{
    if (m_RayTracingShader == nullptr &&
        m_DirectRayTracingBindingSet == nullptr &&
        m_IndirectRayTracingBindingSet == nullptr &&
        m_InlineDirectLightingShader == nullptr &&
        m_InlineIndirectLightingShader == nullptr &&
        m_LightingCompositeShaders.empty())
    {
        return;
    }

    RetiredPipelines retired;
    retired.FrameIndex = Application::GetFrameCount();
    retired.RayTracingShader = std::move(m_RayTracingShader);
    retired.DirectRayTracingBindingSet = std::move(m_DirectRayTracingBindingSet);
    retired.IndirectRayTracingBindingSet = std::move(m_IndirectRayTracingBindingSet);
    retired.InlineDirectLightingShader = std::move(m_InlineDirectLightingShader);
    retired.InlineIndirectLightingShader = std::move(m_InlineIndirectLightingShader);
    retired.LightingCompositeShaders = std::move(m_LightingCompositeShaders);
    m_RetiredPipelines.push_back(std::move(retired));
}

void PathTracingPipelineController::ReleaseExpiredRetiredPipelines()
{
    const uint64_t currentFrame = Application::GetFrameCount();
    while (!m_RetiredPipelines.empty())
    {
        const RetiredPipelines& retired = m_RetiredPipelines.front();
        if (currentFrame <= retired.FrameIndex + Window::BUFFER_COUNT)
        {
            break;
        }
        m_RetiredPipelines.pop_front();
    }
}
//Modify End

void PathTracingPipelineController::Reset()
{
//Modify Begin:2026-07-27 by BestHui
    RetireCurrentPipelines();
    m_RetiredPipelines.clear();
//Modify End
    m_LightingCompositeShaders.clear();
    m_InlineIndirectLightingShader.reset();
    m_InlineDirectLightingShader.reset();
    m_IndirectRayTracingBindingSet.reset();
    m_DirectRayTracingBindingSet.reset();
    m_RayTracingShader.reset();
    m_ShaderVariants.Clear();
//Modify Begin:2026-07-30 by BestHui
    m_ShadowMode = PathTracingShadowMode::HardShadows;
//Modify End
//Modify Begin:2026-07-30 by BestHui
    m_MaterialShadingModel = MaterialShadingModel::Pbr;
//Modify End
//Modify Begin:2026-08-11 by BestHui
    m_MaxPathBounces = 3u;
//Modify End
    m_Layout = {};
}

void PathTracingPipelineController::EnsurePipelines(
    const PathTracingBackend backend,
//Modify Begin:2026-07-30 by BestHui
    const PathTracingShadowMode shadowMode,
//Modify End
    const MaterialShadingModel shadingModel,
    const RayTracingSceneResourceLayout& layout,
    const uint32_t maxPathBounces)
{
//Modify Begin:2026-07-27 by BestHui
    ReleaseExpiredRetiredPipelines();
//Modify End
//Modify Begin:2026-08-11 by BestHui
    const uint32_t clampedMaxPathBounces = std::clamp(maxPathBounces, 1u, 5u);
//Modify End
    const bool layoutChanged = m_Layout != layout;
    const bool backendChanged = m_Backend != backend;
//Modify Begin:2026-07-30 by BestHui
    const bool shadowModeChanged = m_ShadowMode != shadowMode;
//Modify End
    const bool shadingModelChanged = m_MaterialShadingModel != shadingModel;
//Modify Begin:2026-08-11 by BestHui
    const bool maxPathBouncesChanged = m_MaxPathBounces != clampedMaxPathBounces;
//Modify End
    const bool needsDxrPipeline = backend == PathTracingBackend::ShaderTableDxr;

    if (!layoutChanged &&
        !backendChanged &&
//Modify Begin:2026-07-30 by BestHui
        !shadowModeChanged &&
//Modify End
        !shadingModelChanged &&
        !maxPathBouncesChanged &&
        m_InlineDirectLightingShader != nullptr &&
        m_InlineIndirectLightingShader != nullptr &&
        (!needsDxrPipeline ||
            (m_RayTracingShader != nullptr &&
                m_DirectRayTracingBindingSet != nullptr &&
                m_IndirectRayTracingBindingSet != nullptr)))
    {
        return;
    }

    m_Backend = backend;
//Modify Begin:2026-07-30 by BestHui
    m_ShadowMode = shadowMode;
//Modify End
    m_MaterialShadingModel = shadingModel;
//Modify Begin:2026-08-11 by BestHui
    m_MaxPathBounces = clampedMaxPathBounces;
//Modify End
    m_Layout = layout;

//Modify Begin:2026-07-27 by BestHui
    if (layoutChanged || backendChanged
//Modify Begin:2026-07-30 by BestHui
        || shadowModeChanged || shadingModelChanged || maxPathBouncesChanged
//Modify End
        )
    {
        RetireCurrentPipelines();
    }
//Modify End

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
}

void PathTracingPipelineController::CreateDxrPipeline(const RayTracingSceneResourceLayout& layout)
{
//Modify Begin:2026-07-30 by BestHui
    const bool useSoftShadows = m_ShadowMode == PathTracingShadowMode::SoftShadows;
    std::wstring shaderFileName = L"PathTracing" + GetEnvironmentProjectionShaderSuffix(layout.EnvironmentProjection);
    if (useSoftShadows)
    {
        shaderFileName += L".softshadow";
    }
//Modify Begin:2026-08-11 by BestHui
    shaderFileName += L".bounces" + std::to_wstring(m_MaxPathBounces);
//Modify End
    shaderFileName += GetMaterialShadingModelShaderSuffix(m_MaterialShadingModel);
    shaderFileName += L".rt.cso";
    std::vector<ShaderVariantDefine> defines;
    AddEnvironmentProjectionDefine(defines, layout.EnvironmentProjection);
    if (useSoftShadows)
    {
        defines.push_back({ "RAYTRACING_DEMO_SOFT_SHADOWS", "1" });
    }
//Modify Begin:2026-08-11 by BestHui
    AddMaxPathBouncesDefine(defines, m_MaxPathBounces);
//Modify End
    AddMaterialShadingModelDefine(defines, m_MaterialShadingModel);
    constexpr const char* targetProfile = ShaderTargetProfile::RayTracingLibrary();
    const std::shared_ptr<ShaderBlob> pathTracingShader = LoadShader(
        shaderFileName,
        L"Demos/RaytracingDemo/shaders/PathTracing/PathTracing.rt.hlsl",
        targetProfile,
        defines,
        "");
//Modify End
    const RayTracingPipelineDesc rayTracingDesc = RayTracingPipelineDescBuilder::ReflectedDefault(*pathTracingShader)
//Modify Begin:2026-07-30 by BestHui
        .WithStaticSamplerContract(PipelineStaticSamplers::LinearWrap(0u))
//Modify End
//Modify Begin:2026-07-27 by BestHui
        .WithTriangleHitGroup(L"HitGroup", L"ClosestHit")
        .WithTriangleHitGroup(L"VisibilityHitGroup", L"VisibilityClosestHit")
        .WithRayGenerationPass("DirectLightingRayGen", L"DirectLightingRayGen", { L"Miss" }, { L"HitGroup", L"VisibilityHitGroup" })
        .WithRayGenerationPass("IndirectLightingRayGen", L"IndirectLightingRayGen", { L"Miss" }, { L"HitGroup", L"VisibilityHitGroup" })
//Modify End
//Modify Begin:2026-08-06 by BestHui
        .WithPayloadSize(80)
//Modify End
        .Build();
    m_RayTracingShader = std::make_unique<RayTracingShader>(m_DeviceContext, *pathTracingShader, rayTracingDesc);
    m_DirectRayTracingBindingSet = std::make_unique<RayTracingBindingSet>(m_RayTracingShader->CreateBindingSet());
    m_IndirectRayTracingBindingSet = std::make_unique<RayTracingBindingSet>(m_RayTracingShader->CreateBindingSet());
}

void PathTracingPipelineController::CreateInlinePipelines(const RayTracingSceneResourceLayout& layout)
{
//Modify Begin:2026-07-30 by BestHui
    const bool useSoftShadows = m_ShadowMode == PathTracingShadowMode::SoftShadows;
    std::vector<ShaderVariantDefine> directShaderDefines;
    AddEnvironmentProjectionDefine(directShaderDefines, layout.EnvironmentProjection);
    if (useSoftShadows)
    {
        directShaderDefines.push_back({ "RAYTRACING_DEMO_SOFT_SHADOWS", "1" });
    }
//Modify Begin:2026-08-11 by BestHui
    std::vector<ShaderVariantDefine> indirectShaderDefines = directShaderDefines;
    AddMaxPathBouncesDefine(indirectShaderDefines, m_MaxPathBounces);
//Modify End
    AddMaterialShadingModelDefine(directShaderDefines, m_MaterialShadingModel);
    AddMaterialShadingModelDefine(indirectShaderDefines, m_MaterialShadingModel);
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
//Modify Begin:2026-08-11 by BestHui
    indirectLightingShaderFileName += L".bounces" + std::to_wstring(m_MaxPathBounces);
//Modify End
    const std::wstring materialShadingModelSuffix = GetMaterialShadingModelShaderSuffix(m_MaterialShadingModel);
    directLightingShaderFileName += materialShadingModelSuffix;
    indirectLightingShaderFileName += materialShadingModelSuffix;
    directLightingShaderFileName += L".cs.cso";
    indirectLightingShaderFileName += L".cs.cso";
    const std::shared_ptr<ShaderBlob> inlineDirectLightingShader = LoadShader(
        std::move(directLightingShaderFileName),
        L"Demos/RaytracingDemo/shaders/PathTracing/DirectLighting.cs.hlsl",
        ShaderTargetProfile::Compute(),
        directShaderDefines);
//Modify End
    const ComputePipelineDesc inlineDirectLightingDesc = ComputePipelineDescBuilder::ReflectedDefault(*inlineDirectLightingShader)
//Modify Begin:2026-07-30 by BestHui
        .WithDirectlyIndexedResourceHeap()
//Modify End
        .WithStaticSamplerContract(PipelineStaticSamplers::LinearWrap(1u))
        .Build();
    m_InlineDirectLightingShader = std::make_unique<ComputeShader>(m_DeviceContext, *inlineDirectLightingShader, inlineDirectLightingDesc);

    //Modify Begin:2026-07-30 by BestHui
    const std::shared_ptr<ShaderBlob> inlineIndirectLightingShader = LoadShader(
        std::move(indirectLightingShaderFileName),
        L"Demos/RaytracingDemo/shaders/PathTracing/IndirectLighting.cs.hlsl",
        ShaderTargetProfile::Compute(),
        indirectShaderDefines);
    //Modify End
    const ComputePipelineDesc inlineIndirectLightingDesc = ComputePipelineDescBuilder::ReflectedDefault(*inlineIndirectLightingShader)
//Modify Begin:2026-07-30 by BestHui
        .WithDirectlyIndexedResourceHeap()
//Modify End
        .WithStaticSamplerContract(PipelineStaticSamplers::LinearWrap(1u))
        .Build();
    m_InlineIndirectLightingShader = std::make_unique<ComputeShader>(m_DeviceContext, *inlineIndirectLightingShader, inlineIndirectLightingDesc);

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

//Modify Begin:2026-07-30 by BestHui
RayTracingShader& PathTracingPipelineController::GetRayTracingShader() const
{
    return *m_RayTracingShader;
}
//Modify End

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
