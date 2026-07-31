#pragma once

//Modify Begin:2026-07-30 by BestHui

#include <DX12Library/ByteAddressBuffer.h>
#include <DX12Library/StructuredBuffer.h>
#include <Framework/Geometry/Mesh.h>

#include <DirectXMath.h>
#include <d3d12.h>

#include <cstdint>
#include <utility>
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

//Modify Begin:2026-07-31 by BestHui
struct MeshletDraw
{
    DirectX::XMMATRIX WorldMatrix = DirectX::XMMatrixIdentity();
    uint32_t MaterialIndex = 0;
    uint32_t MeshletOffset = 0;
    uint32_t MeshletCount = 0;
    uint32_t Padding0 = 0;
};

struct MeshletTransformData
{
    DirectX::XMMATRIX Model = DirectX::XMMatrixIdentity();
    DirectX::XMMATRIX InverseTransposeModel = DirectX::XMMatrixIdentity();
};

struct MeshletInstanceData
{
    uint32_t MeshletIndex = 0;
    uint32_t TransformIndex = 0;
    uint32_t MaterialIndex = 0;
    uint32_t Padding0 = 0;
};

struct MeshletIndirectCommand
{
    uint32_t MeshletInstanceIndex = 0;
    uint32_t Flags = 0;
    uint32_t Padding0 = 0;
    uint32_t Padding1 = 0;
    D3D12_DRAW_ARGUMENTS DrawArguments = {};
};

struct MeshletGpuResources
{
    const StructuredBuffer* Vertices = nullptr;
    const ByteAddressBuffer* Indices = nullptr;
    const StructuredBuffer* Meshlets = nullptr;
    const StructuredBuffer* Transforms = nullptr;
    const StructuredBuffer* Instances = nullptr;
    StructuredBuffer* IndirectCommands = nullptr;
    uint32_t InstanceCount = 0;

    bool IsValid() const
    {
        return Vertices != nullptr &&
            Indices != nullptr &&
            Meshlets != nullptr &&
            Transforms != nullptr &&
            Instances != nullptr &&
            IndirectCommands != nullptr &&
            InstanceCount > 0;
    }
};

class CommandList;

class MeshletGeometrySet
{
public:
    MeshletGeometrySet();

    void Clear();
    std::pair<uint32_t, uint32_t> AddGeometry(const MeshPrototype& prototype);
    void AddDraw(const MeshPrototype& prototype, const DirectX::XMMATRIX& worldMatrix, uint32_t materialIndex);
    void AddDraw(uint32_t meshletOffset, uint32_t meshletCount, const DirectX::XMMATRIX& worldMatrix, uint32_t materialIndex);
    void Upload(CommandList& commandList);

    MeshletGpuResources GetGpuResources();

private:
    void BuildInstances();

    StructuredBuffer m_VertexBuffer;
    ByteAddressBuffer m_IndexBuffer;
    StructuredBuffer m_MeshletBuffer;
    StructuredBuffer m_TransformBuffer;
    StructuredBuffer m_InstanceBuffer;
    StructuredBuffer m_IndirectCommandBuffer;

    std::vector<VertexAttributes> m_Vertices;
    std::vector<uint16_t> m_Indices;
    std::vector<Meshlet> m_Meshlets;
    std::vector<MeshletDraw> m_Draws;
    std::vector<MeshletTransformData> m_Transforms;
    std::vector<MeshletInstanceData> m_Instances;
};
//Modify End

//Modify End
