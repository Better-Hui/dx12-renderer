//Modify Begin:2026-07-30 by BestHui
#pragma once

#include <Framework/Geometry/Mesh.h>

#include <DirectXMath.h>

#include <cstdint>
#include <memory>
#include <vector>

class Model;

struct RaytracingDemoMaterialData
{
    DirectX::XMFLOAT4 Diffuse = { 1.0f, 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT4 Specular = { 0.04f, 0.04f, 0.04f, 1.0f };
    DirectX::XMFLOAT4 Emission = { 0.0f, 0.0f, 0.0f, 1.0f };
    DirectX::XMFLOAT4 TilingOffset = { 1.0f, 1.0f, 0.0f, 0.0f };
    uint32_t DiffuseTextureIndex = 0;
    uint32_t NormalTextureIndex = 0;
    uint32_t MetallicTextureIndex = 0;
    uint32_t RoughnessTextureIndex = 0;
    uint32_t AmbientOcclusionTextureIndex = 0;
    uint32_t EmissionTextureIndex = 0;
    uint32_t HasDiffuseMap = 0;
    uint32_t HasNormalMap = 0;
    uint32_t HasMetallicMap = 0;
    uint32_t HasRoughnessMap = 0;
    uint32_t HasAmbientOcclusionMap = 0;
    uint32_t HasEmissionMap = 0;
    float Metallic = 0.0f;
    float Roughness = 0.5f;
    uint32_t Padding0 = 0;
    uint32_t Padding1 = 0;
};

struct RaytracingDemoSceneGeometry
{
    std::shared_ptr<Model> Model;
    std::vector<MeshPrototype> MeshPrototypes;
};

struct RaytracingDemoSceneObject
{
    DirectX::XMMATRIX WorldMatrix = DirectX::XMMatrixIdentity();
    uint32_t GeometryIndex = 0;
    uint32_t MaterialIndex = 0;
};
//Modify End
