#pragma once

//Modify Begin:2026-08-25 by Hui

#include <DX12Library/ByteAddressBuffer.h>
#include <DX12Library/StructuredBuffer.h>
#include <Framework/Geometry/Mesh.h>

#include <DirectXMath.h>
#include <d3d12.h>

#include <cstdint>
#include <span>
#include <unordered_map>
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
    std::vector<uint32_t> CompactToSourceVertexIndices;
};

class MeshletBuilder
{
public:
    static MeshletBuildResult Build(const MeshPrototype& meshPrototype, const MeshletBuildOptions& options = {});
};

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
    void ClearDraws();
    std::pair<uint32_t, uint32_t> AddGeometry(const MeshPrototype& prototype);
    void AddDraw(const MeshPrototype& prototype, const DirectX::XMMATRIX& worldMatrix, uint32_t materialIndex);
    void AddDraw(uint32_t meshletOffset, uint32_t meshletCount, const DirectX::XMMATRIX& worldMatrix, uint32_t materialIndex);
    bool UpdateGeometryVertices(
        uint32_t meshletOffset,
        uint32_t meshletCount,
        std::span<const VertexAttributes> sourceVertices);
    void Upload(CommandList& commandList);

    MeshletGpuResources GetGpuResources();

private:
    struct GeometryEntry
    {
        uint32_t MeshletOffset = 0;
        uint32_t MeshletCount = 0;
        uint32_t VertexOffset = 0;
        std::vector<uint32_t> CompactToSourceVertexIndices;
    };

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
    std::vector<GeometryEntry> m_GeometryEntries;
    std::vector<MeshletDraw> m_Draws;
    std::vector<MeshletTransformData> m_Transforms;
    std::vector<MeshletInstanceData> m_Instances;
    bool m_GeometryDataDirty = true;
    bool m_IndexDataDirty = true;
    bool m_InstanceDataDirty = true;
};

struct MeshletSceneGeometrySource
{
    const std::vector<MeshPrototype>* MeshPrototypes = nullptr;
};

struct MeshletSceneInstanceSource
{
    DirectX::XMMATRIX WorldMatrix = DirectX::XMMatrixIdentity();
    uint32_t GeometryIndex = 0;
    uint32_t MaterialIndex = 0;
};

using MeshletSceneInstanceHandle = uint64_t;

struct MeshletSceneUpdateStatistics
{
    uint64_t InstanceUpdateCount = 0;
    uint64_t GeometryUpdateCount = 0;
};

class MeshletSceneResources final
{
public:
    void Clear();
    void InitializeGeometries(const std::vector<MeshletSceneGeometrySource>& geometries);
    void Rebuild(
        const std::vector<MeshletSceneGeometrySource>& geometries,
        const std::vector<MeshletSceneInstanceSource>& instances);
    MeshletSceneInstanceHandle AddInstance(const MeshletSceneInstanceSource& instance);
    bool UpdateInstance(MeshletSceneInstanceHandle handle, const MeshletSceneInstanceSource& instance);
    bool UpdateGeometryVertices(
        uint32_t geometryIndex,
        uint32_t prototypeIndex,
        std::span<const VertexAttributes> sourceVertices);
    bool RemoveInstance(MeshletSceneInstanceHandle handle);
    void RemoveInstances(std::span<const MeshletSceneInstanceHandle> handles);
    void Upload(CommandList& commandList);

    MeshletGpuResources GetGpuResources();
    const MeshletSceneUpdateStatistics& GetUpdateStatistics() const { return m_UpdateStatistics; }

private:
    struct InstanceEntry
    {
        MeshletSceneInstanceHandle Handle = 0;
        MeshletSceneInstanceSource Source;
    };

    void RebuildDraws();

    MeshletGeometrySet m_GeometrySet;
    std::vector<std::vector<std::pair<uint32_t, uint32_t>>> m_GeometryMeshletRanges;
    std::vector<InstanceEntry> m_Instances;
    std::unordered_map<MeshletSceneInstanceHandle, uint32_t> m_InstanceIndices;
    MeshletSceneInstanceHandle m_NextInstanceHandle = 1;
    MeshletSceneUpdateStatistics m_UpdateStatistics;
    bool m_DrawsDirty = true;
};

//Modify End
