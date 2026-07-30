//Modify Begin:2026-07-27 by BestHui
#include <PathTracing/PathTracingPipelineController.h>

#include <DX12Library/Application.h>
#include <DX12Library/Window.h>
#include <Framework/RayTracingAccelerationStructure.h>
#include <Framework/ShaderBlob.h>
#include <Framework/ShaderResourceView.h>
#include <Scene/SceneLightManager.h>
#include <Scene/SceneResources.h>

#include <algorithm>

std::shared_ptr<ShaderBlob> PathTracingPipelineController::LoadShader(
    std::wstring compiledFileName,
    std::string targetProfile)
{
    ShaderVariantDesc desc;
    desc.CompiledFileName = std::move(compiledFileName);
    desc.TargetProfile = std::move(targetProfile);
    desc.HotReloadKey = "RaytracingDemo";
    return m_ShaderVariants.LoadCompiledVariant(desc);
}

//Modify Begin:2026-07-27 by BestHui
void PathTracingPipelineController::RetireCurrentPipelines()
{
    if (m_RayTracingShader == nullptr &&
        m_DirectRayTracingBindingSet == nullptr &&
        m_IndirectRayTracingBindingSet == nullptr &&
        m_InlineDirectLightingShader == nullptr &&
        m_InlineIndirectLightingShader == nullptr &&
        m_LightingCompositeShader == nullptr)
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
    retired.LightingCompositeShader = std::move(m_LightingCompositeShader);
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
    m_LightingCompositeShader.reset();
    m_InlineIndirectLightingShader.reset();
    m_InlineDirectLightingShader.reset();
    m_IndirectRayTracingBindingSet.reset();
    m_DirectRayTracingBindingSet.reset();
    m_RayTracingShader.reset();
    m_ShaderVariants.Clear();
    m_Layout = {};
}

void PathTracingPipelineController::EnsurePipelines(
    const PathTracingBackend backend,
    const RayTracingSceneResourceLayout& layout)
{
//Modify Begin:2026-07-27 by BestHui
    ReleaseExpiredRetiredPipelines();
//Modify End
    const bool layoutChanged = m_Layout != layout;
    const bool backendChanged = m_Backend != backend;
    const bool needsDxrPipeline = backend == PathTracingBackend::ShaderTableDxr;

    if (!layoutChanged &&
        !backendChanged &&
        m_InlineDirectLightingShader != nullptr &&
        m_InlineIndirectLightingShader != nullptr &&
        m_LightingCompositeShader != nullptr &&
        (!needsDxrPipeline ||
            (m_RayTracingShader != nullptr &&
                m_DirectRayTracingBindingSet != nullptr &&
                m_IndirectRayTracingBindingSet != nullptr)))
    {
        return;
    }

    m_Backend = backend;
    m_Layout = layout;

//Modify Begin:2026-07-27 by BestHui
    if (layoutChanged || backendChanged)
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
    const std::shared_ptr<ShaderBlob> pathTracingShader = LoadShader(L"PathTracing.rt.cso", "lib_6_6");
    const RayTracingPipelineDesc rayTracingDesc = RayTracingPipelineDescBuilder::ReflectedDefault(*pathTracingShader)
        .WithExport(L"DirectLightingRayGen")
        .WithExport(L"IndirectLightingRayGen")
//Modify Begin:2026-07-27 by BestHui
        .WithExport(L"VisibilityClosestHit")
        .WithTriangleHitGroup(L"VisibilityHitGroup", L"VisibilityClosestHit")
        .WithRayGenerationPass("DirectLightingRayGen", L"DirectLightingRayGen", { L"Miss" }, { L"HitGroup", L"VisibilityHitGroup" })
        .WithRayGenerationPass("IndirectLightingRayGen", L"IndirectLightingRayGen", { L"Miss" }, { L"HitGroup", L"VisibilityHitGroup" })
//Modify End
        .WithTextureArray("Textures", 0, 3, layout.TextureDescriptorCapacity)
        .WithVertexBufferArray("VertexBuffers", 0, 1, layout.GeometryDescriptorCapacity)
        .WithIndexBufferArray("IndexBuffers", 0, 2, layout.GeometryDescriptorCapacity)
        .WithPayloadSize(64)
        .Build();
    m_RayTracingShader = std::make_unique<RayTracingShader>(*pathTracingShader, rayTracingDesc);
    m_DirectRayTracingBindingSet = std::make_unique<RayTracingBindingSet>(m_RayTracingShader->CreateBindingSet());
    m_IndirectRayTracingBindingSet = std::make_unique<RayTracingBindingSet>(m_RayTracingShader->CreateBindingSet());
}

void PathTracingPipelineController::CreateInlinePipelines(const RayTracingSceneResourceLayout& layout)
{
    const std::shared_ptr<ShaderBlob> inlineDirectLightingShader = LoadShader(L"DirectLighting.cs.cso", "cs_6_6");
    const ComputePipelineDesc inlineDirectLightingDesc = ComputePipelineDescBuilder::ReflectedDefault(*inlineDirectLightingShader)
        .WithDescriptorArrayCount("Textures", layout.TextureDescriptorCapacity)
        .WithDescriptorArrayCount("VertexBuffers", layout.GeometryDescriptorCapacity)
        .WithDescriptorArrayCount("IndexBuffers", layout.GeometryDescriptorCapacity)
        .Build();
    m_InlineDirectLightingShader = std::make_unique<ComputeShader>(*inlineDirectLightingShader, inlineDirectLightingDesc);

    const std::shared_ptr<ShaderBlob> inlineIndirectLightingShader = LoadShader(L"IndirectLighting.cs.cso", "cs_6_6");
    const ComputePipelineDesc inlineIndirectLightingDesc = ComputePipelineDescBuilder::ReflectedDefault(*inlineIndirectLightingShader)
        .WithDescriptorArrayCount("Textures", layout.TextureDescriptorCapacity)
        .WithDescriptorArrayCount("VertexBuffers", layout.GeometryDescriptorCapacity)
        .WithDescriptorArrayCount("IndexBuffers", layout.GeometryDescriptorCapacity)
        .Build();
    m_InlineIndirectLightingShader = std::make_unique<ComputeShader>(*inlineIndirectLightingShader, inlineIndirectLightingDesc);

    const std::shared_ptr<ShaderBlob> lightingCompositeShader = LoadShader(L"LightingComposite.cs.cso", "cs_6_6");
    m_LightingCompositeShader = std::make_unique<ComputeShader>(
        *lightingCompositeShader,
        ComputePipelineDescBuilder::ReflectedDefault(*lightingCompositeShader).Build());
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
        const std::vector<std::shared_ptr<Texture>>& textures = sceneResources.GetTextures();
        if (bindingSet.HasBinding("Textures"))
        {
            for (uint32_t textureIndex = 0; textureIndex < textures.size(); ++textureIndex)
            {
                bindingSet.SetTexture("Textures", textureIndex, ShaderResourceView(textures[textureIndex]));
            }
        }
        if (bindingSet.HasBinding("Skybox"))
        {
            bindingSet.SetTexture("Skybox", ShaderResourceView::TextureCube(skyboxTexture));
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

ComputeShader& PathTracingPipelineController::GetLightingCompositeShader() const
{
    return *m_LightingCompositeShader;
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
