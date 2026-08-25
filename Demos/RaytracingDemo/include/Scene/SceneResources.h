//Modify Begin:2026-08-25 by Hui
#pragma once

#include <Scene/SceneResourceBuilders.h>
#include <Scene/SceneLighting.h>
#include <Framework/Geometry/Mesh.h>
#include <Framework/Scene/Scene.h>
#include <Framework/Rendering/Lighting/SurfaceEmitter.h>

#include <DirectXMath.h>
#include <d3d12.h>
#include <wrl.h>

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class CommandList;
class D3D12DeviceContext;
class Resource;
class VertexBuffer;
class SceneLightManager;

struct RaytracingDemoDynamicRtasUpdateStatistics
{
    uint64_t GeometryUploadCount = 0;
    uint64_t MeshletTransformUpdateCount = 0;
    uint64_t MeshletGeometryUpdateCount = 0;
    uint64_t EmissiveMeshRefreshCount = 0;
    uint64_t RefitCount = 0;
    uint64_t RestoreCount = 0;
    bool LastUpdateRestored = false;
};

struct RaytracingDemoDynamicSceneCapabilities
{
    bool SupportsMeshletTransformUpdates = true;
    bool SupportsMeshletGeometryUpdates = true;
    bool SupportsDynamicEmissiveMeshUpdates = true;
    bool SupportsSkinnedMeshUpdates = false;
};

class RaytracingDemoSceneResources final
{
public:
    explicit RaytracingDemoSceneResources(std::shared_ptr<D3D12DeviceContext> deviceContext);

    void Clear();
    void LoadDeferredLightingScene(CommandList& commandList);
    bool LoadScene(CommandList& commandList, const Scene& scene, bool enableStressTestSpheres = true);
    bool SetStressTestSpheresEnabled(CommandList& commandList, bool enabled);
    bool AreStressTestSpheresEnabled() const { return m_StressTestSpheresEnabled; }
    void SetDynamicRayTracingUpdatesEnabled(bool enabled);
    bool AreDynamicRayTracingUpdatesEnabled() const { return m_DynamicRayTracingUpdatesEnabled; }
    bool RequiresDynamicRayTracingUpdatePass() const;
    const VertexBuffer& GetDynamicRayTracingVertexBuffer() const;
    const IndexBuffer& GetDynamicRayTracingIndexBuffer() const;
    bool BeginDynamicRayTracingGeometryUpdate(CommandList& commandList, float timeSeconds);
    bool FinishDynamicRayTracingUpdate(CommandList& commandList);
    bool RefreshDynamicEmissiveMeshSurfaceEmitters(SceneLightManager& lights);
    bool HasActiveDynamicRayTracingEmitter() const { return m_DynamicRayTracingEmitterActive; }
    const RaytracingDemoDynamicRtasUpdateStatistics& GetDynamicRayTracingUpdateStatistics() const
    {
        return m_DynamicRayTracingUpdateStatistics;
    }
    const RaytracingDemoDynamicSceneCapabilities& GetDynamicSceneCapabilities() const
    {
        return m_DynamicSceneCapabilities;
    }
    void BuildRayTracingAccelerationStructure(
        CommandList& commandList,
        RayTracingAccelerationStructureBuildSettings settings = {});

