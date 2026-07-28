//Modify Begin:2026-07-27 by BestHui

#include <Framework/CommandContext.h>

#include <DX12Library/Application.h>
#include <DX12Library/CommandList.h>
#include <DX12Library/Helpers.h>
#include <DX12Library/Resource.h>
#include <DX12Library/StructuredBuffer.h>
#include <Framework/ComputeShader.h>
#include <Framework/PipelineDescriptorSet.h>
#include <Framework/PipelineLayout.h>
#include <Framework/RayTracingAccelerationStructure.h>
#include <Framework/RayTracingPipelineStateBuilder.h>
#include <Framework/RayTracingShader.h>
#include <Framework/Shader.h>
#include <Framework/UnorderedAccessView.h>

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
    pipelineLayout.StageDefaultDescriptorTables(m_CommandList);
}

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

void CommandContext::SetPipeline(const ComputeShader& shader) const
{
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
    const RayTracingPipelineState& pipelineState = shader.GetPipelineState();
    SetRayTracingPipelineState(pipelineState.GetStateObject(), pipelineState.GetGlobalRootSignature());
    SetPipelineLayout(PipelineBindPoint::Compute, shader.GetPipelineLayout());
}

void CommandContext::BindPipeline(Shader& shader) const
{
    SetPipeline(shader);
}

void CommandContext::BindPipeline(const ComputeShader& shader) const
{
    SetPipeline(shader);
}

void CommandContext::BindPipeline(const RayTracingShader& shader) const
{
    SetPipeline(shader);
}

void CommandContext::BindDescriptorSet(const PipelineDescriptorSet& descriptorSet, const PipelineBindPoint bindPoint) const
{
    SetDescriptorSet(bindPoint, descriptorSet);
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
            if (const DescriptorAllocation* allocation = descriptorSet.FindDescriptorTableAllocation(rootParameterIndex))
            {
                StageDynamicDescriptors(
                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                    rootParameterIndex,
                    0u,
                    allocation->GetNumHandles(),
                    allocation->GetDescriptorHandle());
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
        if (const DescriptorAllocation* allocation = descriptorSet.FindDescriptorTableAllocation(rootParameterIndex))
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

            StageDynamicDescriptors(
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                rootParameterIndex,
                0u,
                allocation->GetNumHandles(),
                allocation->GetDescriptorHandle());
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
        if (const DescriptorAllocation* allocation = descriptorSet.FindDescriptorTableAllocation(rootParameterIndex))
        {
            if (unorderedAccessView.m_Resource->AreAutoBarriersEnabled())
            {
                TransitionUnorderedAccessView(m_CommandList, unorderedAccessView);
            }

            StageDynamicDescriptors(
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                rootParameterIndex,
                0u,
                allocation->GetNumHandles(),
                allocation->GetDescriptorHandle());
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
            if (const DescriptorAllocation* allocation = descriptorSet.FindDescriptorTableAllocation(rootParameterIndex))
            {
                StageDynamicDescriptors(
                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                    rootParameterIndex,
                    0u,
                    allocation->GetNumHandles(),
                    allocation->GetDescriptorHandle());
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
        if (const DescriptorAllocation* allocation = descriptorSet.FindDescriptorTableAllocation(rootParameterIndex))
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

            StageDynamicDescriptors(
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                rootParameterIndex,
                0u,
                allocation->GetNumHandles(),
                allocation->GetDescriptorHandle());
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
        if (const DescriptorAllocation* allocation = descriptorSet.FindDescriptorTableAllocation(rootParameterIndex))
        {
            if (unorderedAccessView.m_Resource->AreAutoBarriersEnabled())
            {
                TransitionUnorderedAccessView(m_CommandList, unorderedAccessView);
            }

            StageDynamicDescriptors(
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                rootParameterIndex,
                0u,
                allocation->GetNumHandles(),
                allocation->GetDescriptorHandle());
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
    m_CommandList.Draw(vertexCount, instanceCount, startVertex, startInstance);
}

void CommandContext::Dispatch(const uint32_t numGroupsX, const uint32_t numGroupsY, const uint32_t numGroupsZ) const
{
    m_CommandList.Dispatch(numGroupsX, numGroupsY, numGroupsZ);
}

void CommandContext::BindRayTracingDescriptorSet(const RayTracingBindingSet& bindingSet) const
{
//Modify Begin:2026-07-27 by BestHui
    const RayTracingShader& shader = bindingSet.GetShader();
    const PipelineDescriptorSet& descriptorSet = bindingSet.GetDescriptorSet();
    Assert(descriptorSet.GetAccelerationStructure() != nullptr, "Ray tracing acceleration structure is not bound.");

    SetPipeline(shader);
    SetDescriptorSet(PipelineBindPoint::Compute, descriptorSet);
//Modify End
}

void CommandContext::DispatchRays(const D3D12_DISPATCH_RAYS_DESC& dispatchRaysDesc) const
{
    m_CommandList.DispatchRays(dispatchRaysDesc);
}

//Modify End
