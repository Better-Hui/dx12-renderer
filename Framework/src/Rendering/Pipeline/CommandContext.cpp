//Modify Begin:2026-07-27 by BestHui

#include <Framework/Rendering/Pipeline/CommandContext.h>

#include <DX12Library/Application.h>
#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/Resource.h>
#include <DX12Library/StructuredBuffer.h>
//Modify Begin:2026-07-30 by BestHui
#include <Framework/Rendering/Pipeline/BindlessDescriptorHeap.h>
//Modify End
#include <Framework/Rendering/Pipeline/ComputeShader.h>
//Modify Begin:2026-07-30 by BestHui
#include <Framework/Rendering/Pipeline/MeshShader.h>
//Modify End
#include <Framework/Rendering/Pipeline/PipelineDescriptorPool.h>
#include <Framework/Rendering/Pipeline/PipelineDescriptorSet.h>
#include <Framework/Rendering/Pipeline/PipelineLayout.h>
#include <Framework/Rendering/RayTracing/RayTracingAccelerationStructure.h>
#include <Framework/Rendering/RayTracing/RayTracingPipelineStateBuilder.h>
#include <Framework/Rendering/RayTracing/RayTracingShader.h>
#include <Framework/Rendering/Pipeline/Shader.h>
#include <Framework/Rendering/Texture/UnorderedAccessView.h>

#include <cstring>
#include <set>

