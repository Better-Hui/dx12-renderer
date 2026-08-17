//Modify Begin:2026-07-27 by Hui
#pragma once

#include <Framework/Rendering/Pipeline/ComputeShader.h>
#include <Framework/Rendering/Lighting/MaterialShadingModel.h>
#include <Framework/Rendering/RayTracing/RayTracingShader.h>
#include <Framework/Rendering/Pipeline/ShaderVariant.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>

#include <cstdint>
#include <deque>
#include <memory>
#include <unordered_map>

class RayTracingAccelerationStructure;
//Modify Begin:2026-07-30 by Hui
class FrameworkDeviceContext;
//Modify End
class SceneLightManager;
class Texture;
class RaytracingDemoSceneResources;

enum class PathTracingBackend
{
    InlineRayQuery = 0,
    ShaderTableDxr = 1,
};

//Modify Begin:2026-07-30 by Hui
enum class PathTracingShadowMode
{
    HardShadows = 0,
    SoftShadows = 1,
};
//Modify End

struct RayTracingSceneResourceLayout
{
    static constexpr uint32_t MinDescriptorArrayCapacity = 1u;

    uint32_t TextureDescriptorCapacity = MinDescriptorArrayCapacity;
    uint32_t GeometryDescriptorCapacity = MinDescriptorArrayCapacity;
//Modify Begin:2026-08-06 by Hui
    EnvironmentTextureProjection EnvironmentProjection = EnvironmentTextureProjection::Cubemap;
//Modify End

    bool operator!=(const RayTracingSceneResourceLayout& other) const
    {
        return TextureDescriptorCapacity != other.TextureDescriptorCapacity ||
            GeometryDescriptorCapacity != other.GeometryDescriptorCapacity ||
            EnvironmentProjection != other.EnvironmentProjection;
    }
};

struct PathTracingCompositeFeatures
{
    bool DirectLightingEnabled = false;
    bool IndirectLightingEnabled = false;
    bool AccumulationEnabled = false;
    uint32_t DenoiserMode = 0u;
    bool UseNrdReblur = false;
};

class PathTracingPipelineController final
{
public:
    explicit PathTracingPipelineController(FrameworkDeviceContext& deviceContext)
        : m_DeviceContext(deviceContext)
    {
    }

    void Reset();
    //Modify Begin:2026-07-30 by Hui
    void EnsurePipelines(
        PathTracingBackend backend,
        PathTracingShadowMode shadowMode,
        MaterialShadingModel shadingModel,
        const RayTracingSceneResourceLayout& layout,
        uint32_t maxPathBounces);
    //Modify End
    void BindRayTracingResources(
        const RayTracingAccelerationStructure& accelerationStructure,
        const RaytracingDemoSceneResources& sceneResources,
        SceneLightManager& lights,
        const std::shared_ptr<Texture>& skyboxTexture);

    PathTracingBackend GetBackend() const { return m_Backend; }
    //Modify Begin:2026-07-30 by Hui
    PathTracingShadowMode GetShadowMode() const { return m_ShadowMode; }
    MaterialShadingModel GetMaterialShadingModel() const { return m_MaterialShadingModel; }
    //Modify End
    ComputeShader& GetInlineDirectLightingShader() const;
    ComputeShader& GetInlineIndirectLightingShader() const;
    ComputeShader& GetLightingCompositeShader(const PathTracingCompositeFeatures& features);
//Modify Begin:2026-07-30 by Hui
    RayTracingShader& GetRayTracingShader() const;
//Modify End
    RayTracingBindingSet& GetDirectRayTracingBindingSet() const;
    RayTracingBindingSet& GetIndirectRayTracingBindingSet() const;
    bool HasDxrBindingSets() const;
    const RayTracingSceneResourceLayout& GetLayout() const { return m_Layout; }

private:
//Modify Begin:2026-07-27 by Hui
    struct RetiredPipelines
    {
        uint64_t DirectFenceValue = 0;
        uint64_t ComputeFenceValue = 0;
        std::unique_ptr<RayTracingShader> RayTracingShader;
        std::unique_ptr<RayTracingBindingSet> DirectRayTracingBindingSet;
        std::unique_ptr<RayTracingBindingSet> IndirectRayTracingBindingSet;
        std::unique_ptr<ComputeShader> InlineDirectLightingShader;
        std::unique_ptr<ComputeShader> InlineIndirectLightingShader;
        std::unordered_map<uint32_t, std::unique_ptr<ComputeShader>> LightingCompositeShaders;
    };

    void RetireCurrentPipelines();
    void ReleaseExpiredRetiredPipelines();
//Modify End
    std::shared_ptr<ShaderBlob> LoadShader(
        std::wstring compiledFileName,
        std::wstring sourceFileName,
        std::string targetProfile,
        std::vector<ShaderVariantDefine> defines = {},
        std::string entryPoint = "main");
    static uint32_t GetLightingCompositeVariantKey(const PathTracingCompositeFeatures& features);
    void CreateInlinePipelines(const RayTracingSceneResourceLayout& layout);
    void CreateDxrPipeline(const RayTracingSceneResourceLayout& layout);

    FrameworkDeviceContext& m_DeviceContext;
    ShaderVariantManager m_ShaderVariants;
    PathTracingBackend m_Backend = PathTracingBackend::InlineRayQuery;
//Modify Begin:2026-07-30 by Hui
    PathTracingShadowMode m_ShadowMode = PathTracingShadowMode::HardShadows;
//Modify End
//Modify Begin:2026-07-30 by Hui
    MaterialShadingModel m_MaterialShadingModel = MaterialShadingModel::Pbr;
//Modify End
    uint32_t m_MaxPathBounces = 3u;
    RayTracingSceneResourceLayout m_Layout;
    std::unique_ptr<RayTracingShader> m_RayTracingShader;
    std::unique_ptr<RayTracingBindingSet> m_DirectRayTracingBindingSet;
    std::unique_ptr<RayTracingBindingSet> m_IndirectRayTracingBindingSet;
    std::unique_ptr<ComputeShader> m_InlineDirectLightingShader;
    std::unique_ptr<ComputeShader> m_InlineIndirectLightingShader;
    std::unordered_map<uint32_t, std::unique_ptr<ComputeShader>> m_LightingCompositeShaders;
//Modify Begin:2026-07-27 by Hui
    std::deque<RetiredPipelines> m_RetiredPipelines;
//Modify End
};
//Modify End
