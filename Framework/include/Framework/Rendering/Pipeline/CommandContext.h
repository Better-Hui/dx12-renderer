#pragma once

//Modify Begin:2026-08-19 by Hui

#include <d3d12.h>
#include <wrl.h>

#include <Framework/Rendering/Pipeline/CommandContextDescriptorAllocator.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

class CommandList;
class BindlessDescriptorHeap;
class ComputeShader;
class IndirectCommandSignature;
class MeshShader;
class PipelineDescriptorPool;
class PipelineDescriptorSet;
class PipelineLayout;
struct PipelineDescriptorRangeDesc;
struct PipelineBoundResource;
class RayTracingBindingSet;
class RayTracingAccelerationStructure;
class RayTracingShader;
class Resource;
class RootSignature;
class Shader;
struct ShaderResourceView;
class StructuredBuffer;
struct UnorderedAccessView;

enum class PipelineBindPoint
{
    Graphics,
    Compute,
    RayTracing
};

struct PipelineDescriptorSetBindDesc
{
    uint32_t SetIndex = 0;
    const PipelineDescriptorSet* DescriptorSet = nullptr;
};

struct RayTracingDispatchDesc
{
    std::string_view PassName;
    uint32_t Width = 1;
    uint32_t Height = 1;
    uint32_t Depth = 1;
};

struct IndirectCommandExecutionDesc
{
    const Resource* ArgumentBuffer = nullptr;
    uint32_t MaxCommandCount = 1;
    uint64_t ArgumentBufferOffset = 0;
    const Resource* CountBuffer = nullptr;
    uint64_t CountBufferOffset = 0;
};

class CommandContext final
{
public:
    static constexpr uint32_t MaxDescriptorSetSlots = 16;
    explicit CommandContext(CommandList& commandList);

    CommandList& GetCommandList() const { return m_CommandList; }

    void BindPipeline(Shader& shader) const;
    void BindPipeline(MeshShader& shader) const;
    void BindPipeline(const ComputeShader& shader) const;
    void BindPipeline(const RayTracingShader& shader) const;
    void BindBindlessDescriptorHeap(BindlessDescriptorHeap& bindlessDescriptorHeap) const;
    void BindDescriptorSet(const PipelineDescriptorSetBindDesc& descriptorSetDesc) const;
    void BindDescriptorSet(const PipelineDescriptorSet& descriptorSet) const;
    void SetConstantBuffer(Shader& shader, std::string_view name, size_t size, const void* data) const;

    template<typename T>
    void SetConstantBuffer(Shader& shader, std::string_view name, const T& data) const
    {
        SetConstantBuffer(shader, name, sizeof(T), &data);
    }

    void SetShaderResourceView(Shader& shader, std::string_view name, const ShaderResourceView& shaderResourceView) const;
    void SetShaderResourceView(Shader& shader, std::string_view name, uint32_t arrayIndex, const ShaderResourceView& shaderResourceView) const;
    void SetShaderResourceViews(Shader& shader, std::string_view name, std::span<const ShaderResourceView> shaderResourceViews) const;
    void SetShaderResource(Shader& shader, std::string_view name, const Resource& resource, D3D12_RESOURCE_STATES stateAfter = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE) const;
    void SetStructuredBuffer(Shader& shader, std::string_view name, const StructuredBuffer& buffer) const;
    void SetTexture(Shader& shader, std::string_view name, const ShaderResourceView& shaderResourceView) const;
    void SetTexture(Shader& shader, std::string_view name, const std::shared_ptr<Resource>& texture) const;

    void SetConstantBuffer(MeshShader& shader, std::string_view name, size_t size, const void* data) const;

    template<typename T>
    void SetConstantBuffer(MeshShader& shader, std::string_view name, const T& data) const
    {
        SetConstantBuffer(shader, name, sizeof(T), &data);
    }

    void SetShaderResourceView(MeshShader& shader, std::string_view name, const ShaderResourceView& shaderResourceView) const;
    void SetShaderResourceView(MeshShader& shader, std::string_view name, uint32_t arrayIndex, const ShaderResourceView& shaderResourceView) const;
    void SetShaderResourceViews(MeshShader& shader, std::string_view name, std::span<const ShaderResourceView> shaderResourceViews) const;
    void SetShaderResource(MeshShader& shader, std::string_view name, const Resource& resource, D3D12_RESOURCE_STATES stateAfter = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE) const;
    void SetStructuredBuffer(MeshShader& shader, std::string_view name, const StructuredBuffer& buffer) const;
    void SetTexture(MeshShader& shader, std::string_view name, const ShaderResourceView& shaderResourceView) const;
    void SetTexture(MeshShader& shader, std::string_view name, const std::shared_ptr<Resource>& texture) const;

    void SetStructuredBuffer(const ComputeShader& shader, std::string_view name, const StructuredBuffer& buffer) const;
    void SetConstantBuffer(const ComputeShader& shader, std::string_view name, size_t size, const void* data) const;

    template<typename T>
    void SetConstantBuffer(const ComputeShader& shader, std::string_view name, const T& data) const
    {
        SetConstantBuffer(shader, name, sizeof(T), &data);
    }

