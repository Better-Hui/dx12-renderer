#pragma once

//Modify Begin:2026-08-06 by BestHui
#include <DX12Library/StructuredBuffer.h>

#include <Framework/Rendering/Lighting/LightingData.h>
#include <Framework/Rendering/Lighting/SurfaceEmitter.h>
#include <Framework/Scene/Light.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

class CommandContext;
class CommandList;
class ComputeShader;
class FrameworkDeviceContext;
class RayTracingBindingSet;
class SharedUploadBuffer;

struct LightingGpuInput
{
    const std::vector<DirectionalLight>& DirectionalLights;
    const std::vector<PointLight>& PointLights;
    const std::vector<AreaLightData>& AreaLights;
    const SurfaceEmitterSceneData& MeshSurfaceEmitters;
    bool DirectionalLightsEnabled = true;
    bool PointLightsEnabled = true;
    bool AreaLightsEnabled = true;
};

class LightingGpuResources final
{
public:
    explicit LightingGpuResources(FrameworkDeviceContext& deviceContext);
    ~LightingGpuResources();

    LightingGpuResources(const LightingGpuResources&) = delete;
    LightingGpuResources& operator=(const LightingGpuResources&) = delete;

    void Rebuild(const LightingGpuInput& input);
    void SetMeshSurfaceEmitters(SurfaceEmitterSceneData emitterData);
    void SetLightGroupSettings(bool directionalLightsEnabled, bool pointLightsEnabled, bool areaLightsEnabled);
    void UpdateDirectionalLight(const DirectionalLight& light, size_t lightIndex);
    void UpdatePointLight(const PointLight& light, size_t lightIndex, bool updateSamplingDistribution = true);
    void UpdateAreaLight(const AreaLightData& light, size_t lightIndex);

    void Initialize(CommandList& commandList);
    bool Upload(CommandList& commandList, uint64_t frameIndex);
    void BindComputeResources(CommandContext& commandContext, ComputeShader& shader);
    void PrepareAsyncComputeResources(CommandList& commandList) const;
    void BindRayTracingResources(RayTracingBindingSet& bindingSet);

    uint32_t GetDirectionalLightCount() const;
    uint32_t GetPointLightCount() const;
    uint32_t GetSurfaceEmitterCount() const;
    size_t GetMeshSurfaceEmitterCount() const;

private:
    void BuildGpuData(const std::vector<DirectionalLight>& directionalLights, const std::vector<PointLight>& pointLights);
    void RebuildSurfaceEmitterGpuData();
    void UpdateAreaLightSurfaceEmitter(size_t lightIndex);
    void RebuildDirectLightSamplingCdf();
    void MarkDirectLightSamplingDirty();
    void MarkAllGpuDataDirty();
    void MarkDirectionalLightsDirty(size_t beginIndex, size_t endIndex, bool updateSamplingDistribution);
    void MarkPointLightsDirty(size_t beginIndex, size_t endIndex, bool updateSamplingDistribution);

    FrameworkDeviceContext& m_DeviceContext;
    std::vector<AreaLightData> m_AreaLights;
    SurfaceEmitterSceneData m_MeshSurfaceEmitterData;
    std::vector<DirectionalLightData> m_DirectionalLightGpuData;
    std::vector<PointLightData> m_PointLightGpuData;
    std::vector<SurfaceEmitterGeometryData> m_SurfaceEmitterGeometryGpuData;
    std::vector<SurfaceEmitterTriangleData> m_SurfaceEmitterTriangleGpuData;
    std::vector<float> m_SurfaceEmitterTriangleCdfGpuData;
    std::vector<SurfaceEmitterInstanceData> m_SurfaceEmitterInstanceGpuData;
    std::vector<float> m_DirectLightCdfGpuData;

    StructuredBuffer m_DirectionalLightBuffer;
    StructuredBuffer m_PointLightBuffer;
    StructuredBuffer m_SurfaceEmitterGeometryBuffer;
    StructuredBuffer m_SurfaceEmitterTriangleBuffer;
    StructuredBuffer m_SurfaceEmitterTriangleCdfBuffer;
    StructuredBuffer m_SurfaceEmitterInstanceBuffer;
    StructuredBuffer m_DirectLightCdfBuffer;
    std::unique_ptr<SharedUploadBuffer> m_UploadBuffer;

    size_t m_DirectionalLightBufferCapacity = 0;
    size_t m_PointLightBufferCapacity = 0;
    size_t m_SurfaceEmitterGeometryBufferCapacity = 0;
    size_t m_SurfaceEmitterTriangleBufferCapacity = 0;
    size_t m_SurfaceEmitterTriangleCdfBufferCapacity = 0;
    size_t m_SurfaceEmitterInstanceBufferCapacity = 0;
    size_t m_DirectLightCdfBufferCapacity = 0;

    size_t m_DirectionalLightDirtyBegin = 0;
    size_t m_DirectionalLightDirtyEnd = 0;
    size_t m_PointLightDirtyBegin = 0;
    size_t m_PointLightDirtyEnd = 0;
    size_t m_SurfaceEmitterGeometryDirtyBegin = 0;
    size_t m_SurfaceEmitterGeometryDirtyEnd = 0;
    size_t m_SurfaceEmitterTriangleDirtyBegin = 0;
    size_t m_SurfaceEmitterTriangleDirtyEnd = 0;
    size_t m_SurfaceEmitterTriangleCdfDirtyBegin = 0;
    size_t m_SurfaceEmitterTriangleCdfDirtyEnd = 0;
    size_t m_SurfaceEmitterInstanceDirtyBegin = 0;
    size_t m_SurfaceEmitterInstanceDirtyEnd = 0;
    size_t m_DirectLightCdfDirtyBegin = 0;
    size_t m_DirectLightCdfDirtyEnd = 0;

    uint32_t m_RectangleEmitterGeometryIndex = SurfaceEmitterInvalidMaterialIndex;
    size_t m_RectangleSurfaceEmitterOffset = 0;
    bool m_DirectionalLightsEnabled = true;
    bool m_PointLightsEnabled = true;
    bool m_AreaLightsEnabled = true;
};
//Modify End
