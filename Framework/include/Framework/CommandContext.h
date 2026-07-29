#pragma once

//Modify Begin:2026-07-27 by BestHui

#include <d3d12.h>
#include <wrl.h>

//Modify Begin:2026-07-29 by BestHui
#include <Framework/CommandContextDescriptorAllocator.h>
//Modify End

#include <array>
#include <cstdint>
#include <string_view>

class CommandList;
class ComputeShader;
class PipelineDescriptorPool;
class PipelineDescriptorSet;
class PipelineLayout;
class RayTracingBindingSet;
class RayTracingShader;
class Resource;
class RootSignature;
class Shader;

enum class PipelineBindPoint
{
    Graphics,
    Compute
};

//Modify Begin:2026-07-29 by BestHui
struct PipelineDescriptorSetBindDesc
{
    uint32_t SetIndex = 0;
    const PipelineDescriptorSet* DescriptorSet = nullptr;
};
//Modify End

class CommandContext final
{
public:
//Modify Begin:2026-07-29 by BestHui
    static constexpr uint32_t MaxDescriptorSetSlots = 16;
//Modify End
    explicit CommandContext(CommandList& commandList);

    CommandList& GetCommandList() const { return m_CommandList; }

    void SetPipelineLayout(PipelineBindPoint bindPoint, const PipelineLayout& pipelineLayout) const;
//Modify Begin:2026-07-29 by BestHui
    void SetDescriptorSet(PipelineBindPoint bindPoint, const PipelineDescriptorSetBindDesc& descriptorSetDesc) const;
//Modify End
    void SetDescriptorSet(PipelineBindPoint bindPoint, const PipelineDescriptorSet& descriptorSet) const;
    void SetPipeline(Shader& shader) const;
    void SetPipeline(const ComputeShader& shader) const;
    void SetPipeline(const RayTracingShader& shader) const;

    void BindPipeline(Shader& shader) const;
    void BindPipeline(const ComputeShader& shader) const;
    void BindPipeline(const RayTracingShader& shader) const;
    void BindDescriptorSet(const PipelineDescriptorSet& descriptorSet, PipelineBindPoint bindPoint) const;

    void SetGraphicsRootSignature(const RootSignature& rootSignature) const;
    void SetComputeRootSignature(const RootSignature& rootSignature) const;
    void SetGraphicsPipelineState(const Microsoft::WRL::ComPtr<ID3D12PipelineState>& pipelineState) const;
    void SetComputePipelineState(const Microsoft::WRL::ComPtr<ID3D12PipelineState>& pipelineState) const;
    void SetRayTracingPipelineState(
        const Microsoft::WRL::ComPtr<ID3D12StateObject>& stateObject,
        const RootSignature& globalRootSignature) const;

    void ApplyGraphicsBinding(const PipelineDescriptorSet& descriptorSet, UINT rootParameterIndex) const;
    void ApplyComputeBinding(const PipelineDescriptorSet& descriptorSet, UINT rootParameterIndex) const;
    void StageDynamicDescriptors(
        D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapType,
        UINT rootParameterIndex,
        UINT offset,
        UINT numDescriptors,
        D3D12_CPU_DESCRIPTOR_HANDLE baseDescriptor) const;
//Modify Begin:2026-07-27 by BestHui
    void InsertDescriptorSetOutputBarriers(const PipelineDescriptorSet& descriptorSet) const;
//Modify End

    void TransitionShaderResource(const Resource& resource) const;
    void TransitionUnorderedAccess(const Resource& resource) const;
    void UavBarrier(const Resource& resource) const;

    void Draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t startVertex = 0, uint32_t startInstance = 0) const;
    void Dispatch(uint32_t numGroupsX, uint32_t numGroupsY = 1, uint32_t numGroupsZ = 1) const;
    void BindRayTracingDescriptorSet(const RayTracingBindingSet& bindingSet) const;
    void DispatchRays(const D3D12_DISPATCH_RAYS_DESC& dispatchRaysDesc) const;

private:
//Modify Begin:2026-07-29 by BestHui
    void SetDescriptorPool(const PipelineDescriptorPool& descriptorPool) const;
//Modify End

    CommandList& m_CommandList;
//Modify Begin:2026-07-29 by BestHui
    mutable CommandContextDescriptorAllocator m_DescriptorAllocator;
    mutable const PipelineDescriptorPool* m_DescriptorPool = nullptr;
    mutable std::array<const PipelineDescriptorSet*, MaxDescriptorSetSlots> m_DescriptorSets = {};
//Modify End
};

//Modify End