//Modify Begin:2026-07-27 by BestHui
namespace
{
    void TransitionShaderResourceBinding(CommandList& commandList, const PipelineShaderResourceBinding& shaderResource)
    {
//Modify Begin:2026-07-28 by BestHui
        if (shaderResource.Resource == nullptr || !shaderResource.Resource->AreAutoBarriersEnabled())
        {
            return;
        }
//Modify End
        if (shaderResource.NumSubresources < D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
        {
            for (UINT i = 0; i < shaderResource.NumSubresources; ++i)
            {
                commandList.TransitionBarrier(*shaderResource.Resource, shaderResource.StateAfter, shaderResource.FirstSubresource + i);
            }
        }
        else
        {
            commandList.TransitionBarrier(*shaderResource.Resource, shaderResource.StateAfter);
        }
    }

    void TransitionUnorderedAccessView(CommandList& commandList, const UnorderedAccessView& unorderedAccessView)
    {
//Modify Begin:2026-07-28 by BestHui
        if (unorderedAccessView.m_Resource == nullptr || !unorderedAccessView.m_Resource->AreAutoBarriersEnabled())
        {
            return;
        }
//Modify End
        if (unorderedAccessView.m_NumSubresources < D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
        {
            for (UINT i = 0; i < unorderedAccessView.m_NumSubresources; ++i)
            {
                commandList.TransitionBarrier(*unorderedAccessView.m_Resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, unorderedAccessView.m_FirstSubresource + i);
            }
        }
        else
        {
            commandList.TransitionBarrier(*unorderedAccessView.m_Resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
    }

    void InsertDescriptorSetOutputBarriersImpl(CommandList& commandList, const PipelineDescriptorSet& descriptorSet)
    {
        for (const auto& [rootParameterIndex, boundResource] : descriptorSet.GetBoundResources())
        {
            (void)rootParameterIndex;
            if (boundResource.UnorderedAccessView.has_value() &&
                boundResource.UnorderedAccessView->m_Resource != nullptr &&
                boundResource.UnorderedAccessView->m_Resource->AreAutoBarriersEnabled())
            {
                commandList.UavBarrier(*boundResource.UnorderedAccessView->m_Resource);
            }
        }
    }

}
//Modify End

CommandContext::CommandContext(CommandList& commandList)
    : m_CommandList(commandList)
{
}

void CommandContext::SetPipelineLayout(const PipelineBindPoint bindPoint, const PipelineLayout& pipelineLayout) const
{
    const RootSignature* rootSignature = pipelineLayout.GetRootSignature();
    Assert(rootSignature != nullptr, "Pipeline layout does not have a root signature.");

    if (bindPoint == PipelineBindPoint::Graphics)
    {
        SetGraphicsRootSignature(*rootSignature);
    }
    else
    {
        SetComputeRootSignature(*rootSignature);
    }
//Modify Begin:2026-07-29 by BestHui
    m_DescriptorAllocator.ResetTransientBindings();
    (void)pipelineLayout;
//Modify End
}

//Modify Begin:2026-07-29 by BestHui
void CommandContext::SetDescriptorPool(const PipelineDescriptorPool& descriptorPool) const
{
    m_DescriptorPool = &descriptorPool;
}

void CommandContext::SetDescriptorSet(
    const PipelineBindPoint bindPoint,
    const PipelineDescriptorSetBindDesc& descriptorSetDesc) const
{
    Assert(descriptorSetDesc.DescriptorSet != nullptr, "Pipeline descriptor set bind desc has no descriptor set.");
    const PipelineDescriptorSet& descriptorSet = *descriptorSetDesc.DescriptorSet;
    if (const PipelineDescriptorPool* descriptorPool = descriptorSet.GetDescriptorPool())
    {
        SetDescriptorPool(*descriptorPool);
    }
    Assert(
        descriptorSetDesc.SetIndex == descriptorSet.GetSetIndex(),
        "Pipeline descriptor set bind desc set index does not match the allocated descriptor set.");
    Assert(
        descriptorSetDesc.SetIndex < MaxDescriptorSetSlots,
        "Pipeline descriptor set index exceeds CommandContext descriptor set slots.");
    m_DescriptorSets[descriptorSetDesc.SetIndex] = &descriptorSet;
    SetDescriptorSet(bindPoint, descriptorSet);
}
//Modify End

void CommandContext::SetDescriptorSet(const PipelineBindPoint bindPoint, const PipelineDescriptorSet& descriptorSet) const
{
//Modify Begin:2026-07-27 by BestHui
    std::set<UINT> appliedRootParameters;
    for (const auto& [rootParameterIndex, boundResource] : descriptorSet.GetBoundResources())
    {
        (void)boundResource;
        appliedRootParameters.insert(rootParameterIndex);
        switch (bindPoint)
        {
        case PipelineBindPoint::Graphics:
            ApplyGraphicsBinding(descriptorSet, rootParameterIndex);
            break;
        case PipelineBindPoint::Compute:
        case PipelineBindPoint::RayTracing:
            ApplyComputeBinding(descriptorSet, rootParameterIndex);
            break;
        default:
            Assert(false, "Unsupported pipeline bind point.");
            break;
        }
    }

    for (const PipelineDescriptorRangeDesc& range : descriptorSet.GetLayout().GetDesc().DescriptorRanges)
    {
        if (range.BindingMode != PipelineDescriptorBindingMode::DescriptorTable ||
            appliedRootParameters.find(range.RootParameterIndex) != appliedRootParameters.end())
        {
            continue;
        }

        switch (bindPoint)
        {
        case PipelineBindPoint::Graphics:
            ApplyGraphicsBinding(descriptorSet, range.RootParameterIndex);
            break;
        case PipelineBindPoint::Compute:
        case PipelineBindPoint::RayTracing:
            ApplyComputeBinding(descriptorSet, range.RootParameterIndex);
            break;
        default:
            Assert(false, "Unsupported pipeline bind point.");
            break;
        }
    }
//Modify End
}

void CommandContext::SetPipeline(Shader& shader) const
{
    m_BoundPipelineBindPoint = PipelineBindPoint::Graphics;
    m_HasBoundPipeline = true;
    m_BoundRayTracingShader = nullptr;

    const auto device = Application::Get().GetDevice();
    const auto& renderTargetState = m_CommandList.GetLastRenderTargetState();
    const auto pipelineState = shader.GetPipelineState(device, renderTargetState);

    if (shader.UsesReflectedRootSignature())
    {
        SetPipelineLayout(PipelineBindPoint::Graphics, *shader.GetPipelineLayout());
    }
    else
    {
        SetGraphicsRootSignature(shader.GetRootSignature());
        shader.StageDefaultDescriptorTables(m_CommandList);
    }

    SetGraphicsPipelineState(pipelineState);
}

//Modify Begin:2026-07-30 by BestHui
void CommandContext::SetPipeline(MeshShader& shader) const
{
    m_BoundPipelineBindPoint = PipelineBindPoint::Graphics;
    m_HasBoundPipeline = true;
    m_BoundRayTracingShader = nullptr;

    const auto device = Application::Get().GetDevice();
    const auto& renderTargetState = m_CommandList.GetLastRenderTargetState();
    const auto pipelineState = shader.GetPipelineState(device, renderTargetState);

    if (shader.UsesReflectedRootSignature())
    {
        SetPipelineLayout(PipelineBindPoint::Graphics, *shader.GetPipelineLayout());
    }
    else
    {
        SetGraphicsRootSignature(shader.GetRootSignature());
    }

    SetGraphicsPipelineState(pipelineState);
}
//Modify End

void CommandContext::SetPipeline(const ComputeShader& shader) const
{
    m_BoundPipelineBindPoint = PipelineBindPoint::Compute;
    m_HasBoundPipeline = true;
    m_BoundRayTracingShader = nullptr;

    const auto device = Application::Get().GetDevice();
    const auto pipelineState = shader.GetPipelineState(device);

    if (shader.UsesReflectedRootSignature())
    {
        SetPipelineLayout(PipelineBindPoint::Compute, *shader.GetPipelineLayout());
    }
    else
    {
        SetComputeRootSignature(shader.GetRootSignature());
        shader.StageDefaultDescriptorTables(m_CommandList);
    }
    SetComputePipelineState(pipelineState);
}

void CommandContext::SetPipeline(const RayTracingShader& shader) const
{
    m_BoundPipelineBindPoint = PipelineBindPoint::RayTracing;
    m_HasBoundPipeline = true;

    const RayTracingPipelineState& pipelineState = shader.GetPipelineState();
    SetRayTracingPipelineState(pipelineState.GetStateObject(), pipelineState.GetGlobalRootSignature());
    SetPipelineLayout(PipelineBindPoint::RayTracing, shader.GetPipelineLayout());
    m_BoundRayTracingShader = &shader;
}

void CommandContext::BindPipeline(Shader& shader) const
{
    SetPipeline(shader);
}

//Modify Begin:2026-07-30 by BestHui
void CommandContext::BindPipeline(MeshShader& shader) const
{
    SetPipeline(shader);
}
//Modify End

void CommandContext::BindPipeline(const ComputeShader& shader) const
{
    SetPipeline(shader);
}

void CommandContext::BindPipeline(const RayTracingShader& shader) const
{
    SetPipeline(shader);
}

//Modify Begin:2026-07-30 by BestHui
void CommandContext::BindBindlessDescriptorHeap(BindlessDescriptorHeap& bindlessDescriptorHeap) const
{
    m_CommandList.BindShaderVisibleDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        bindlessDescriptorHeap.GetResourceDescriptorHeap());
    m_DescriptorAllocator.SetBindlessDescriptorHeap(&bindlessDescriptorHeap);
}
//Modify End

void CommandContext::BindDescriptorSet(const PipelineDescriptorSetBindDesc& descriptorSetDesc) const
{
//Modify Begin:2026-07-29 by BestHui
    Assert(m_HasBoundPipeline, "A pipeline must be bound before binding a descriptor set.");
    SetDescriptorSet(m_BoundPipelineBindPoint, descriptorSetDesc);
//Modify End
}

void CommandContext::BindDescriptorSet(const PipelineDescriptorSet& descriptorSet) const
{
    BindDescriptorSet(PipelineDescriptorSetBindDesc{ descriptorSet.GetSetIndex(), &descriptorSet });
}

void CommandContext::SetGraphicsRootSignature(const RootSignature& rootSignature) const
{
    m_CommandList.SetGraphicsRootSignature(rootSignature);
}

void CommandContext::SetComputeRootSignature(const RootSignature& rootSignature) const
{
    m_CommandList.SetComputeRootSignature(rootSignature);
}

void CommandContext::SetGraphicsPipelineState(const Microsoft::WRL::ComPtr<ID3D12PipelineState>& pipelineState) const
{
    m_CommandList.SetPipelineState(pipelineState);
}

void CommandContext::SetComputePipelineState(const Microsoft::WRL::ComPtr<ID3D12PipelineState>& pipelineState) const
{
    m_CommandList.SetPipelineState(pipelineState);
}

void CommandContext::SetRayTracingPipelineState(
    const Microsoft::WRL::ComPtr<ID3D12StateObject>& stateObject,
    const RootSignature& globalRootSignature) const
{
    m_CommandList.SetRaytracingPipelineState(stateObject);
    m_CommandList.SetComputeRootSignature(globalRootSignature);
}

void CommandContext::ApplyGraphicsBinding(const PipelineDescriptorSet& descriptorSet, const UINT rootParameterIndex) const
{
    const PipelineLayout& layout = descriptorSet.GetLayout();
    const PipelineDescriptorRangeDesc* range = layout.FindRangeByRootParameterIndex(rootParameterIndex);
    Assert(range != nullptr, "Pipeline descriptor set binding was not found.");

    const PipelineBoundResource* boundResource = descriptorSet.FindBoundResource(rootParameterIndex);
    if (boundResource == nullptr)
    {
//Modify Begin:2026-07-27 by BestHui
        if (range->BindingMode == PipelineDescriptorBindingMode::DescriptorTable)
        {
            if (const PipelineDescriptorTableAllocation* allocation = descriptorSet.FindDescriptorTableAllocation(rootParameterIndex))
            {
                m_DescriptorAllocator.StageDescriptorTable(m_CommandList, PipelineBindPoint::Graphics, rootParameterIndex, *allocation);
            }
        }
//Modify End
        return;
    }

    if (range->Kind == DescriptorBindingKind::ConstantBuffer)
    {
        Assert(!boundResource->ConstantBufferData.empty(), "Pipeline constant buffer is not bound.");
        m_CommandList.SetGraphicsDynamicConstantBuffer(
            rootParameterIndex,
            boundResource->ConstantBufferData.size(),
            boundResource->ConstantBufferData.data());
        return;
    }

    Assert(range->Kind != DescriptorBindingKind::AccelerationStructure, "Graphics pipeline does not support acceleration structure root bindings.");

    if (range->Kind == DescriptorBindingKind::ShaderResourceView)
    {
        Assert(range->BindingMode == PipelineDescriptorBindingMode::DescriptorTable, "Graphics root SRV bindings are not supported yet.");

//Modify Begin:2026-07-27 by BestHui
        if (const PipelineDescriptorTableAllocation* allocation = descriptorSet.FindDescriptorTableAllocation(rootParameterIndex))
        {
            for (const auto& shaderResource : boundResource->ShaderResources)
            {
                if (!shaderResource.has_value() || shaderResource->Resource == nullptr)
                {
                    continue;
                }

                if (shaderResource->AutoTransition && shaderResource->Resource->AreAutoBarriersEnabled())
                {
                    TransitionShaderResourceBinding(m_CommandList, *shaderResource);
                }
            }

            m_DescriptorAllocator.StageDescriptorTable(m_CommandList, PipelineBindPoint::Graphics, rootParameterIndex, *allocation);
            return;
        }
//Modify End

        const size_t shaderResourceCount = boundResource->ShaderResources.size();
        for (UINT i = 0; i < static_cast<UINT>(shaderResourceCount); ++i)
        {
            if (i >= boundResource->ShaderResources.size() || !boundResource->ShaderResources[i].has_value())
            {
                continue;
            }

            const PipelineShaderResourceBinding& shaderResource = *boundResource->ShaderResources[i];
            Assert(shaderResource.Resource != nullptr, "Pipeline SRV resource is not bound.");
            const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc = shaderResource.HasDesc ? &shaderResource.Desc : nullptr;
            if (shaderResource.AutoTransition && shaderResource.Resource->AreAutoBarriersEnabled())
            {
                m_CommandList.SetShaderResourceView(
                    rootParameterIndex,
                    i,
                    *shaderResource.Resource,
                    shaderResource.StateAfter,
                    shaderResource.FirstSubresource,
                    shaderResource.NumSubresources,
                    srvDesc);
            }
            else
            {
                m_CommandList.SetShaderResourceView(
                    rootParameterIndex,
                    i,
                    *shaderResource.Resource,
                    shaderResource.FirstSubresource,
                    shaderResource.NumSubresources,
                    srvDesc);
            }
        }
        return;
    }

    if (range->Kind == DescriptorBindingKind::UnorderedAccessView)
    {
        Assert(boundResource->UnorderedAccessView.has_value(), "Pipeline UAV resource is not bound.");
        const UnorderedAccessView& unorderedAccessView = *boundResource->UnorderedAccessView;
//Modify Begin:2026-07-27 by BestHui
        if (const PipelineDescriptorTableAllocation* allocation = descriptorSet.FindDescriptorTableAllocation(rootParameterIndex))
        {
            if (unorderedAccessView.m_Resource->AreAutoBarriersEnabled())
            {
                TransitionUnorderedAccessView(m_CommandList, unorderedAccessView);
            }

            m_DescriptorAllocator.StageDescriptorTable(m_CommandList, PipelineBindPoint::Graphics, rootParameterIndex, *allocation);
            return;
        }
//Modify End
        if (unorderedAccessView.m_Resource->AreAutoBarriersEnabled())
        {
            m_CommandList.SetUnorderedAccessView(
                rootParameterIndex,
                0u,
                *unorderedAccessView.m_Resource,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                unorderedAccessView.m_FirstSubresource,
                unorderedAccessView.m_NumSubresources,
                unorderedAccessView.GetDescOrNullptr());
        }
        else
        {
            m_CommandList.SetUnorderedAccessView(
                rootParameterIndex,
                0u,
                *unorderedAccessView.m_Resource,
                unorderedAccessView.m_FirstSubresource,
                unorderedAccessView.m_NumSubresources,
                unorderedAccessView.GetDescOrNullptr());
        }
    }
}

void CommandContext::ApplyComputeBinding(const PipelineDescriptorSet& descriptorSet, const UINT rootParameterIndex) const
{
    const PipelineLayout& layout = descriptorSet.GetLayout();
    const PipelineDescriptorRangeDesc* range = layout.FindRangeByRootParameterIndex(rootParameterIndex);
    Assert(range != nullptr, "Pipeline descriptor set binding was not found.");

    const PipelineBoundResource* boundResource = descriptorSet.FindBoundResource(rootParameterIndex);
    if (boundResource == nullptr)
    {
//Modify Begin:2026-07-27 by BestHui
        if (range->BindingMode == PipelineDescriptorBindingMode::DescriptorTable)
        {
            if (const PipelineDescriptorTableAllocation* allocation = descriptorSet.FindDescriptorTableAllocation(rootParameterIndex))
            {
                m_DescriptorAllocator.StageDescriptorTable(m_CommandList, PipelineBindPoint::Compute, rootParameterIndex, *allocation);
            }
        }
//Modify End
        return;
    }

    if (range->Kind == DescriptorBindingKind::ConstantBuffer)
    {
        Assert(!boundResource->ConstantBufferData.empty(), "Pipeline constant buffer is not bound.");
        auto allocation = m_CommandList.AllocateInUploadBuffer(
            boundResource->ConstantBufferData.size(),
            D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
        std::memcpy(allocation.Cpu, boundResource->ConstantBufferData.data(), boundResource->ConstantBufferData.size());
        m_CommandList.SetComputeRootConstantBufferView(rootParameterIndex, allocation.Gpu);
        return;
    }

    if (range->Kind == DescriptorBindingKind::AccelerationStructure)
    {
        const RayTracingAccelerationStructure* accelerationStructure = boundResource->AccelerationStructure != nullptr ?
            boundResource->AccelerationStructure :
            descriptorSet.GetAccelerationStructure();
        Assert(accelerationStructure != nullptr && accelerationStructure->IsBuilt(), "Pipeline acceleration structure is not bound.");
        m_CommandList.SetComputeRootShaderResourceView(rootParameterIndex, accelerationStructure->GetGpuVirtualAddress());
        return;
    }

    if (range->Kind == DescriptorBindingKind::ShaderResourceView)
    {
        if (range->BindingMode == PipelineDescriptorBindingMode::RootDescriptor)
        {
            Assert(boundResource->StructuredBufferResource != nullptr, "Pipeline root SRV structured buffer is not bound.");
            if (boundResource->StructuredBufferResource->AreAutoBarriersEnabled())
            {
                m_CommandList.TransitionBarrier(*boundResource->StructuredBufferResource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            }
            m_CommandList.SetComputeRootShaderResourceView(
                rootParameterIndex,
                boundResource->StructuredBufferResource->GetD3D12Resource()->GetGPUVirtualAddress());
            return;
        }

//Modify Begin:2026-07-27 by BestHui
        if (const PipelineDescriptorTableAllocation* allocation = descriptorSet.FindDescriptorTableAllocation(rootParameterIndex))
        {
            for (const auto& shaderResource : boundResource->ShaderResources)
            {
                if (!shaderResource.has_value() || shaderResource->Resource == nullptr)
                {
                    continue;
                }

                if (shaderResource->AutoTransition && shaderResource->Resource->AreAutoBarriersEnabled())
                {
                    TransitionShaderResourceBinding(m_CommandList, *shaderResource);
                }
            }

            m_DescriptorAllocator.StageDescriptorTable(m_CommandList, PipelineBindPoint::Compute, rootParameterIndex, *allocation);
            return;
        }
//Modify End

        const size_t shaderResourceCount = boundResource->ShaderResources.size();
        for (UINT i = 0; i < static_cast<UINT>(shaderResourceCount); ++i)
        {
            if (i >= boundResource->ShaderResources.size() || !boundResource->ShaderResources[i].has_value())
            {
                continue;
            }

            const PipelineShaderResourceBinding& shaderResource = *boundResource->ShaderResources[i];
            Assert(shaderResource.Resource != nullptr, "Pipeline SRV resource is not bound.");
            const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc = shaderResource.HasDesc ? &shaderResource.Desc : nullptr;
            if (shaderResource.AutoTransition && shaderResource.Resource->AreAutoBarriersEnabled())
            {
                m_CommandList.SetShaderResourceView(
                    rootParameterIndex,
                    i,
                    *shaderResource.Resource,
                    shaderResource.StateAfter,
                    shaderResource.FirstSubresource,
                    shaderResource.NumSubresources,
                    srvDesc);
            }
            else
            {
                m_CommandList.SetShaderResourceView(
                    rootParameterIndex,
                    i,
                    *shaderResource.Resource,
                    shaderResource.FirstSubresource,
                    shaderResource.NumSubresources,
                    srvDesc);
            }
        }
        return;
    }

    if (range->Kind == DescriptorBindingKind::UnorderedAccessView)
    {
        Assert(boundResource->UnorderedAccessView.has_value(), "Pipeline UAV resource is not bound.");
        const UnorderedAccessView& unorderedAccessView = *boundResource->UnorderedAccessView;
//Modify Begin:2026-07-27 by BestHui
        if (const PipelineDescriptorTableAllocation* allocation = descriptorSet.FindDescriptorTableAllocation(rootParameterIndex))
        {
            if (unorderedAccessView.m_Resource->AreAutoBarriersEnabled())
            {
                TransitionUnorderedAccessView(m_CommandList, unorderedAccessView);
            }

            m_DescriptorAllocator.StageDescriptorTable(m_CommandList, PipelineBindPoint::Compute, rootParameterIndex, *allocation);
            return;
        }
//Modify End
        if (unorderedAccessView.m_Resource->AreAutoBarriersEnabled())
        {
            m_CommandList.SetUnorderedAccessView(
                rootParameterIndex,
                0u,
                *unorderedAccessView.m_Resource,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                unorderedAccessView.m_FirstSubresource,
                unorderedAccessView.m_NumSubresources,
                unorderedAccessView.GetDescOrNullptr());
        }
        else
        {
            m_CommandList.SetUnorderedAccessView(
                rootParameterIndex,
                0u,
                *unorderedAccessView.m_Resource,
                unorderedAccessView.m_FirstSubresource,
                unorderedAccessView.m_NumSubresources,
                unorderedAccessView.GetDescOrNullptr());
        }
    }
}

void CommandContext::StageDynamicDescriptors(
    const D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapType,
    const UINT rootParameterIndex,
    const UINT offset,
    const UINT numDescriptors,
    const D3D12_CPU_DESCRIPTOR_HANDLE baseDescriptor) const
{
    m_CommandList.StageDynamicDescriptors(descriptorHeapType, rootParameterIndex, offset, numDescriptors, baseDescriptor);
}

//Modify Begin:2026-07-27 by BestHui
void CommandContext::InsertDescriptorSetOutputBarriers(const PipelineDescriptorSet& descriptorSet) const
{
    InsertDescriptorSetOutputBarriersImpl(m_CommandList, descriptorSet);
}
//Modify End

void CommandContext::TransitionShaderResource(const Resource& resource) const
{
    if (resource.AreAutoBarriersEnabled())
    {
        m_CommandList.TransitionBarrier(resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
}

void CommandContext::TransitionUnorderedAccess(const Resource& resource) const
{
    if (resource.AreAutoBarriersEnabled())
    {
        m_CommandList.TransitionBarrier(resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
}

void CommandContext::UavBarrier(const Resource& resource) const
{
    m_CommandList.UavBarrier(resource);
}

void CommandContext::Draw(
    const uint32_t vertexCount,
    const uint32_t instanceCount,
    const uint32_t startVertex,
    const uint32_t startInstance) const
{
//Modify Begin:2026-07-30 by BestHui
    if (m_DescriptorAllocator.HasBindlessDescriptorHeap())
    {
        m_CommandList.FlushResourceBarriers();
        m_CommandList.GetGraphicsCommandList()->DrawInstanced(vertexCount, instanceCount, startVertex, startInstance);
        return;
    }
//Modify End
    m_CommandList.Draw(vertexCount, instanceCount, startVertex, startInstance);
}

//Modify Begin:2026-07-30 by BestHui
void CommandContext::DrawIndexed(
    const uint32_t indexCount,
    const uint32_t instanceCount,
    const uint32_t startIndex,
    const int32_t baseVertex,
    const uint32_t startInstance) const
{
    if (m_DescriptorAllocator.HasBindlessDescriptorHeap())
    {
        m_CommandList.FlushResourceBarriers();
        m_CommandList.GetGraphicsCommandList()->DrawIndexedInstanced(indexCount, instanceCount, startIndex, baseVertex, startInstance);
        return;
    }
    m_CommandList.DrawIndexed(indexCount, instanceCount, startIndex, baseVertex, startInstance);
}
//Modify End

//Modify Begin:2026-07-30 by BestHui
void CommandContext::DispatchMesh(const uint32_t numGroupsX, const uint32_t numGroupsY, const uint32_t numGroupsZ) const
{
    if (m_DescriptorAllocator.HasBindlessDescriptorHeap())
    {
        m_CommandList.FlushResourceBarriers();
        m_CommandList.GetGraphicsCommandList6()->DispatchMesh(numGroupsX, numGroupsY, numGroupsZ);
        return;
    }
    m_CommandList.DispatchMesh(numGroupsX, numGroupsY, numGroupsZ);
}
//Modify End

void CommandContext::Dispatch(const uint32_t numGroupsX, const uint32_t numGroupsY, const uint32_t numGroupsZ) const
{
//Modify Begin:2026-07-30 by BestHui
    if (m_DescriptorAllocator.HasBindlessDescriptorHeap())
    {
        m_CommandList.FlushResourceBarriers();
        m_CommandList.GetGraphicsCommandList()->Dispatch(numGroupsX, numGroupsY, numGroupsZ);
        return;
    }
//Modify End
    m_CommandList.Dispatch(numGroupsX, numGroupsY, numGroupsZ);
}

void CommandContext::BindDescriptorSet(const RayTracingBindingSet& bindingSet) const
{
//Modify Begin:2026-07-27 by BestHui
    const RayTracingShader& shader = bindingSet.GetShader();
    const PipelineDescriptorSet& descriptorSet = bindingSet.GetDescriptorSet();
    Assert(descriptorSet.GetAccelerationStructure() != nullptr, "Ray tracing acceleration structure is not bound.");

    if (m_BoundRayTracingShader != &shader)
    {
        SetPipeline(shader);
    }
//Modify Begin:2026-07-29 by BestHui
    SetDescriptorPool(bindingSet.GetDescriptorPool());
    SetDescriptorSet(PipelineBindPoint::RayTracing, PipelineDescriptorSetBindDesc{ descriptorSet.GetSetIndex(), &descriptorSet });
//Modify End
//Modify End
}

void CommandContext::DispatchRays(const RayTracingDispatchDesc& dispatchDesc) const
{
    Assert(m_BoundRayTracingShader != nullptr, "Ray tracing pipeline must be bound before DispatchRays.");
    const D3D12_DISPATCH_RAYS_DESC d3d12DispatchDesc = m_BoundRayTracingShader->BuildDispatchDesc(
        dispatchDesc.PassName,
        dispatchDesc.Width,
        dispatchDesc.Height,
        dispatchDesc.Depth);
//Modify Begin:2026-07-30 by BestHui
    if (m_DescriptorAllocator.HasBindlessDescriptorHeap())
    {
        m_CommandList.FlushResourceBarriers();
        m_CommandList.GetGraphicsCommandList5()->DispatchRays(&d3d12DispatchDesc);
        return;
    }
//Modify End
    m_CommandList.DispatchRays(d3d12DispatchDesc);
}

void CommandContext::InsertDescriptorSetOutputBarriers(const RayTracingBindingSet& bindingSet) const
{
    InsertDescriptorSetOutputBarriers(bindingSet.GetDescriptorSet());
}

//Modify End
