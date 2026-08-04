//Modify Begin:2026-07-27 by BestHui
#pragma once

#include <Scene/SceneResourceBuilders.h>
#include <Framework/Scene/Scene.h>

#include <DirectXMath.h>
#include <d3d12.h>
#include <wrl.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class CommandList;
class RaytracingDemoSceneResources final
{
public:
    explicit RaytracingDemoSceneResources(Microsoft::WRL::ComPtr<ID3D12Device2> device);

    void Clear();
    void LoadDeferredLightingScene(CommandList& commandList);
//Modify Begin:2026-07-30 by BestHui
    bool LoadScene(CommandList& commandList, const Scene& scene, bool enableStressTestSpheres = true);
    bool SetStressTestSpheresEnabled(CommandList& commandList, bool enabled);
    bool AreStressTestSpheresEnabled() const { return m_StressTestSpheresEnabled; }
//Modify End
    void BuildRayTracingAccelerationStructure(
        CommandList& commandList,
        RayTracingAccelerationStructureBuildSettings settings = {});

    const std::vector<RaytracingDemoSceneObject>& GetSceneObjects() const { return m_GeometryResources.GetObjects(); }
    const std::vector<RaytracingDemoSceneGeometry>& GetSceneGeometries() const { return m_GeometryResources.GetGeometries(); }
    const std::vector<RaytracingDemoMaterialData>& GetMaterials() const { return m_TextureMaterialResources.GetMaterials(); }
    const std::vector<std::shared_ptr<Texture>>& GetTextures() const { return m_TextureMaterialResources.GetTextures(); }
    std::vector<ShaderResourceView> CreateTextureShaderResourceViews() const { return m_TextureMaterialResources.CreateTextureShaderResourceViews(); }
    const StructuredBuffer& GetMaterialBuffer() const { return m_TextureMaterialResources.GetMaterialBuffer(); }
    const StructuredBuffer& GetGeometryBuffer() const { return m_RayTracingResources.GetGeometryBuffer(); }
    void TransitionRayTracingShaderResources(CommandList& commandList, D3D12_RESOURCE_STATES stateAfter) const;
//Modify Begin:2026-07-30 by BestHui
    BindlessDescriptorHeap& GetBindlessDescriptorHeap() { return m_TextureMaterialResources.GetBindlessDescriptorHeap(); }
    const BindlessDescriptorHeap& GetBindlessDescriptorHeap() const { return m_TextureMaterialResources.GetBindlessDescriptorHeap(); }
//Modify End
//Modify Begin:2026-07-31 by BestHui
//Modify Begin:2026-07-30 by BestHui
    MeshletGpuResources GetMeshletGpuResources() { return m_MeshletResources.GetGpuResources(); }
//Modify End
//Modify End
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
        bool hasAmbientOcclusionMap = false);
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
//Modify Begin:2026-07-31 by BestHui
    uint32_t AddSceneGeometry(const std::shared_ptr<Model>& model, std::vector<MeshPrototype> prototypes);
    void AddSceneObject(const DirectX::XMMATRIX& worldMatrix, uint32_t geometryIndex, uint32_t materialIndex);
//Modify Begin:2026-07-30 by BestHui
    void InitializeMeshletSceneResources();
//Modify End
    void AddStressTestSpheres(CommandList& commandList, uint32_t whiteTextureIndex);
    void UploadMeshletBuffers(CommandList& commandList);
//Modify End
    SceneTextureMaterialResources m_TextureMaterialResources;
    SceneGeometryResources m_GeometryResources;
    SceneMeshletResources m_MeshletResources;
    SceneRayTracingResources m_RayTracingResources;
//Modify Begin:2026-07-30 by BestHui
    std::vector<RaytracingDemoSceneObject> m_StressTestSphereObjects;
    size_t m_StressTestSphereObjectStart = 0;
    bool m_StressTestSpheresEnabled = true;
//Modify End
};
//Modify End
