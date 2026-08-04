//Modify Begin:2026-07-27 by BestHui
#pragma once

#include <Framework/Rendering/Pipeline/ComputeShader.h>
#include <Framework/Rendering/RayTracing/RayTracingShader.h>
#include <Framework/Rendering/Pipeline/ShaderVariant.h>

#include <cstdint>
#include <deque>
#include <memory>

class RayTracingAccelerationStructure;
//Modify Begin:2026-07-30 by BestHui
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

//Modify Begin:2026-07-30 by BestHui
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

    bool operator!=(const RayTracingSceneResourceLayout& other) const
    {
        return TextureDescriptorCapacity != other.TextureDescriptorCapacity ||
            GeometryDescriptorCapacity != other.GeometryDescriptorCapacity;
    }
};

class PathTracingPipelineController final
{
public:
    explicit PathTracingPipelineController(FrameworkDeviceContext& deviceContext)
        : m_DeviceContext(deviceContext)
    {
    }

    void Reset();
    //Modify Begin:2026-07-30 by BestHui
    void EnsurePipelines(
        PathTracingBackend backend,
        PathTracingShadowMode shadowMode,
        const RayTracingSceneResourceLayout& layout);
    //Modify End
    void BindRayTracingResources(
        const RayTracingAccelerationStructure& accelerationStructure,
        const RaytracingDemoSceneResources& sceneResources,
        SceneLightManager& lights,
        const std::shared_ptr<Texture>& skyboxTexture);

    PathTracingBackend GetBackend() const { return m_Backend; }
    //Modify Begin:2026-07-30 by BestHui
    PathTracingShadowMode GetShadowMode() const { return m_ShadowMode; }
    //Modify End
    ComputeShader& GetInlineDirectLightingShader() const;
    ComputeShader& GetInlineIndirectLightingShader() const;
    ComputeShader& GetLightingCompositeShader() const;
//Modify Begin:2026-07-30 by BestHui
    RayTracingShader& GetRayTracingShader() const;
//Modify End
    RayTracingBindingSet& GetDirectRayTracingBindingSet() const;
    RayTracingBindingSet& GetIndirectRayTracingBindingSet() const;
    bool HasDxrBindingSets() const;
    const RayTracingSceneResourceLayout& GetLayout() const { return m_Layout; }

private:
//Modify Begin:2026-07-27 by BestHui
    struct RetiredPipelines
    {
        uint64_t FrameIndex = 0;
        std::unique_ptr<RayTracingShader> RayTracingShader;
        std::unique_ptr<RayTracingBindingSet> DirectRayTracingBindingSet;
        std::unique_ptr<RayTracingBindingSet> IndirectRayTracingBindingSet;
        std::unique_ptr<ComputeShader> InlineDirectLightingShader;
        std::unique_ptr<ComputeShader> InlineIndirectLightingShader;
        std::unique_ptr<ComputeShader> LightingCompositeShader;
    };

    void RetireCurrentPipelines();
    void ReleaseExpiredRetiredPipelines();
//Modify End
    std::shared_ptr<ShaderBlob> LoadShader(std::wstring compiledFileName, std::string targetProfile);
    void CreateInlinePipelines(const RayTracingSceneResourceLayout& layout);
    void CreateDxrPipeline(const RayTracingSceneResourceLayout& layout);

    FrameworkDeviceContext& m_DeviceContext;
    ShaderVariantManager m_ShaderVariants;
    PathTracingBackend m_Backend = PathTracingBackend::InlineRayQuery;
    //Modify Begin:2026-07-30 by BestHui
    PathTracingShadowMode m_ShadowMode = PathTracingShadowMode::HardShadows;
    //Modify End
    RayTracingSceneResourceLayout m_Layout;
    std::unique_ptr<RayTracingShader> m_RayTracingShader;
    std::unique_ptr<RayTracingBindingSet> m_DirectRayTracingBindingSet;
    std::unique_ptr<RayTracingBindingSet> m_IndirectRayTracingBindingSet;
    std::unique_ptr<ComputeShader> m_InlineDirectLightingShader;
    std::unique_ptr<ComputeShader> m_InlineIndirectLightingShader;
    std::unique_ptr<ComputeShader> m_LightingCompositeShader;
//Modify Begin:2026-07-27 by BestHui
    std::deque<RetiredPipelines> m_RetiredPipelines;
//Modify End
};
//Modify End