    void SetShaderResourceView(const ComputeShader& shader, std::string_view name, const ShaderResourceView& shaderResourceView) const;
    void SetShaderResourceView(const ComputeShader& shader, std::string_view name, uint32_t arrayIndex, const ShaderResourceView& shaderResourceView) const;
    void SetShaderResource(const ComputeShader& shader, std::string_view name, uint32_t arrayIndex, const Resource& resource, D3D12_RESOURCE_STATES stateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) const;
    void SetShaderResourceViews(const ComputeShader& shader, std::string_view name, std::span<const ShaderResourceView> shaderResourceViews) const;
    void SetTexture(const ComputeShader& shader, std::string_view name, const ShaderResourceView& shaderResourceView) const;
    void SetTexture(const ComputeShader& shader, std::string_view name, const std::shared_ptr<Resource>& texture) const;
    void SetUnorderedAccessView(const ComputeShader& shader, std::string_view name, const UnorderedAccessView& unorderedAccessView) const;
    void SetAccelerationStructure(const ComputeShader& shader, std::string_view name, const RayTracingAccelerationStructure& accelerationStructure) const;
    void InsertDescriptorSetOutputBarriers(const PipelineDescriptorSet& descriptorSet) const;

    void TransitionShaderResource(const Resource& resource) const;
    void TransitionUnorderedAccess(const Resource& resource) const;
    void UavBarrier(const Resource& resource) const;

    void Draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t startVertex = 0, uint32_t startInstance = 0) const;
    void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t startIndex = 0, int32_t baseVertex = 0, uint32_t startInstance = 0) const;
    void ExecuteIndirect(const IndirectCommandSignature& commandSignature, const IndirectCommandExecutionDesc& executionDesc) const;
    void DrawIndirect(const IndirectCommandSignature& commandSignature, const IndirectCommandExecutionDesc& executionDesc) const;
    void DispatchIndirect(const IndirectCommandSignature& commandSignature, const IndirectCommandExecutionDesc& executionDesc) const;
    void DispatchMeshIndirect(const IndirectCommandSignature& commandSignature, const IndirectCommandExecutionDesc& executionDesc) const;
    void DispatchRaysIndirect(const IndirectCommandSignature& commandSignature, const IndirectCommandExecutionDesc& executionDesc) const;
    D3D12_DISPATCH_RAYS_DESC BuildDispatchRaysArguments(const RayTracingDispatchDesc& dispatchDesc) const;
    void ClearUnorderedAccessUint(const Resource& resource, const UINT values[4]) const;
    void DispatchMesh(uint32_t numGroupsX, uint32_t numGroupsY = 1, uint32_t numGroupsZ = 1) const;
    void Dispatch(uint32_t numGroupsX, uint32_t numGroupsY = 1, uint32_t numGroupsZ = 1) const;
    void BindDescriptorSet(const RayTracingBindingSet& bindingSet) const;
    void DispatchRays(const RayTracingDispatchDesc& dispatchDesc) const;
    void InsertDescriptorSetOutputBarriers(const RayTracingBindingSet& bindingSet) const;

private:
    void SetPipelineLayout(PipelineBindPoint bindPoint, const PipelineLayout& pipelineLayout) const;
    void SetDescriptorSet(PipelineBindPoint bindPoint, const PipelineDescriptorSetBindDesc& descriptorSetDesc) const;
    void SetDescriptorSet(PipelineBindPoint bindPoint, const PipelineDescriptorSet& descriptorSet) const;
    void SetPipeline(Shader& shader) const;
    void SetPipeline(MeshShader& shader) const;
    void SetPipeline(const ComputeShader& shader) const;
    void SetPipeline(const RayTracingShader& shader) const;
    void SetGraphicsRootSignature(const RootSignature& rootSignature) const;
    void SetComputeRootSignature(const RootSignature& rootSignature) const;
    void SetGraphicsPipelineState(const Microsoft::WRL::ComPtr<ID3D12PipelineState>& pipelineState) const;
    void SetComputePipelineState(const Microsoft::WRL::ComPtr<ID3D12PipelineState>& pipelineState) const;
    void SetRayTracingPipelineState(
        const Microsoft::WRL::ComPtr<ID3D12StateObject>& stateObject,
        const RootSignature& globalRootSignature) const;
    void StageDefaultDescriptorTable(
        PipelineBindPoint bindPoint,
        const PipelineDescriptorSet& descriptorSet,
        UINT rootParameterIndex) const;
    bool TryApplyDescriptorTableBinding(
        PipelineBindPoint bindPoint,
        const PipelineDescriptorSet& descriptorSet,
        const PipelineDescriptorRangeDesc& range,
        const PipelineBoundResource& boundResource,
        UINT rootParameterIndex) const;
    void ApplyGraphicsBinding(const PipelineDescriptorSet& descriptorSet, UINT rootParameterIndex) const;
    void ApplyComputeBinding(const PipelineDescriptorSet& descriptorSet, UINT rootParameterIndex) const;
    void StageDynamicDescriptors(
        D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapType,
        UINT rootParameterIndex,
        UINT offset,
        UINT numDescriptors,
        D3D12_CPU_DESCRIPTOR_HANDLE baseDescriptor) const;
    void SetDescriptorPool(const PipelineDescriptorPool& descriptorPool) const;

    CommandList& m_CommandList;
    mutable CommandContextDescriptorAllocator m_DescriptorAllocator;
    mutable const PipelineDescriptorPool* m_DescriptorPool = nullptr;
    mutable std::array<const PipelineDescriptorSet*, MaxDescriptorSetSlots> m_DescriptorSets = {};
    mutable const RayTracingShader* m_BoundRayTracingShader = nullptr;
    mutable PipelineBindPoint m_BoundPipelineBindPoint = PipelineBindPoint::Graphics;
    mutable bool m_HasBoundPipeline = false;
};

//Modify End
