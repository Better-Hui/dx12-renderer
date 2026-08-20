//Modify Begin:2026-08-19 by Hui
#pragma once

#include <Framework/Rendering/Pipeline/ComputeShader.h>
#include <Framework/Rendering/Pipeline/IndirectCommandBuffer.h>
#include <Framework/Rendering/Pipeline/IndirectCommandSignature.h>
#include <Framework/Rendering/Lighting/MaterialShadingModel.h>
#include <Framework/Rendering/RayTracing/RayTracingShader.h>
#include <Framework/Rendering/Pipeline/ShaderVariant.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>

#include <cstdint>
#include <deque>
#include <memory>
#include <unordered_map>

class CommandList;
class RayTracingAccelerationStructure;
class FrameworkDeviceContext;
class Resource;
class SceneLightManager;
class Texture;
class RaytracingDemoSceneResources;

enum class PathTracingBackend
{
    InlineRayQuery = 0,
    ShaderTableDxr = 1,
};

enum class PathTracingDispatchMode : uint32_t
{
    FullResolution = 0,
    CompactedIndirect = 1,
};

enum class PathTracingShadowMode
{
    HardShadows = 0,
    SoftShadows = 1,
};

struct RayTracingSceneResourceLayout
{
    static constexpr uint32_t MinDescriptorArrayCapacity = 1u;

    uint32_t TextureDescriptorCapacity = MinDescriptorArrayCapacity;
    uint32_t GeometryDescriptorCapacity = MinDescriptorArrayCapacity;
    EnvironmentTextureProjection EnvironmentProjection = EnvironmentTextureProjection::Cubemap;

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

struct PathTracingIndirectDispatch
{
    const IndirectCommandSignature& Signature;
    Resource& Arguments;
};

class PathTracingPipelineController final
{
public:
    explicit PathTracingPipelineController(FrameworkDeviceContext& deviceContext);

    void Reset();
    void EnsurePipelines(
        PathTracingBackend backend,
        PathTracingShadowMode shadowMode,
        MaterialShadingModel shadingModel,
        const RayTracingSceneResourceLayout& layout,
        uint32_t maxPathBounces,
        PathTracingDispatchMode dispatchMode);
    void BindRayTracingResources(
        const RayTracingAccelerationStructure& accelerationStructure,
        const RaytracingDemoSceneResources& sceneResources,
        SceneLightManager& lights,
        const std::shared_ptr<Texture>& skyboxTexture);
    void PrepareDxrCompactedDispatchTemplate(
        CommandList& commandList,
        uint32_t width,
        uint32_t height,
        bool directLighting);
    PathTracingIndirectDispatch GetCompactedIndirectDispatch(
        PathTracingBackend backend,
        bool directLighting) const;
    PathTracingBackend GetBackend() const { return m_Backend; }
    PathTracingDispatchMode GetDispatchMode() const { return m_DispatchMode; }
    PathTracingShadowMode GetShadowMode() const { return m_ShadowMode; }
    MaterialShadingModel GetMaterialShadingModel() const { return m_MaterialShadingModel; }
    ComputeShader& GetInlineDirectLightingShader() const;
    ComputeShader& GetInlineIndirectLightingShader() const;
    ComputeShader& GetInlineCompactedDispatchFinalizeShader() const;
    ComputeShader& GetDxrCompactedDispatchFinalizeShader() const;
    ComputeShader& GetLightingCompositeShader(const PathTracingCompositeFeatures& features);
    RayTracingShader& GetRayTracingShader() const;
    RayTracingBindingSet& GetDirectRayTracingBindingSet() const;
    RayTracingBindingSet& GetIndirectRayTracingBindingSet() const;
    bool HasDxrBindingSets() const;
    const RayTracingSceneResourceLayout& GetLayout() const { return m_Layout; }

private:
    struct RetiredPipelines
    {
        uint64_t DirectFenceValue = 0;
        uint64_t ComputeFenceValue = 0;
        std::unique_ptr<RayTracingShader> RayTracingShader;
        std::unique_ptr<RayTracingBindingSet> DirectRayTracingBindingSet;
        std::unique_ptr<RayTracingBindingSet> IndirectRayTracingBindingSet;
        std::unique_ptr<ComputeShader> InlineDirectLightingShader;
        std::unique_ptr<ComputeShader> InlineIndirectLightingShader;
        std::unique_ptr<ComputeShader> InlineCompactedDispatchFinalizeShader;
        std::unique_ptr<ComputeShader> DxrCompactedDispatchFinalizeShader;
        std::unordered_map<uint32_t, std::unique_ptr<ComputeShader>> LightingCompositeShaders;
    };

    void RetireCurrentPipelines();
    void ReleaseExpiredRetiredPipelines();
    std::shared_ptr<ShaderBlob> LoadShader(
        std::wstring compiledFileName,
        std::wstring sourceFileName,
        std::string targetProfile,
        std::vector<ShaderVariantDefine> defines = {},
        std::string entryPoint = "main");
    static uint32_t GetLightingCompositeVariantKey(const PathTracingCompositeFeatures& features);
    void CreateInlinePipelines(const RayTracingSceneResourceLayout& layout);
    void CreateDxrPipeline(const RayTracingSceneResourceLayout& layout);
    void CreateCompactedDispatchPipelines();
    void CreateComputeIndirectDispatchResources();
    void EnsureRayTracingIndirectDispatchResources();

    FrameworkDeviceContext& m_DeviceContext;
    ShaderVariantManager m_ShaderVariants;
    PathTracingBackend m_Backend = PathTracingBackend::InlineRayQuery;
    PathTracingDispatchMode m_DispatchMode = PathTracingDispatchMode::FullResolution;
    PathTracingShadowMode m_ShadowMode = PathTracingShadowMode::HardShadows;
    MaterialShadingModel m_MaterialShadingModel = MaterialShadingModel::Pbr;
    uint32_t m_MaxPathBounces = 3u;
    RayTracingSceneResourceLayout m_Layout;
    std::unique_ptr<RayTracingShader> m_RayTracingShader;
    std::unique_ptr<RayTracingBindingSet> m_DirectRayTracingBindingSet;
    std::unique_ptr<RayTracingBindingSet> m_IndirectRayTracingBindingSet;
    std::unique_ptr<ComputeShader> m_InlineDirectLightingShader;
    std::unique_ptr<ComputeShader> m_InlineIndirectLightingShader;
    std::unique_ptr<ComputeShader> m_InlineCompactedDispatchFinalizeShader;
    std::unique_ptr<ComputeShader> m_DxrCompactedDispatchFinalizeShader;
    std::unique_ptr<IndirectCommandSignature> m_ComputeIndirectCommandSignature;
    std::unique_ptr<IndirectCommandSignature> m_RayTracingIndirectCommandSignature;
    std::unique_ptr<IndirectCommandBuffer> m_ComputeIndirectArguments;
    std::unique_ptr<IndirectCommandBuffer> m_DirectRayTracingIndirectArguments;
    std::unique_ptr<IndirectCommandBuffer> m_IndirectRayTracingIndirectArguments;
    std::unordered_map<uint32_t, std::unique_ptr<ComputeShader>> m_LightingCompositeShaders;
    std::deque<RetiredPipelines> m_RetiredPipelines;
};
//Modify End
