//Modify Begin:2026-07-27 by BestHui
#pragma once

#include <Framework/ComputeShader.h>
#include <Framework/RayTracingShader.h>
#include <Framework/ShaderVariant.h>

#include <cstdint>
#include <deque>
#include <memory>

class RayTracingAccelerationStructure;
class SceneLightManager;
class Texture;
class RaytracingDemoSceneResources;

enum class PathTracingBackend
{
    InlineRayQuery = 0,
    ShaderTableDxr = 1,
};

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
    void Reset();
    void EnsurePipelines(PathTracingBackend backend, const RayTracingSceneResourceLayout& layout);
    void BindRayTracingResources(
        const RayTracingAccelerationStructure& accelerationStructure,
        const RaytracingDemoSceneResources& sceneResources,
        SceneLightManager& lights,
        const std::shared_ptr<Texture>& skyboxTexture);

    PathTracingBackend GetBackend() const { return m_Backend; }
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

    ShaderVariantManager m_ShaderVariants;
    PathTracingBackend m_Backend = PathTracingBackend::InlineRayQuery;
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
