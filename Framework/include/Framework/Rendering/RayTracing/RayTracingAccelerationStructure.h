#pragma once
//Modify Begin:2026-07-21 by BestHui

#include <DirectXMath.h>

#include <d3d12.h>
#include <wrl.h>

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

class CommandList;
class Mesh;

using RayTracingInstanceHandle = uint64_t;

struct RayTracingGeometryData
{
    uint32_t VertexBufferIndex = 0;
    uint32_t IndexBufferIndex = 0;
    uint32_t MaterialIndex = 0;
    uint32_t Padding = 0;
};

struct RayTracingMeshInstance
{
    std::shared_ptr<Mesh> Mesh;
    DirectX::XMMATRIX Transform = DirectX::XMMatrixIdentity();
    uint32_t MaterialIndex = 0;
};

struct RayTracingInstanceDesc
{
    std::shared_ptr<Mesh> Mesh;
    DirectX::XMMATRIX Transform = DirectX::XMMatrixIdentity();
    uint32_t MaterialIndex = 0;
    uint32_t InstanceID = UINT32_MAX;
    uint32_t InstanceContributionToHitGroupIndex = 0;
    uint8_t Mask = 0xFF;
    D3D12_RAYTRACING_INSTANCE_FLAGS Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
};

struct RayTracingAccelerationStructureBuildSettings
{
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS BottomLevelFlags =
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS TopLevelFlags =
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    bool AllowUpdate = false;
};

class RayTracingAccelerationStructure
{
public:
    RayTracingInstanceHandle AddInstance(const RayTracingInstanceDesc& instanceDesc);
    bool UpdateInstance(RayTracingInstanceHandle handle, const RayTracingInstanceDesc& instanceDesc);
    bool RemoveInstance(RayTracingInstanceHandle handle);
    void ClearInstances();

    void Build(CommandList& commandList, RayTracingAccelerationStructureBuildSettings settings = {});
    void Build(CommandList& commandList, const std::vector<RayTracingMeshInstance>& instances);
    void Update(CommandList& commandList);

    bool IsBuilt() const;
    D3D12_GPU_VIRTUAL_ADDRESS GetGpuVirtualAddress() const;
    const std::vector<std::shared_ptr<Mesh>>& GetMeshes() const;
    const std::vector<RayTracingGeometryData>& GetGeometryData() const;
    const std::vector<RayTracingInstanceDesc>& GetInstances() const;
    uint32_t GetInstanceCount() const;

private:
    struct BottomLevelAccelerationStructure
    {
        std::shared_ptr<Mesh> Mesh;
        Microsoft::WRL::ComPtr<ID3D12Resource> Resource;
    };

    Microsoft::WRL::ComPtr<ID3D12Resource> CreateAccelerationStructureBuffer(
        uint64_t size,
        D3D12_RESOURCE_STATES initialState,
        const wchar_t* name) const;

    Microsoft::WRL::ComPtr<ID3D12Resource> CreateUploadBuffer(
        const void* data,
        uint64_t size,
        const wchar_t* name) const;

    BottomLevelAccelerationStructure BuildBottomLevelAccelerationStructure(
        CommandList& commandList,
        const std::shared_ptr<Mesh>& mesh,
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags) const;

    std::map<const Mesh*, uint32_t> BuildBottomLevelAccelerationStructures(CommandList& commandList);
    std::map<const Mesh*, uint32_t> CreateMeshToBlasIndex() const;

    void BuildTopLevelAccelerationStructure(
        CommandList& commandList,
        const std::map<const Mesh*, uint32_t>& meshToBlasIndex,
        bool update);

    RayTracingInstanceHandle m_NextInstanceHandle = 1;
    std::vector<RayTracingInstanceHandle> m_InstanceHandles;
    std::vector<RayTracingInstanceDesc> m_Instances;
    std::vector<BottomLevelAccelerationStructure> m_BottomLevelAccelerationStructures;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_TopLevelAccelerationStructure;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_InstanceDescUpload;
    std::vector<std::shared_ptr<Mesh>> m_Meshes;
    std::vector<RayTracingGeometryData> m_GeometryData;
    RayTracingAccelerationStructureBuildSettings m_LastBuildSettings;
    uint32_t m_BuiltInstanceCount = 0;
};
//Modify End
