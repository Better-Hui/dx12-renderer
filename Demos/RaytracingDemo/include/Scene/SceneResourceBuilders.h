//Modify Begin:2026-08-21 by Hui
#pragma once

#include <Scene/SceneResourceTypes.h>
#include <Framework/Scene/Scene.h>

#include <DX12Library/StructuredBuffer.h>
#include <DX12Library/Texture.h>

#include <Framework/Geometry/Meshlet.h>
#include <Framework/Rendering/Pipeline/BindlessDescriptorHeap.h>
#include <Framework/Rendering/RayTracing/RayTracingAccelerationStructure.h>
#include <Framework/Rendering/Texture/ShaderResourceView.h>

#include <d3d12.h>
#include <wrl.h>

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <memory>
#include <span>
#include <string>
#include <vector>

class CommandList;
class D3D12DeviceContext;
class Model;
class Resource;

class SceneTextureMaterialResources final
{
public:
    explicit SceneTextureMaterialResources(Microsoft::WRL::ComPtr<ID3D12Device2> device);

    void Clear();
    uint32_t AddTexture(CommandList& commandList, const std::wstring& path, TextureUsageType usage = TextureUsageType::Albedo);
    uint32_t AddTexture(CommandList& commandList, const SceneTextureBinding& binding, TextureUsageType usage = TextureUsageType::Albedo);
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

    void UploadMaterialBuffer(CommandList& commandList);
    void ForEachBindlessTexture(const std::function<void(const Resource&)>& action) const;
    void ForEachShaderResource(const std::function<void(const Resource&)>& action) const;
    const std::vector<ShaderResourceView>& GetTextureShaderResourceViews() const;

    const std::vector<RaytracingDemoMaterialData>& GetMaterials() const { return m_Materials; }
    const std::vector<std::shared_ptr<Texture>>& GetTextures() const { return m_Textures; }
    const StructuredBuffer& GetMaterialBuffer() const { return m_MaterialBuffer; }
    BindlessDescriptorHeap& GetBindlessDescriptorHeap() { return m_BindlessDescriptorHeap; }
    const BindlessDescriptorHeap& GetBindlessDescriptorHeap() const { return m_BindlessDescriptorHeap; }

private:
    std::wstring BuildTextureCacheKey(const std::wstring& path, TextureUsageType usage) const;

    StructuredBuffer m_MaterialBuffer;
    BindlessDescriptorHeap m_BindlessDescriptorHeap;
    std::vector<RaytracingDemoMaterialData> m_Materials;
    std::vector<std::shared_ptr<Texture>> m_Textures;
    std::vector<ShaderResourceView> m_TextureShaderResourceViews;
    std::unordered_map<std::wstring, uint32_t> m_TextureIndices;
};

class SceneGeometryResources final
{
public:
    void Clear();
    uint32_t AddGeometry(const std::shared_ptr<Model>& model, std::vector<MeshPrototype> prototypes);
    void AddObject(const DirectX::XMMATRIX& worldMatrix, uint32_t geometryIndex, uint32_t materialIndex);
    void AppendObjects(std::span<const RaytracingDemoSceneObject> objects);
    void ResizeObjects(size_t count);

    const std::vector<RaytracingDemoSceneGeometry>& GetGeometries() const { return m_Geometries; }
    const std::vector<RaytracingDemoSceneObject>& GetObjects() const { return m_Objects; }

private:
    std::vector<RaytracingDemoSceneGeometry> m_Geometries;
    std::vector<RaytracingDemoSceneObject> m_Objects;
};

class SceneMeshletResources final
{
public:
    void Clear();
    void Initialize(
        const std::vector<RaytracingDemoSceneGeometry>& geometries,
        const std::vector<RaytracingDemoSceneObject>& objects,
        size_t stressObjectStart,
        size_t stressObjectCount,
        bool stressObjectsEnabled);
    void AddStressInstances(std::span<const RaytracingDemoSceneObject> objects);
    void RemoveStressInstances();
    void Upload(CommandList& commandList);
    MeshletGpuResources GetGpuResources() { return m_Resources.GetGpuResources(); }

private:
    MeshletSceneResources m_Resources;
    std::vector<MeshletSceneInstanceHandle> m_StressInstanceHandles;
};

class SceneRayTracingResources final
{
public:
    explicit SceneRayTracingResources(std::shared_ptr<D3D12DeviceContext> deviceContext);

    void Clear();
    void Build(
        CommandList& commandList,
        const std::vector<RaytracingDemoSceneGeometry>& geometries,
        const std::vector<RaytracingDemoSceneObject>& objects,
        size_t stressObjectStart,
        size_t stressObjectCount,
        bool stressObjectsEnabled,
        BindlessDescriptorHeap& bindlessDescriptorHeap,
        RayTracingAccelerationStructureBuildSettings settings);
    void AddStressInstances(
        const std::vector<RaytracingDemoSceneGeometry>& geometries,
        std::span<const RaytracingDemoSceneObject> objects);
    void RemoveStressInstances();
    void Update(CommandList& commandList, BindlessDescriptorHeap& bindlessDescriptorHeap);
    void ForEachShaderResource(const std::function<void(const Resource&)>& action) const;

    const StructuredBuffer& GetGeometryBuffer() const { return m_GeometryBuffer; }
    const RayTracingAccelerationStructure& GetAccelerationStructure() const { return m_AccelerationStructure; }
    RayTracingAccelerationStructure& GetAccelerationStructure() { return m_AccelerationStructure; }

private:
    void AddObjectInstances(
        const std::vector<RaytracingDemoSceneGeometry>& geometries,
        const RaytracingDemoSceneObject& object,
        std::vector<RayTracingInstanceHandle>* instanceHandles = nullptr);
    void UploadGeometryBuffer(CommandList& commandList, BindlessDescriptorHeap& bindlessDescriptorHeap);

    StructuredBuffer m_GeometryBuffer;
    RayTracingAccelerationStructure m_AccelerationStructure;
    std::vector<RayTracingInstanceHandle> m_StressInstanceHandles;
};
//Modify End
