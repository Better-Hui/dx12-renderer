#pragma once
//Modify Begin:2026-07-30 by Hui

#include <DirectXMath.h>

#include <d3d12.h>
#include <wrl.h>

#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

class CommandList;
class D3D12DeviceContext;
class Mesh;
class ResourceStateRegistration;

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

struct RayTracingAccelerationStructureUpdateStatistics
{
    uint64_t FullBuildCount = 0;
    uint64_t BottomLevelBuildCount = 0;
    uint64_t BottomLevelUpdateCount = 0;
    uint64_t TopLevelBuildCount = 0;
    uint64_t TopLevelUpdateCount = 0;
    uint64_t RetiredResourceCount = 0;
};

class RayTracingAccelerationStructure
{
public:
    explicit RayTracingAccelerationStructure(std::shared_ptr<D3D12DeviceContext> deviceContext);

    RayTracingInstanceHandle AddInstance(const RayTracingInstanceDesc& instanceDesc);
    bool UpdateInstance(RayTracingInstanceHandle handle, const RayTracingInstanceDesc& instanceDesc);
    void MarkBottomLevelGeometryDirty(std::span<const std::shared_ptr<Mesh>> meshes);
    bool RemoveInstance(RayTracingInstanceHandle handle);
    void RemoveInstances(std::span<const RayTracingInstanceHandle> handles);
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
    const RayTracingAccelerationStructureUpdateStatistics& GetUpdateStatistics() const
    {
        return m_UpdateStatistics;
    }

private:
    struct ManagedRayTracingResource
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> Resource;
        std::shared_ptr<ResourceStateRegistration> StateRegistration;

        explicit operator bool() const
        {
            return Resource != nullptr;
        }
    };

    struct BottomLevelAccelerationStructure
    {
        std::shared_ptr<Mesh> Mesh;
        ManagedRayTracingResource Resource;
    };

    ManagedRayTracingResource CreateAccelerationStructureBuffer(
        uint64_t size,
        D3D12_RESOURCE_STATES initialState,
        const wchar_t* name) const;

    ManagedRayTracingResource CreateUploadBuffer(
        const void* data,
        uint64_t size,
        const wchar_t* name) const;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO GetBottomLevelPrebuildInfo(
        const Mesh& mesh,
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags) const;

    BottomLevelAccelerationStructure BuildBottomLevelAccelerationStructure(
        CommandList& commandList,
        const std::shared_ptr<Mesh>& mesh,
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags,
        const ManagedRayTracingResource& scratch);

    std::map<const Mesh*, uint32_t> BuildBottomLevelAccelerationStructures(CommandList& commandList);
    void UpdateDirtyBottomLevelAccelerationStructures(CommandList& commandList);
    std::map<const Mesh*, uint32_t> CreateMeshToBlasIndex() const;

    void BuildTopLevelAccelerationStructure(
        CommandList& commandList,
        const std::map<const Mesh*, uint32_t>& meshToBlasIndex,
        bool update);
    void RetireResourceState(
        CommandList& commandList,
        const Microsoft::WRL::ComPtr<ID3D12Resource>& resource);

    RayTracingInstanceHandle m_NextInstanceHandle = 1;
    std::vector<RayTracingInstanceHandle> m_InstanceHandles;
    std::vector<RayTracingInstanceDesc> m_Instances;
    std::unordered_map<RayTracingInstanceHandle, uint32_t> m_InstanceIndices;
    std::vector<BottomLevelAccelerationStructure> m_BottomLevelAccelerationStructures;
    ManagedRayTracingResource m_TopLevelAccelerationStructure;
    ManagedRayTracingResource m_InstanceDescUpload;
    std::vector<std::shared_ptr<Mesh>> m_Meshes;
    std::vector<RayTracingGeometryData> m_GeometryData;
    std::unordered_set<const Mesh*> m_DirtyBottomLevelMeshes;
    RayTracingAccelerationStructureBuildSettings m_LastBuildSettings;
    RayTracingAccelerationStructureUpdateStatistics m_UpdateStatistics;
    uint32_t m_BuiltInstanceCount = 0;
    std::shared_ptr<D3D12DeviceContext> m_DeviceContext;
    Microsoft::WRL::ComPtr<ID3D12Device5> m_Device;
    bool m_InstanceMeshChanged = false;
};
//Modify End