    const std::vector<RaytracingDemoSceneObject>& GetSceneObjects() const { return m_GeometryResources.GetObjects(); }
    const std::vector<RaytracingDemoSceneGeometry>& GetSceneGeometries() const { return m_GeometryResources.GetGeometries(); }
    const std::vector<RaytracingDemoMaterialData>& GetMaterials() const { return m_TextureMaterialResources.GetMaterials(); }
    SurfaceEmitterSceneData CollectEmissiveMeshSurfaceEmitters() const;
    const std::vector<std::shared_ptr<Texture>>& GetTextures() const { return m_TextureMaterialResources.GetTextures(); }
    const std::vector<ShaderResourceView>& GetTextureShaderResourceViews() const { return m_TextureMaterialResources.GetTextureShaderResourceViews(); }
    const StructuredBuffer& GetMaterialBuffer() const { return m_TextureMaterialResources.GetMaterialBuffer(); }
    const StructuredBuffer& GetGeometryBuffer() const { return m_RayTracingResources.GetGeometryBuffer(); }
    void ForEachGBufferShaderResource(const std::function<void(const Resource&)>& action) const;
    void ForEachRayTracingShaderResource(const std::function<void(const Resource&)>& action) const;
    BindlessDescriptorHeap& GetBindlessDescriptorHeap() { return m_TextureMaterialResources.GetBindlessDescriptorHeap(); }
    const BindlessDescriptorHeap& GetBindlessDescriptorHeap() const { return m_TextureMaterialResources.GetBindlessDescriptorHeap(); }
    MeshletGpuResources GetMeshletGpuResources() { return m_MeshletResources.GetGpuResources(); }
    const RayTracingAccelerationStructure& GetRayTracingAccelerationStructure() const { return m_RayTracingResources.GetAccelerationStructure(); }
    RayTracingAccelerationStructure& GetRayTracingAccelerationStructure() { return m_RayTracingResources.GetAccelerationStructure(); }
    size_t GetTextureCount() const { return m_TextureMaterialResources.GetTextures().size(); }
    size_t GetTextureCapacity() const { return m_TextureMaterialResources.GetTextures().capacity(); }

private:
    uint32_t AddTexture(CommandList& commandList, const std::wstring& path, TextureUsageType usage = TextureUsageType::Albedo);
    uint32_t AddMaterial(const RaytracingDemoMaterialData& material);
    uint32_t AddPbrMaterial(
        const DirectX::XMFLOAT4& diffuse,
        const DirectX::XMFLOAT4& tilingOffset,
        uint32_t diffuseTextureIndex,
        uint32_t normalTextureIndex,
        uint32_t metallicTextureIndex,
        uint32_t roughnessTextureIndex,
        uint32_t ambientOcclusionTextureIndex,
        float metallic = 0.0f,
        float roughness = 0.5f,
        bool hasDiffuseMap = true,
        bool hasNormalMap = false,
        bool hasMetallicMap = false,
        bool hasRoughnessMap = false,
        bool hasAmbientOcclusionMap = false,
        const DirectX::XMFLOAT4& emission = { 0.0f, 0.0f, 0.0f, 1.0f },
        uint32_t emissionTextureIndex = 0,
        bool hasEmissionMap = false);
    uint32_t AddDiffuseMaterial(
        const DirectX::XMFLOAT4& diffuse,
        const DirectX::XMFLOAT4& tilingOffset,
        uint32_t diffuseTextureIndex,
        float metallic = 0.0f,
        float roughness = 0.5f);
    std::vector<uint32_t> LoadSceneMaterials(CommandList& commandList, const Scene& scene, uint32_t whiteTexture);
    void LoadSceneObjects(
        CommandList& commandList,
        const Scene& scene,
        const std::vector<uint32_t>& materialIndexMap,
        uint32_t defaultMaterial);
    uint32_t AddSceneGeometry(const std::shared_ptr<Model>& model, std::vector<MeshPrototype> prototypes);
    void AddSceneObject(const DirectX::XMMATRIX& worldMatrix, uint32_t geometryIndex, uint32_t materialIndex);
    void InitializeMeshletSceneResources();
    void AddStressTestSpheres(CommandList& commandList, uint32_t whiteTextureIndex);
    void AddDynamicSceneAutomationEmitter(CommandList& commandList, uint32_t whiteTextureIndex);
    void UploadMeshletBuffers(CommandList& commandList);
    void InitializeDynamicRayTracingUpdateTarget();
    SceneTextureMaterialResources m_TextureMaterialResources;
    SceneGeometryResources m_GeometryResources;
    SceneMeshletResources m_MeshletResources;
    SceneRayTracingResources m_RayTracingResources;
    std::vector<RaytracingDemoSceneObject> m_StressTestSphereObjects;
    size_t m_StressTestSphereObjectStart = 0;
    uint32_t m_StressTestSphereMaterialIndex = (std::numeric_limits<uint32_t>::max)();
    bool m_StressTestSpheresEnabled = true;
    bool m_DynamicRayTracingUpdatesEnabled = false;
    bool m_DynamicRayTracingRestorePending = false;
    bool m_DynamicRayTracingUpdatePending = false;
    size_t m_DynamicRayTracingObjectIndex = (std::numeric_limits<size_t>::max)();
    uint32_t m_DynamicRayTracingGeometryIndex = (std::numeric_limits<uint32_t>::max)();
    uint32_t m_DynamicRayTracingPrototypeIndex = (std::numeric_limits<uint32_t>::max)();
    std::shared_ptr<Mesh> m_DynamicRayTracingMesh;
    bool m_DynamicRayTracingEmitterActive = false;
    DirectX::XMMATRIX m_DynamicRayTracingBaseWorldMatrix = DirectX::XMMatrixIdentity();
    VertexCollectionType m_DynamicRayTracingBaseVertices;
    VertexCollectionType m_DynamicRayTracingVertices;
    RaytracingDemoDynamicRtasUpdateStatistics m_DynamicRayTracingUpdateStatistics;
    RaytracingDemoDynamicSceneCapabilities m_DynamicSceneCapabilities;
};
//Modify End
