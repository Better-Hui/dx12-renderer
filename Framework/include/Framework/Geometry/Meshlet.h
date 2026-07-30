#pragma once

//Modify Begin:2026-07-30 by BestHui

#include <Framework/Geometry/Mesh.h>

#include <DirectXMath.h>

#include <cstdint>
#include <vector>

struct MeshletBounds
{
    DirectX::XMFLOAT3 Center = { 0.0f, 0.0f, 0.0f };
    float Radius = 0.0f;
    DirectX::XMFLOAT3 ConeApex = { 0.0f, 0.0f, 0.0f };
    float ConeCutoff = 0.0f;
    DirectX::XMFLOAT3 ConeAxis = { 0.0f, 0.0f, 1.0f };
    float Padding0 = 0.0f;
    DirectX::XMFLOAT3 AabbCenter = { 0.0f, 0.0f, 0.0f };
    float Padding1 = 0.0f;
    DirectX::XMFLOAT3 AabbHalfSize = { 0.0f, 0.0f, 0.0f };
    float Padding2 = 0.0f;
};

struct Meshlet
{
    MeshletBounds Bounds;
    uint32_t VertexOffset = 0;
    uint32_t VertexCount = 0;
    uint32_t IndexOffset = 0;
    uint32_t IndexCount = 0;
    uint32_t TransformIndex = 0;
    uint32_t MaterialIndex = 0;
    uint32_t VertexBufferIndex = 0;
    uint32_t IndexBufferIndex = 0;
};

struct MeshletBuildOptions
{
    size_t MaxVertices = 64;
    size_t MaxTriangles = 124;
    float ConeWeight = 0.5f;
};

struct MeshletBuildResult
{
    MeshPrototype Mesh;
    std::vector<Meshlet> Meshlets;
};

class MeshletBuilder
{
public:
    static MeshletBuildResult Build(const MeshPrototype& meshPrototype, const MeshletBuildOptions& options = {});
};

//Modify End
