//Modify Begin:2026-07-27 by BestHui
#pragma once

#include <DX12Library/StructuredBuffer.h>
#include <DX12Library/Texture.h>

#include <Framework/RayTracingAccelerationStructure.h>
#include <Framework/Scene.h>

#include <DirectXMath.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class CommandList;
class Model;

struct RaytracingDemoMaterialData
{
    DirectX::XMFLOAT4 Diffuse = { 1.0f, 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT4 Specular = { 0.04f, 0.04f, 0.04f, 1.0f };
    DirectX::XMFLOAT4 TilingOffset = { 1.0f, 1.0f, 0.0f, 0.0f };
    uint32_t DiffuseTextureIndex = 0;
    uint32_t NormalTextureIndex = 0;
    uint32_t MetallicTextureIndex = 0;
    uint32_t RoughnessTextureIndex = 0;
    uint32_t AmbientOcclusionTextureIndex = 0;
    uint32_t HasDiffuseMap = 0;
    uint32_t HasNormalMap = 0;
    uint32_t HasMetallicMap = 0;
    uint32_t HasRoughnessMap = 0;
    uint32_t HasAmbientOcclusionMap = 0;
    float Metallic = 0.0f;
    float Roughness = 0.5f;
    uint32_t Padding0 = 0;
    uint32_t Padding1 = 0;
};

struct RaytracingDemoSceneObject
{
    DirectX::XMMATRIX WorldMatrix = DirectX::XMMatrixIdentity();
    std::shared_ptr<Model> Model;
    uint32_t MaterialIndex = 0;
};

class RaytracingDemoSceneResources final
{
public:
    RaytracingDemoSceneResources();

    void Clear();
    void LoadDeferredLightingScene(CommandList& commandList);
    bool LoadScene(CommandList& commandList, const Scene& scene);
    void BuildRayTracingAccelerationStructure(
        CommandList& commandList,
        RayTracingAccelerationStructureBuildSettings settings = {});

    const std::vector<RaytracingDemoSceneObject>& GetSceneObjects() const { return m_SceneObjects; }
    const std::vector<RaytracingDemoMaterialData>& GetMaterials() const { return m_Materials; }
    const std::vector<std::shared_ptr<Texture>>& GetTextures() const { return m_Textures; }
    const StructuredBuffer& GetMaterialBuffer() const { return m_MaterialBuffer; }
    const StructuredBuffer& GetGeometryBuffer() const { return m_GeometryBuffer; }
    const RayTracingAccelerationStructure& GetRayTracingAccelerationStructure() const { return m_RayTracingAccelerationStructure; }
    RayTracingAccelerationStructure& GetRayTracingAccelerationStructure() { return m_RayTracingAccelerationStructure; }
    size_t GetTextureCount() const { return m_Textures.size(); }
    size_t GetTextureCapacity() const { return m_Textures.capacity(); }

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
    void AddRayTracingInstances(RayTracingAccelerationStructure& accelerationStructure) const;
    void UploadRayTracingBuffers(CommandList& commandList, const RayTracingAccelerationStructure& accelerationStructure);

    StructuredBuffer m_MaterialBuffer;
    StructuredBuffer m_GeometryBuffer;
    RayTracingAccelerationStructure m_RayTracingAccelerationStructure;
    std::vector<RaytracingDemoSceneObject> m_SceneObjects;
    std::vector<RaytracingDemoMaterialData> m_Materials;
    std::vector<std::shared_ptr<Texture>> m_Textures;
};
//Modify End
