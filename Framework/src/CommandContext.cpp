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
    for (const auto& [rootParameterIndex, boundResource] : descriptorSet.GetBoundResources())
    {
        (void)boundResource;
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
            if (shaderResource.Resource->AreAutoBarriersEnabled())
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
            if (shaderResource.Resource->AreAutoBarriersEnabled())
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

void CommandContext::DispatchRays(
    RayTracingBindingSet& bindingSet,
    std::string_view passName,
    const uint32_t width,
    const uint32_t height,
    const uint32_t depth) const
{
    bindingSet.PrepareDispatch(passName);
    bindingSet.TransitionDispatchResources(*this);
    SetPipeline(bindingSet.GetShader());
    bindingSet.StageDescriptorTable(*this);
    bindingSet.ApplyRootBindings(*this);
    DispatchRays(bindingSet.BuildDispatchDesc(width, height, depth));
    bindingSet.InsertOutputBarriers(*this);
}

void CommandContext::DispatchRays(const D3D12_DISPATCH_RAYS_DESC& dispatchRaysDesc) const
{
    m_CommandList.DispatchRays(dispatchRaysDesc);
}

//Modify End
